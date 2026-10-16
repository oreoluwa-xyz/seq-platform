#if defined(EECONFIG_SIZE)
_Static_assert(EECONFIG_SIZE <= WEAR_LEVELING_LOGICAL_SIZE, "EEPROM user block overlaps firmware settings");
#endif
/* Included by sequencer.c so the codec can access private project state.
 * All integers have an explicit little-endian representation; no C structs or
 * runtime pointers are persisted. Decode is validated before changing state. */
#define SEQ_PROJECT_VERSION 1
#define SEQ_TRANSFER_CAPACITY 6144
#define SEQ_TEMP_CAPACITY 7424
static bool seq_temp_active, seq_temp_enter_held, seq_temp_escape_held;
static uint16_t seq_temp_length;
static bool seq_temp_restore_pending;
/* Save/load and USB transfers are mutually exclusive, so share scratch RAM. */
typedef struct {
    seq_chord_event_t drafts[12];
    seq_euclid_track_t euclid[12];
    seq_accumulator_state_t accumulators[12];
    seq_variation_t queued[12];
    uint16_t variation_pending;
    uint16_t picked[12];
    uint8_t last_semitone[12], drum_note[12];
    uint8_t slot, child_slot, save_status;
    uint16_t bar;
    bool child, dirty;
} seq_temp_context_t;
static union {
    seq_temp_context_t temp;
    uint8_t transfer[SEQ_TEMP_CAPACITY];
    uint8_t saved[EECONFIG_USER_DATA_SIZE];
} seq_project_scratch;
#define seq_transfer_buffer seq_project_scratch.transfer
#define seq_saved_buffer seq_project_scratch.saved
static uint16_t seq_harmony_source_size;
static uint8_t seq_harmony_source_object;
static uint8_t  seq_save_status; /* 0 saved, 1 dirty, 2 too large, 3 invalid */
static bool seq_saved_valid;
static bool     seq_transfer_active;
static uint32_t seq_transfer_deadline;
static uint16_t seq_transfer_length, seq_transfer_received;
static uint8_t  seq_transfer_object;
static uint16_t seq_codec_lanes, seq_codec_cc;

typedef struct {
    uint8_t *data;
    uint16_t size, pos;
    bool     read, apply, ok;
} seq_codec_t;
static uint8_t seq_codec_u8(seq_codec_t *c, uint8_t value) {
    if (c->pos >= c->size) {
        c->ok = false;
        return 0;
    }
    if (c->read) return c->data[c->pos++];
    if (c->data) c->data[c->pos] = value;
    c->pos++;
    return value;
}
static uint16_t seq_codec_u16(seq_codec_t *c, uint16_t value) {
    uint16_t lo = seq_codec_u8(c, value & 255u);
    return lo | ((uint16_t)seq_codec_u8(c, value >> 8) << 8);
}
#define PROJECT_FIELD(field, maximum)                         \
    do {                                                      \
        uint16_t value = seq_codec_u16(c, (uint16_t)(field)); \
        if (value > (maximum)) c->ok = false;                 \
        if (c->apply && c->ok) (field) = value;               \
    } while (0)
#define PROJECT_BYTE(field, maximum)                       \
    do {                                                   \
        uint8_t value = seq_codec_u8(c, (uint8_t)(field)); \
        if (value > (maximum)) c->ok = false;              \
        if (c->apply && c->ok) (field) = value;            \
    } while (0)
