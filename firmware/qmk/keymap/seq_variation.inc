/* Deterministic, allocation-free playback transforms. Group identity is onset;
 * reordered chords use the destination gate, while untouched groups retain each
 * voice's original release. The source events and palette never change. */
static uint32_t seq_variation_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16);
}
static uint32_t seq_variation_seed(uint8_t track) {
    const seq_variation_t *v = &seq_variations[track];
    return v->locked ? v->seed : seq_variation_hash(v->seed ^ (seq_step_position / seq_track_playback_length(track)));
}
static uint8_t seq_variation_select(const seq_variation_t *v, uint8_t count, uint16_t position, uint32_t seed) {
    if (!count) return 0;
    uint8_t index = position % count;
    if (!v->enabled) return index;
    if (v->order == 1) return count - 1u - index;
    if (v->order == 2) return (index + v->rotate) % count;
    if (v->order == 4) return seq_variation_hash(seed ^ position ^ 0x7919u) % count;
    if (v->order == 3) {
        uint8_t order[MEL_TRACK_MAX_EVENTS];
        for (uint8_t i = 0; i < count; i++) order[i] = i;
        for (uint8_t n = count; n > 1; n--) {
            seed = seq_variation_hash(seed + n);
            uint8_t j = seed % n, value = order[n - 1];
            order[n - 1] = order[j]; order[j] = value;
        }
        return order[index];
    }
    return index;
}
static int16_t seq_variation_mutation(uint8_t track, uint8_t root, uint32_t random) {
    const seq_variation_t *v = &seq_variations[track];
    if (!v->enabled || !v->mutation || !v->range || random % 100u >= v->mutation) return 0;
    const seq_scale_track_t *scale = &seq_scale_tracks[track];
    if (scale->mode == 2 || (!scale->mode && !seq_scale_global.enabled)) {
        const seq_euclid_track_t *e = &euclid_tracks[track];
        if (!e->note_count) return 0;
        /* Scale off: choose only authored palette roots within the range. */
        uint8_t choices[8], count = 0;
        for (uint8_t i = 0; i < e->note_count; i++) {
            int16_t pitch = seq_melodic_note_pitch(track, e->palette_root) + e->note_pool[i];
            if (pitch >= 0 && pitch <= 127 && abs(pitch - root) <= v->range) choices[count++] = pitch;
        }
        return count ? (int16_t)choices[(random / 100u) % count] - root : 0;
    }
    uint8_t key = scale->mode == 1 ? scale->root : seq_scale_global.root;
    uint8_t kind = scale->mode == 1 ? scale->kind : seq_scale_global.kind;
    int16_t pitch = seq_scale_quantize(root, key, kind, scale->direction);
    int8_t degrees = (random / 100u) % (2u * v->range + 1u) - v->range;
    int8_t direction = degrees < 0 ? -1 : 1;
    for (uint8_t n = 0; n < abs(degrees); n++) {
        int16_t next = pitch;
        do { next += direction; } while (next >= 0 && next <= 127 && !seq_scale_contains(next, key, kind));
        if (next < 0 || next > 127) break;
        pitch = next;
    }
    return pitch - root;
}
static int16_t seq_variation_bound(int16_t offset, uint8_t low, uint8_t high) {
    if (offset < -(int16_t)low) offset = -(int16_t)low;
    if (offset > 127 - high) offset = 127 - high;
    return offset;
}
static void seq_variation_voice(uint8_t *notes, uint8_t count, uint8_t amount) {
    /* Raise the current lowest voice; never clip or collapse a chord. */
    for (uint8_t n = 0; n < amount && count > 1; n++) {
        uint8_t lowest = 0;
        for (uint8_t i = 1; i < count; i++) if (notes[i] < notes[lowest]) lowest = i;
        if (notes[lowest] > 115) break;
        uint8_t next = notes[lowest] + 12;
        bool duplicate = false;
        for (uint8_t i = 0; i < count; i++) if (i != lowest && notes[i] == next) duplicate = true;
        if (duplicate) break;
        notes[lowest] = next;
    }
}
static uint8_t seq_variation_groups(uint8_t track, uint8_t *groups) {
    uint8_t count = 0;
    uint16_t last = 0;
    mel_event_snapshot_t event, other;
    for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
        mel_track_event_at(track, i, &event);
        if (!count || event.start_step > last) { groups[count++] = i; last = event.start_step; continue; }
        if (event.start_step == last) continue;
        uint8_t at = 0;
        while (at < count) {
            mel_track_event_at(track, groups[at], &other);
            if (other.start_step >= event.start_step) break;
            at++;
        }
        if (at < count && other.start_step == event.start_step) continue;
        memmove(groups + at + 1, groups + at, count - at);
        groups[at] = i; count++;
    }
    return count;
}
static void seq_variation_group(uint8_t track, uint16_t step, uint16_t source, uint16_t destination, uint16_t end, uint8_t cache, uint8_t velocity, uint32_t random,
                                bool euclidean, bool first, bool project, uint8_t *output, uint8_t *attacks) {
    uint8_t notes[128], velocities[128], count = 0, low = 127, high = 0;
    uint8_t active[16] = {0};
    bool starts = first || euclidean || step == destination;
    mel_event_snapshot_t event;
    for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
        mel_track_event_at(track, i, &event);
        if (event.start_step != source) continue;
        notes[count] = event.pitch; velocities[count] = source == destination ? event.velocity : velocity;
        if (step <= (source == destination ? event.end_step : end)) active[count >> 3] |= 1u << (count & 7);
        count++;
        if (event.pitch < low) low = event.pitch;
        if (event.pitch > high) high = event.pitch;
    }
    if (!count) return;
    int16_t offset = 0;
    if (euclidean && euclid_tracks[track].pitch_mode == 1) {
        int8_t pitch_offset; seq_euclid_step_hit(track, step, NULL, &pitch_offset); offset = pitch_offset;
    }
    if (!project) {
        if (starts || seq_accumulator_active_notes[track][cache] == 255) {
            int16_t next = seq_accumulator_next(track);
            seq_accumulator_active_notes[track][cache] = (next < -127 ? -127 : next) + 127;
        }
        offset += (int16_t)seq_accumulator_active_notes[track][cache] - 127;
    }
    offset += seq_variation_mutation(track, low, random);
    offset = seq_variation_bound(offset, low, high);
    /* Fixed chord qualities survive global quantization. Single notes retain
     * ordinary scale behavior. A scale mutation moves the whole chord root. */
    for (uint8_t i = 0; i < count; i++) notes[i] = count == 1 ? seq_scale_apply(track, notes[i] + offset) : notes[i] + offset;
    const seq_variation_t *v = &seq_variations[track];
    if (v->enabled) seq_variation_voice(notes, count, v->voicing == 4 ? random % count : v->voicing);
    uint16_t hit = 0; if (euclidean) seq_euclid_step_hit(track, step, &hit, NULL);
    for (uint8_t i = 0; i < count; i++) {
        if (!(active[i >> 3] & (1u << (i & 7)))) continue;
        uint8_t pitch = notes[i], velocity = velocities[i];
        if (euclidean) velocity = seq_velocity_apply_swing(velocity, euclid_tracks[track].swing_amount, hit);
        if (velocity > output[pitch]) output[pitch] = velocity;
        if (starts) attacks[pitch >> 3] |= 1u << (pitch & 7);
    }
}
static void seq_variation_frame(uint8_t track, uint16_t step, bool first, bool project, uint8_t *output, uint8_t *attacks) {
    const seq_variation_t *v = &seq_variations[track];
    bool euclidean = seq_modules[track].kind == SEQ_MODULE_EUCLID && euclid_tracks[track].enabled;
    uint16_t hit = 0; int8_t pitch_offset = 0;
    if (euclidean && (!seq_euclid_step_hit(track, step, &hit, &pitch_offset) || (!project && !seq_euclid_probability_allows(track)))) return;
    uint8_t groups[128], count = seq_variation_groups(track, groups);
    uint32_t seed = seq_variation_seed(track);
    bool has_source = false;
    for (uint8_t g = 0; g < count; g++) {
        mel_event_snapshot_t target, event;
        mel_track_event_at(track, groups[g], &target);
        if (step < target.start_step) break;
        uint16_t end = target.end_step;
        for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
            mel_track_event_at(track, i, &event);
            if (event.start_step == target.start_step && event.end_step > end) end = event.end_step;
        }
        if (step < target.start_step || step > end) continue;
        has_source = true;
        uint8_t source = seq_variation_select(v, count, g, seed);
        mel_track_event_at(track, groups[source], &event);
        seq_variation_group(track, step, event.start_step, target.start_step, end, g, target.velocity, seq_variation_hash(seed ^ g), euclidean, first, project, output, attacks);
    }
    if (!euclidean || has_source) return;
    const seq_euclid_track_t *e = &euclid_tracks[track];
    if (!e->note_count) return;
    uint8_t slot = e->palette_pin < e->note_count ? e->palette_pin : seq_variation_select(v, e->note_count, hit, seed);
    uint8_t notes[SEQ_MELODIC_KEY_COUNT], voices = 0;
    if (e->pitch_mode == 2) voices = seq_palette_chord_notes(track, slot, notes);
    else {
        voices = seq_melodic_euclid_fallback(track, step, notes);
        pitch_offset = e->note_pool[slot];
        for (uint8_t i = 0; i < voices; i++) notes[i] = seq_melodic_euclid_pitch(track, notes[i], pitch_offset, false);
    }
    if (!voices) return;
    uint8_t low = 127, high = 0;
    for (uint8_t i = 0; i < voices; i++) { if (notes[i] < low) low = notes[i]; if (notes[i] > high) high = notes[i]; }
    uint32_t random = seq_variation_hash(seed ^ hit);
    int16_t offset = (project ? 0 : seq_accumulator_next(track)) + seq_variation_mutation(track, low, random);
    offset = seq_variation_bound(offset, low, high);
    for (uint8_t i = 0; i < voices; i++) notes[i] = voices == 1 && e->pitch_mode != 2 ? seq_scale_apply(track, notes[i] + offset) : notes[i] + offset;
    if (v->enabled) seq_variation_voice(notes, voices, v->voicing == 4 ? random % voices : v->voicing);
    uint8_t velocity = seq_velocity_apply_swing(MEL_DEFAULT_VELOCITY, e->swing_amount, hit);
    for (uint8_t i = 0; i < voices; i++) { output[notes[i]] = velocity; attacks[notes[i] >> 3] |= 1u << (notes[i] & 7); }
}
static void seq_variation_emit(uint8_t track, uint32_t now, bool first) {
    uint8_t output[128] = {0}, attacks[16] = {0};
    if (!melodic_track_muted[track]) seq_variation_frame(track, seq_track_step_position[track], first, false, output, attacks);
    for (uint8_t pitch = 0; pitch < 128; pitch++) {
        bool active = melodic_active_contains(track, pitch), attack = attacks[pitch >> 3] & (1u << (pitch & 7));
        if (active && (!output[pitch] || attack)) {
            melodic_active_remove(track, pitch);
            if (!seq_module_preview_owns(track, pitch)) virtual_midi_note_off(midi_channel_for_track(track), pitch, now);
        }
        if (output[pitch] && (!active || attack)) {
            uint8_t velocity = (uint16_t)output[pitch] * melodic_track_velocity[track] / 127u;
            if (!seq_module_preview_owns(track, pitch)) virtual_midi_note_on(midi_channel_for_track(track), pitch, velocity ? velocity : 1, now);
            melodic_active_add(track, pitch);
            mel_last_start_mask |= 1u << track;
        }
    }
}
static void seq_variation_project(uint8_t track, uint16_t step, uint8_t *notes) {
    uint8_t output[128] = {0}, attacks[16] = {0};
    seq_variation_frame(track, step, false, true, output, attacks);
    for (uint8_t i = 0; i < 128; i++) if (output[i]) notes[i >> 3] |= 1u << (i & 7);
}
static void seq_variation_apply_mask(uint16_t mask, uint32_t now) {
    mask &= seq_variation_pending;
    if (!mask) return;
    for (uint8_t t = 0; t < 12; t++) if (mask & (1u << t)) {
        seq_midi_stop_melodic_track(t, now);
        seq_variations[t] = seq_variation_queued[t];
    }
    seq_variation_pending &= ~mask;
    seq_variation_revision++;
    seq_variation_rejoin |= mask;
    seq_mark_dirty();
}

static void seq_variation_apply_pending(uint32_t now) { seq_variation_apply_mask(seq_variation_pending, now); }