static void seq_project_global(seq_codec_t *c) {
    if (seq_codec_u8(c, SEQ_PROJECT_VERSION) != SEQ_PROJECT_VERSION) c->ok = false;
    uint16_t tempo = seq_codec_u16(c, bpm);
    if (tempo < 10 || tempo > 400) c->ok = false;
    if (c->apply && c->ok) bpm = tempo;
    PROJECT_BYTE(follow_mode, 1);
    PROJECT_BYTE(seq_config.default_track, 11);
    PROJECT_BYTE(current_layout, SEQ_LAYOUT_GRID_4X4);
    PROJECT_BYTE(seq_external_clock, 1);
    PROJECT_BYTE(seq_timing_swing, 50);
    for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++)
        PROJECT_BYTE(drum_note_map[n], 127);
    seq_scale_context_t scale = seq_scale_global;
    if ((!c->read && (scale.enabled || scale.root || scale.kind)) || (c->read && c->pos < c->size)) {
        if (seq_codec_u8(c, 'G') != 'G' || seq_codec_u8(c, 1) != 1) c->ok = false;
        scale.enabled = seq_codec_u8(c, scale.enabled); scale.root = seq_codec_u8(c, scale.root); scale.kind = seq_codec_u8(c, scale.kind);
        if (scale.enabled > 1 || scale.root > 11 || scale.kind >= SEQ_SCALE_COUNT) c->ok = false;
    } else if (c->read) scale = (seq_scale_context_t){0};
    if (c->read && c->apply && c->ok) { seq_scale_global = scale; seq_scale_revision++; }
}
static void seq_project_module(seq_codec_t *c, uint8_t track, bool chord_mode, bool legacy_track) {
    seq_module_t module;
    if (c->read) {
        uint16_t size = c->size - c->pos;
        bool migrated = !seq_module_decode(&module, c->data + c->pos, size);
        if (migrated && !(legacy_track && seq_module_migrate_bank(&module, c->data + c->pos, size))) {
            c->ok = false;
            return;
        }
        c->pos += size;
        if (chord_mode && (module.kind != SEQ_MODULE_EUCLID || !module.composed || module.rhythm.kind != 1)) {
            c->ok = false;
            return;
        }
        if (c->apply) {
            /* Stored-rhythm modules omit rhythm parameters. Track restores must
             * retain those serialized settings, including inactive generators. */
            seq_euclid_track_t rhythm = euclid_tracks[track];
            seq_module_install(track, &module);
            if (legacy_track && !migrated && module.composed && module.rhythm.kind == 1) {
                rhythm.pitch_mode = module.config.euclid.pitch_mode;
                euclid_tracks[track] = rhythm;
            }
        }
    } else {
        seq_module_export(track, &module);
        uint16_t size = c->size - c->pos;
        if (!seq_module_encode(&module, c->data ? c->data + c->pos : NULL, &size)) {
            c->ok = false;
            return;
        }
        c->pos += size;
    }
}
#include "seq_chord_codec.inc"
#include "seq_variation_codec.inc"
static void seq_project_euclid(seq_codec_t *c, seq_euclid_track_t *e) {
    PROJECT_BYTE(e->enabled, 1);
    PROJECT_BYTE(e->template_active, 1);
    PROJECT_FIELD(e->template_mask, UINT16_MAX);
    PROJECT_BYTE(e->palette_root, 11);
    uint16_t euclid_steps = seq_codec_u16(c, e->steps);
    uint16_t euclid_fills = seq_codec_u16(c, e->fills);
    if (!euclid_steps || euclid_steps > SEQ_MAX_STEPS || euclid_fills > euclid_steps) c->ok = false;
    if (c->apply) {
        e->steps = euclid_steps;
        e->fills = euclid_fills;
    }
    int16_t rotate = (int16_t)seq_codec_u16(c, (uint16_t)e->rotate);
    if (c->apply) e->rotate = rotate;
    PROJECT_BYTE(e->note_count, SEQ_EUCLID_NOTE_POOL_MAX);
    for (uint8_t i = 0; i < SEQ_EUCLID_NOTE_POOL_MAX; i++) {
        int8_t value = (int8_t)seq_codec_u8(c, (uint8_t)e->note_pool[i]);
        if (c->apply) e->note_pool[i] = value;
    }
    PROJECT_BYTE(e->swing_amount, 100);
    PROJECT_BYTE(e->probability, 100);
    PROJECT_BYTE(e->ratchet_threshold, 255);
    PROJECT_BYTE(e->palette_pin, 255);
}
/* Optional L/1 extension: lane mask followed by 26 bytes per configured lane.
 * Keeping the old track generator preserves routing in pre-lane projects. */
static void seq_project_lane_rhythms(seq_codec_t *c, uint8_t track, uint16_t stored_lanes) {
    uint16_t mask = 0;
    if (!c->read) for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
        const seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, n);
        if (lane && lane->euclid.steps) mask |= 1u << n;
    }
    if ((!c->read && !mask) || (c->read && (c->pos >= c->size || c->data[c->pos] != 'L'))) return;
    seq_codec_u8(c, 'L');
    if (seq_codec_u8(c, 1) != 1) { c->ok = false; return; }
    mask = seq_codec_u16(c, mask);
    if (!mask || mask >> SEQ_DRUM_NOTE_COUNT) { c->ok = false; return; }
    for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
        if (!(mask & (1u << n))) continue;
        seq_euclid_track_t rhythm = {0};
        if (!c->read) rhythm = seq_drum_pool_lane(track, n)->euclid;
        seq_project_euclid(c, &rhythm);
        PROJECT_BYTE(rhythm.pitch_mode, 1);
        PROJECT_BYTE(rhythm.palette_authored, 1);
        /* Per-lane rhythms always target that lane, never the legacy palette. */
        if (c->read) {
            uint16_t start = c->pos - 26;
            if (!c->ok || c->pos < 26) { c->ok = false; return; }
            uint16_t steps = c->data[start + 5] | ((uint16_t)c->data[start + 6] << 8);
            uint16_t rotation = c->data[start + 9] | ((uint16_t)c->data[start + 10] << 8);
            if (c->data[start + 11] != 1 || c->data[start + 12] != n || rotation >= steps) c->ok = false;
        }
        if (!(stored_lanes & (1u << n))) seq_codec_lanes++;
        if (c->apply && c->ok) {
            if (!seq_drum_pool_reserve(track, 1u << n)) { c->ok = false; return; }
            seq_drum_pool_lane(track, n)->euclid = rhythm;
        }
    }
}
static void seq_project_track(seq_codec_t *c, uint8_t t) {
    /* Reuse idle rollback RAM rather than placing 768 bytes on the 2 KiB
     * keyboard-task stack. Reject before touching an in-progress edit. */
    mel_event_snapshot_t *snapshot = mel_track_snapshot_scratch();
    if (!snapshot) {
        c->ok = false;
        return;
    }
    uint16_t track_start = c->pos;
    uint16_t length = seq_codec_u16(c, seq_track_length_steps(t));
    if (!length || length > SEQ_MAX_STEPS) c->ok = false;
    if (c->apply && c->ok) {
        track_length_steps[t]      = length;
        track_length_bars[t]       = (length + 15u) / 16u;
        seq_track_step_position[t] = 0;
        seq_drum_pool_clear_track(t);
        mel_track_reset(t);
        seq_automation_clear(t);
    }
    PROJECT_BYTE(seq_slots[t].kind, SEQ_SLOT_KIND_MELODIC);
    PROJECT_BYTE(seq_slots[t].wraps_drum, 1);
    if (c->apply) {
        seq_slots[t].drum_track    = t;
        seq_slots[t].melodic_track = t;
    }
    PROJECT_BYTE(track_midi_channel[t], 15);
    PROJECT_BYTE(drum_track_velocity[t], 127);
    PROJECT_BYTE(melodic_track_velocity[t], 127);
    PROJECT_BYTE(melodic_track_muted[t], 1);
    int8_t octave = (int8_t)seq_codec_u8(c, (uint8_t)melodic_octave_shift[t]);
    if (octave < -5 || octave > 5) c->ok = false;
    if (c->apply) melodic_octave_shift[t] = octave;
    seq_euclid_track_t *e = &euclid_tracks[t];
    seq_project_euclid(c, e);
    seq_accumulator_state_t *a = &seq_accumulators[t];
    PROJECT_BYTE(a->enabled, 1);
    PROJECT_BYTE(a->random, 1);
    PROJECT_BYTE(a->run_length, 255);
    int8_t direction = (int8_t)seq_codec_u8(c, (uint8_t)a->direction);
    if (direction < -1 || direction > 1) c->ok = false;
    if (c->apply) {
        a->direction = direction;
        seq_accumulator_reset_runtime(t);
    }
    for (uint8_t i = 0; i < SEQ_CC_MAPPING_COUNT; i++) {
        uint8_t stable = cc_values[t][i];
        if (cc_mode.latched_temporary && cc_mode.snapshot_track == t)
            stable = cc_snapshot[t][i];
        else if (cc_session.active && cc_session.temporary && cc_session.track == t && cc_session.mapping_index == i)
            stable = cc_session.prev_value;
        uint8_t value = seq_codec_u8(c, stable);
        if (value > 127) c->ok = false;
        if (c->apply && c->ok) cc_values[t][i] = value;
    }
    uint16_t stored_lanes = 0;
    for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
        PROJECT_BYTE(drum_lane_muted[t][n], 1);
        const seq_drum_lane_storage_t *lane  = seq_drum_pool_lane(t, n);
        uint16_t                       bytes = 0;
        if (!c->read && lane)
            for (uint16_t i = 0; i < SEQ_DRUM_STORAGE_BYTES; i++)
                if (lane->data[i]) bytes = i + 1u;
        bytes = seq_codec_u16(c, bytes);
        if (bytes > SEQ_DRUM_STORAGE_BYTES) {
            c->ok = false;
            return;
        }
        bool allocated = bytes != 0;
        if (c->apply && allocated && !seq_drum_pool_reserve(t, 1u << n)) {
            c->ok = false;
            return;
        }
        for (uint16_t i = 0; i < bytes; i++) {
            uint8_t value = seq_codec_u8(c, lane ? lane->data[i] : 0);
            if (c->apply) seq_drum_pool_lane(t, n)->data[i] = value;
        }
        uint8_t mods = 0;
        if (!c->read && lane)
            for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++)
                mods += lane->mods[i].step != UINT16_MAX;
        mods = seq_codec_u8(c, mods);
        if (mods > SEQ_DRUM_STEP_MOD_CAP) {
            c->ok = false;
            return;
        }
        if (c->apply && mods && !seq_drum_pool_reserve(t, 1u << n)) {
            c->ok = false;
            return;
        }
        seq_codec_lanes += allocated || mods;
        if (allocated || mods) stored_lanes |= 1u << n;
        uint8_t source = 0;
        for (uint8_t i = 0; i < mods; i++) {
            seq_drum_step_mod_t mod = {0};
            if (!c->read) {
                while (lane->mods[source].step == UINT16_MAX)
                    source++;
                mod = lane->mods[source++];
            }
            mod.step    = seq_codec_u16(c, mod.step);
            mod.config  = seq_codec_u8(c, mod.config);
            mod.spacing = seq_codec_u8(c, mod.spacing);
            if (mod.step >= SEQ_MAX_STEPS || mod.config > 15 || mod.spacing >= SEQ_DRUM_RATCHET_SPACING_STEPS) c->ok = false;
            if (c->apply && c->ok) seq_drum_pool_lane(t, n)->mods[i] = mod;
        }
    }
    uint8_t notes = seq_codec_u8(c, mel_track_event_count(t));
    if (notes > MEL_TRACK_MAX_EVENTS) {
        c->ok = false;
        return;
    }
    for (uint8_t i = 0; i < notes; i++) {
        mel_event_snapshot_t ev = {0};
        if (!c->read) mel_track_event_at(t, i, &ev);
        ev.start_step = seq_codec_u16(c, ev.start_step);
        ev.end_step   = seq_codec_u16(c, ev.end_step);
        ev.pitch      = seq_codec_u8(c, ev.pitch);
        ev.velocity   = seq_codec_u8(c, ev.velocity);
        if (ev.start_step > ev.end_step || ev.end_step >= SEQ_MAX_STEPS || ev.pitch > 127 || !ev.velocity || ev.velocity > 127) c->ok = false;
        snapshot[i] = ev;
    }
    if (c->apply && c->ok && !mel_track_restore(t, snapshot, notes)) c->ok = false;
    uint16_t events = seq_codec_u16(c, seq_automation_track_count(t));
    if (events > SEQ_CC_TRACK_EVENT_CAPACITY) {
        c->ok = false;
        return;
    }
    seq_codec_cc += events;
    uint16_t source = 0;
    for (uint16_t i = 0; i < events; i++) {
        seq_cc_event_t ev = {.track = t};
        if (!c->read) {
            while (seq_automation_event(source)->track != t)
                source++;
            ev = *seq_automation_event(source++);
        }
        ev.step  = seq_codec_u16(c, ev.step);
        ev.index = seq_codec_u8(c, ev.index);
        ev.value = seq_codec_u8(c, ev.value);
        if (ev.step >= SEQ_MAX_STEPS || ev.index >= SEQ_CC_MAPPING_COUNT || ev.value > 127) c->ok = false;
        if (c->apply && c->ok && !seq_automation_record(t, ev.index, ev.step, ev.value)) c->ok = false;
    }
    bool manual = false;
    if ((!c->read && cc_automation_manual[t]) ||
        (c->read && c->pos < c->size && c->data[c->pos] == 'A')) {
        seq_codec_u8(c, 'A');
        uint8_t version = seq_codec_u8(c, 1);
        uint8_t mode = seq_codec_u8(c, cc_automation_manual[t] ? 1 : 0);
        if (version != 1 || mode > 1) c->ok = false;
        manual = mode == 1;
    }
    if (c->read && c->apply && c->ok) cc_automation_manual[t] = manual;
    seq_project_lane_rhythms(c, t, stored_lanes);
    /* Only untouched starter palettes need provenance. Older saves and explicit
     * module imports are considered authored, even if they contain only C. */
    seq_scale_track_t scale = seq_scale_tracks[t];
    if ((!c->read && (scale.mode || scale.root || scale.kind || scale.direction)) ||
        (c->read && c->pos < c->size && c->data[c->pos] == 'S')) {
        seq_codec_u8(c, 'S');
        if (seq_codec_u8(c, 1) != 1) c->ok = false;
        scale.mode = seq_codec_u8(c, scale.mode); scale.root = seq_codec_u8(c, scale.root);
        scale.kind = seq_codec_u8(c, scale.kind); scale.direction = seq_codec_u8(c, scale.direction);
        if (scale.mode > 2 || scale.root > 11 || scale.kind >= SEQ_SCALE_COUNT || scale.direction > 2) c->ok = false;
        if (c->read && c->pos >= c->size) c->ok = false;
    } else if (c->read) scale = (seq_scale_track_t){0};
    if (c->read && c->apply && c->ok) { seq_scale_tracks[t] = scale; seq_scale_revision++; }
    bool authored = true;
    if (c->ok && ((!c->read && !euclid_tracks[t].palette_authored) ||
                  (c->read && c->pos < c->size && c->data[c->pos] == 'P'))) {
        seq_codec_u8(c, 'P');
        uint8_t version = seq_codec_u8(c, 1);
        uint8_t flags = seq_codec_u8(c, 0);
        if (version != 1 || flags != 0) c->ok = false;
        authored = false;
        if (c->read && c->pos >= c->size) c->ok = false;
    }
    bool chord_mode = false;
    seq_chord_codec(c,t,snapshot,notes,length,&chord_mode);
    if (chord_mode && c->read && (c->pos >= c->size || c->data[track_start + 2] != SEQ_SLOT_KIND_MELODIC || c->data[track_start + 3])) c->ok = false;
    seq_project_variation(c, t);
    /* Older project objects have no module extension and retain Euclid. */
    if (c->ok && (!c->read || c->pos < c->size))
        seq_project_module(c, t, chord_mode, true);
    else if (c->ok && c->read && c->apply) {
        seq_module_default(&seq_modules[t], SEQ_MODULE_EUCLID);
        euclid_tracks[t].pitch_mode = 1; /* Pre-module projects used offsets. */
    }
    if (c->ok && c->read && c->apply) euclid_tracks[t].palette_authored = authored;
}
#undef PROJECT_FIELD
#undef PROJECT_BYTE

static uint32_t seq_project_checksum(const uint8_t *data, uint16_t size) {
    uint32_t hash = 2166136261u;
    for (uint16_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}
static bool seq_harmony_source(seq_codec_t *c, uint8_t slot);
static bool seq_project_object(uint8_t object, uint8_t *data, uint16_t *size, bool read, bool apply) {
    if (!read && data) seq_chord_prune();
    seq_codec_t codec = {.data = data, .size = *size, .read = read, .apply = apply, .ok = true};
    seq_codec_chords = 0;
    seq_codec_lanes = seq_codec_cc = 0;
    if (!object)
        seq_project_global(&codec);
    else if (object <= 12)
        seq_project_track(&codec, object - 1u);
    else if (object <= 24)
        seq_project_module(&codec, object - 13u, false, false);
    else if (object <= 36 && !read) {
        if (!seq_harmony_source(&codec, object - 25)) return false;
    } else
        return false;
    if (read && codec.pos != *size) codec.ok = false;
    *size = codec.pos;
    return codec.ok;
}
static bool seq_project_restore_object(uint8_t object, uint8_t *data, uint16_t size) {
    uint16_t count = size;
    if (!seq_project_object(object, data, &count, true, false)) return false;
    if (object && object <= 12) {
        uint8_t t = object - 1, existing = 0;
        uint16_t used_chords=0;for(uint8_t n=0;n<12;n++)if(n!=t)used_chords+=seq_chord_count(n);
        if(used_chords+seq_codec_chords>SEQ_CHORD_EVENT_CAPACITY)return false;
        for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++)
            existing += seq_drum_pool_lane(t, n) != NULL;
        if (seq_codec_lanes + seq_drum_pool_used() - existing > SEQ_DRUM_LANE_POOL_CAPACITY || seq_codec_cc + seq_automation_count() - seq_automation_track_count(t) > SEQ_CC_EVENT_CAPACITY) return false;
    }
    seq_midi_all_notes_off(timer_read32());
    seq_ratchet_clear();
    bool ok = seq_project_object(object, data, &size, true, true);
    if (ok && object >= 13 && object <= 24) seq_chord_mode_set(object - 13, false);
    seq_update_total_steps();
    seq_clamp_view_bar();
    seq_apply_layout(current_layout);
    seq_mark_dirty();
    return ok;
}
static bool seq_project_saved_decode(bool apply) {
    uint16_t length = seq_saved_buffer[4] | ((uint16_t)seq_saved_buffer[5] << 8);
    if (memcmp(seq_saved_buffer, "SEQ1", 4) || length < 10 || length > EECONFIG_USER_DATA_SIZE) return false;
    uint32_t checksum = 0;
    for (uint8_t i = 0; i < 4; i++)
        checksum |= (uint32_t)seq_saved_buffer[6 + i] << (i * 8);
    if (seq_project_checksum(seq_saved_buffer + 10, length - 10) != checksum) return false;
    if(apply)memset(seq_chord_events,255,sizeof(seq_chord_events));
    uint16_t pos = 10, lanes = 0, events = 0, chords = 0;
    for (uint8_t object = 0; object <= 12; object++) {
        if (pos + 2 > length) return false;
        uint16_t size = seq_saved_buffer[pos] | ((uint16_t)seq_saved_buffer[pos + 1] << 8);
        pos += 2;
        if (size > length - pos) return false;
        uint16_t used = size;
        if (!seq_project_object(object, seq_saved_buffer + pos, &used, true, apply)) return false;
        chords += seq_codec_chords;if(chords>SEQ_CHORD_EVENT_CAPACITY)return false;
        lanes += seq_codec_lanes;
        events += seq_codec_cc;
        if (lanes > SEQ_DRUM_LANE_POOL_CAPACITY || events > SEQ_CC_EVENT_CAPACITY) return false;
        pos += size;
    }
    return pos == length;
}
static void seq_storage_save_if_dirty(void) {
    if (!seq_dirty || seq_transfer_active || seq_temp_active || seq_state.running) return;
    seq_flush_pending_erase();
    uint16_t pos = 10;
    for (uint8_t object = 0; object <= 12; object++) {
        uint16_t size = pos + 2u < EECONFIG_USER_DATA_SIZE ? EECONFIG_USER_DATA_SIZE - pos - 2u : 0;
        if (!size || !seq_project_object(object, seq_saved_buffer + pos + 2u, &size, false, false)) {
            seq_save_status = 2;
            seq_drum_pool_report_full();
            return;
        }
        seq_saved_buffer[pos++] = size & 255u;
        seq_saved_buffer[pos++] = size >> 8;
        pos += size;
    }
    memcpy(seq_saved_buffer, "SEQ1", 4);
    seq_saved_buffer[4] = pos & 255u;
    seq_saved_buffer[5] = pos >> 8;
    uint32_t hash       = seq_project_checksum(seq_saved_buffer + 10, pos - 10);
    for (uint8_t i = 0; i < 4; i++)
        seq_saved_buffer[6 + i] = hash >> (i * 8);
    memset(seq_saved_buffer + pos, 0, EECONFIG_USER_DATA_SIZE - pos);
    eeconfig_update_user_datablock(seq_saved_buffer);
    seq_dirty       = false;
    seq_save_status = 0;
    seq_saved_valid = true;
}
void eeconfig_init_user_datablock(void) {
    /* QMK's weak default uses a full user-block-sized automatic array. */
    memset(seq_saved_buffer, 0, sizeof(seq_saved_buffer));
    eeconfig_update_user_datablock(seq_saved_buffer);
    seq_saved_valid = false;
}
static void seq_storage_load(void) {
    seq_saved_valid = false;
    if (!eeconfig_is_user_datablock_valid()) return;
    eeconfig_read_user_datablock(seq_saved_buffer);
    if (!seq_project_saved_decode(false)) {
        seq_save_status = 3;
        return;
    }
    seq_loading = true;
    seq_drum_pool_init();
    seq_automation_init();
    seq_project_saved_decode(true);
    seq_loading     = false;
    seq_dirty       = false;
    seq_save_status = 0;
    seq_saved_valid = true;
    seq_update_total_steps();
    seq_clamp_view_bar();
    seq_apply_layout(current_layout);
}

#include "seq_live.inc"
#include "seq_visual.inc"
#include "seq_rhythm_edit.inc"
#include "seq_project_health.inc"
#include "seq_variation_control.inc"
static void seq_scale_request(uint8_t track, uint16_t context, const uint8_t *data, uint8_t size);

/* USB MIDI SysEx commands: 1 read, 2 begin write, 3 append, 4 commit,
 * 5 validate, 6 clear project, 7 finish, 8 save, 9 status.
 * Transfers stop transport and temporarily lock editing for a consistent image. */
void seq_backup_request(uint8_t command, uint8_t object, uint16_t offset, const uint8_t *data, uint8_t size) {
    uint8_t status     = 0;
    uint8_t reply[36]  = {0};
    uint8_t reply_size = 0;
    if (command == 26 || command == 27) { seq_variation_request(command, object, offset, data, size); return; }
    if (command == 25) {
        uint8_t health[SEQ_PROJECT_HEALTH_SIZE];
        if (object >= SEQ_TRACK_COUNT || offset || size) status = 3;
        else seq_project_health(object, health);
        virtual_midi_backup_reply(command, object, offset, status, health, status ? 0 : sizeof(health));
        return;
    }
    if (seq_temp_active && command >= 1 && command <= 9) {
        virtual_midi_backup_reply(command, object, offset, 2, NULL, 0);
        return;
    }
    if (command == 24) {
        if (object || size || (offset != 0 && offset != 64)) {
            virtual_midi_backup_reply(command, object, offset, 3, NULL, 0);
        } else {
            const uint8_t *identity = (const uint8_t *)&seq_firmware_identity;
            uint8_t length = offset ? sizeof(seq_firmware_identity) - 64 : 64;
            virtual_midi_backup_reply(command, object, offset, 0, identity + offset, length);
        }
        return;
    }
    if (command == 13 || command == 14 || command == 16 || command == 17 || command == 21 || command == 22) {
        seq_visual_request(command, object, offset, size);
        return;
    }
    if (command == 23) { seq_chord_request(object,offset,data,size);return; }
    if (command == 20) { seq_scale_request(object, offset, data, size); return; }
    if (command == 19) {
        seq_rhythm_edit_request(object, offset, data, size);
        return;
    }
    if (command == 18) {
        /* Remove includes the displayed palette to reject stale slot indices. */
        if (object >= 12 || !data || !size || data[0] > 4 ||
            (data[0] == 4 ? size != 21 || offset || data[1] > 8 || data[2] > 8 || (data[20] > 11 && data[20] != 16) :
             data[0] == 3 ? size != 12 || offset || data[1] >= SEQ_EUCLID_NOTE_POOL_MAX || data[2] > SEQ_EUCLID_NOTE_POOL_MAX :
             data[0] != 1 ? size != 2 || data[1] > (data[0] == 0 ? 2 : 1) || offset : size != 1 || offset >= seq_track_length_steps(object))) {
            status = 3;
        } else if (object != view_slot || !slot_uses_melodic(object) || seq_modules[object].kind != SEQ_MODULE_EUCLID ||
                   seq_transfer_active || seq_palette_digit_held || seq_palette_type_held || record_state.armed || step_edit.active || chord_capture.active || euclid_palette_capture.active || melodic_key_mask[object] ||
                   (data[0] == 1 && (euclid_tracks[object].pitch_mode == 1 || !euclid_tracks[object].enabled))) {
            status = 2;
        } else if (data[0] == 4 && (euclid_tracks[object].pitch_mode != 2 || data[2] != euclid_tracks[object].note_count || data[3] != euclid_tracks[object].palette_root ||
                   (data[1] != 8 && data[1] >= data[2]) || memcmp(data + 4, euclid_tracks[object].note_pool, data[2]) ||
                   memcmp(data + 12, seq_modules[object].config.euclid.qualities, 8) || ((data[20] == 10 || data[20] == 11) && !seq_chord_scale_enabled(object)))) {
            status = 2;
        } else if (data[0] == 4) {
            seq_palette_set_quality(object, data[1] == 8 ? 0 : data[1] + 1, data[20]);
        } else if (data[0] == 3 && (data[2] != euclid_tracks[object].note_count || data[3] != euclid_tracks[object].palette_root ||
                   data[1] >= data[2] || memcmp(data + 4, euclid_tracks[object].note_pool, data[2]))) {
            status = 2;
        } else if (data[0] == 3) {
            seq_euclid_palette_remove(object, data[1], timer_read32());
        } else if (data[0] == 1 && !mel_track_remove_step_all(slot_melodic_track(object), offset)) {
            status = 4; /* Splitting a sustained event can exhaust event capacity. */
        } else if (data[0] == 2) {
            seq_palette_replace[object] = data[1];
        } else {
            seq_midi_stop_melodic_track(slot_melodic_track(object), timer_read32());
            if (!data[0]) euclid_tracks[object].pitch_mode = data[1];
            seq_accumulator_reset_runtime(object);
            seq_mark_dirty();
        }
        virtual_midi_backup_reply(command, object, offset, status, NULL, 0);
        return;
    }
    if (command == 11 || command == 12) {
        uint8_t live[SEQ_LIVE_SIZE];
        if (size || offset || (command == 11 ? object != 0 : object >= 12)) {
            status = 3;
        } else if (command == 12 && !seq_live_can_select()) {
            status = 2;
        } else {
            if (command == 12) seq_select_root_track(object);
            seq_live_snapshot(live);
        }
        virtual_midi_backup_reply(command, object, offset, status, live, status ? 0 : sizeof(live));
        return;
    }
    if (command == 15) {
        /* Catalog v1, module v2, two stages, eight voices, max wire bytes,
         * followed by (component ID, contract version) pairs. */
        const uint8_t catalog[] = {1, 4, 2, SEQ_CHORD_VOICES, SEQ_MODULE_WIRE_MAX, 0, 1, 1, 2, 1, 3, 1, 3, 2, 3, 3};
        virtual_midi_backup_reply(command, object, offset, (object || offset || size) ? 3 : 0, catalog, (object || offset || size) ? 0 : sizeof(catalog));
        return;
    }
    if (command == 10) {
        uint8_t  capabilities[21] = {SEQ_MODULE_VERSION, SEQ_TRACK_COUNT, 0, SEQ_CHORD_VOICES, SEQ_MODULE_WIRE_MAX, 0, 1};
        uint16_t melodic_mask     = 0;
        for (uint8_t i = 0; i < 12; i++) {
            capabilities[7 + i] = seq_modules[i].kind;
            if (slot_is_melodic(i)) melodic_mask |= 1u << i;
        }
        capabilities[19] = melodic_mask & 255u;
        capabilities[20] = melodic_mask >> 8;
        virtual_midi_backup_reply(command, object, offset, 0, capabilities, sizeof(capabilities));
        return;
    }
    if (command < 1 || command > 9 || (object > 24 && !(command == 1 && object <= 36)))
        status = 1;
    else {
        if (!seq_transfer_active) {
            seq_chord_cancel();
            seq_step_hold_finish(false);
            seq_flush_pending_erase();
            seq_transport_stop(timer_read32());
            seq_scale_editing = false; /* A project transfer cancels pending scale edits. */
            seq_transfer_active = true;
            seq_transfer_length = seq_transfer_received = 0;
            seq_harmony_source_size = 0;
        }
        seq_transfer_deadline = timer_read32() + 10000u;
        if (command != 1 || object <= 24) seq_harmony_source_size = 0;
        if (command == 1) {
            if (seq_transfer_length)
                status = 2;
            else {
                uint16_t length = SEQ_TRANSFER_CAPACITY;
                bool valid;
                if (object > 24 && offset) {
                    length = seq_harmony_source_size;
                    valid = length && object == seq_harmony_source_object;
                } else {
                    valid = seq_project_object(object, seq_transfer_buffer, &length, false, false);
                    if (object > 24) { seq_harmony_source_size = valid ? length : 0; seq_harmony_source_object = object; }
                }
                if (!valid || offset > length) status = 3;
                else {
                    reply[0]   = length & 255u;
                    reply[1]   = length >> 8;
                    reply_size = length - offset > 32u ? 32u : length - offset;
                    memcpy(reply + 2, seq_transfer_buffer + offset, reply_size);
                    reply_size += 2;
                }
            }
        } else if (command == 2) {
            if (!offset || offset > SEQ_TRANSFER_CAPACITY || size)
                status = 3;
            else {
                seq_transfer_length   = offset;
                seq_transfer_received = 0;
                seq_transfer_object   = object;
            }
        } else if (command == 3) {
            if (object != seq_transfer_object || !seq_transfer_length || offset != seq_transfer_received || offset + size > seq_transfer_length)
                status = 3;
            else {
                memcpy(seq_transfer_buffer + offset, data, size);
                seq_transfer_received += size;
            }
        } else if (command == 4 || command == 5) {
            uint32_t hash = 0;
            if (size == 4)
                for (uint8_t i = 0; i < 4; i++)
                    hash |= (uint32_t)data[i] << (i * 8);
            uint16_t length = seq_transfer_length;
            if (object != seq_transfer_object || !length || seq_transfer_received != length || size != 4 || hash != seq_project_checksum(seq_transfer_buffer, length))
                status = 3;
            else if (command == 5) {
                if (!seq_project_object(object, seq_transfer_buffer, &length, true, false))
                    status = 3;
                else {
                    reply[0]   = seq_codec_lanes;
                    reply[1]   = seq_codec_cc & 255u;
                    reply[2]   = seq_codec_cc >> 8;
                    reply_size = 3;
                    /* Offset 1 requests recipe preflight; preserve legacy replies. */
                    if (offset == 1) {
                        reply[3] = seq_codec_chords;
                        reply[4] = SEQ_CHORD_EVENT_CAPACITY;
                        reply_size = 5;
                    }
                }
            } else if (!seq_project_restore_object(object, seq_transfer_buffer, length))
                status = 3;
            seq_transfer_length = seq_transfer_received = 0;
            seq_harmony_source_size = 0;
        } else if (command == 6) {
            if (size != 4 || memcmp(data, "SEQ!", 4))
                status = 3;
            else {
                seq_drum_pool_init();
                mel_tracks_init();
                seq_automation_init();
                memset(seq_chord_events, 255, sizeof(seq_chord_events));
                seq_mark_dirty();
            }
        } else if (command == 7) {
            seq_transfer_active = false;
            seq_transfer_length = seq_transfer_received = 0;
            seq_storage_save_pending                    = false;
        } else if (command == 8) {
            seq_transfer_active = false;
            seq_transfer_length = seq_transfer_received = 0;
            seq_storage_save_if_dirty();
            reply[0]   = seq_save_status;
            reply_size = 1;
        } else {
            reply[0]   = seq_save_status;
            reply[1]   = seq_dirty;
            reply[2]   = seq_drum_pool_used();
            reply[3]   = SEQ_DRUM_LANE_POOL_CAPACITY;
            reply[4]   = SEQ_CC_EVENT_CAPACITY & 255u;
            reply[5]   = SEQ_CC_EVENT_CAPACITY >> 8;
            reply_size = 6;
        }
    }
    virtual_midi_backup_reply(command, object, offset, status, reply, reply_size);
}
