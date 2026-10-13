static bool seq_chord_scale_type(uint8_t type) {
    return type == 10 || type == 11 || (type >= 13 && type <= 15);
}
static bool seq_chord_scale_enabled(uint8_t track) {
    return seq_scale_tracks[track].mode == 1 || (seq_scale_tracks[track].mode == 0 && seq_scale_global.enabled);
}
static uint8_t seq_chord_voices(const seq_chord_event_t *c) {
    return c->type == SEQ_CHORD_CUSTOM ? c->custom_count : c->type == 13 ? 5 : c->type < 6 || c->type == 10 ? 3 : 4;
}
static uint8_t seq_chord_render(const seq_chord_event_t *c, uint8_t *notes) {
    static const uint8_t types[10][4] = {{0, 4, 7, 255}, {0, 3, 7, 255}, {0, 3, 6, 255}, {0, 4, 8, 255}, {0, 2, 7, 255}, {0, 5, 7, 255}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 3, 6, 10}};
    if (c->scale_free > 1 || c->quantize > 1 || c->quantize_root > 11 || c->quantize_kind >= SEQ_SCALE_COUNT || c->root > 127 || c->type >= SEQ_CHORD_TYPE_COUNT || c->scale_root > 11 || c->scale_kind >= SEQ_SCALE_COUNT || !c->velocity || c->velocity > 127) return 0;
    uint8_t count = 0;
    if (c->type == SEQ_CHORD_CUSTOM) {
        if (c->custom_count < 2 || c->custom_count > 8) return 0;
        bool ended = false, named = false;
        for (uint8_t i = 0; i < SEQ_CHORD_NAME_SIZE; i++) {
            if (c->name[i] > 32) named = true;
            if (!c->name[i])
                ended = true;
            else if (ended || c->name[i] < 32 || c->name[i] > 126)
                return 0;
        }
        if (!named) return 0;
        for (uint8_t i = 0; i < c->custom_count; i++) {
            int16_t note = c->root + c->intervals[i];
            if (note < 0 || note > 127 || (i && c->intervals[i] <= c->intervals[i - 1])) return 0;
            notes[count++] = note;
        }
    } else if (c->type == 16) {
        if (c->root > 118) return 0;
        for (uint8_t i = 0; i < 4; i++) notes[count++] = c->root + i * 3;
    } else if (c->type < 10) {
        for (uint8_t i = 0; i < 4 && types[c->type][i] != 255; i++) {
            uint16_t note = c->root + types[c->type][i];
            if (note > 127) return 0;
            notes[count++] = note;
        }
    } else {
        if (!seq_scale_contains(c->root, c->scale_root, c->scale_kind)) return 0;
        const uint8_t stacked[] = {0, 2, 4, 6, 8};
        const uint8_t sixth[] = {0, 2, 4, 5};
        const uint8_t add_nine[] = {0, 2, 4, 8};
        const uint8_t *degrees = c->type == 14 ? sixth : c->type == 15 ? add_nine : stacked;
        uint8_t degree = 0, wanted = seq_chord_voices(c);
        for (uint16_t note = c->root; note < 128 && count < wanted; note++) {
            if (seq_scale_contains(note, c->scale_root, c->scale_kind)) {
                if (degree == degrees[count]) notes[count++] = note;
                degree++;
            }
        }
        if (count != wanted) return 0;
    }
    if (c->inversion >= count) return 0;
    for (uint8_t i = 0; i < c->inversion; i++) {
        if (notes[0] > 115) return 0;
        uint8_t raised = notes[0] + 12, j = 1;
        bool    occupied = true;
        while (occupied) {
            occupied = false;
            for (uint8_t k = 1; k < count; k++)
                if (notes[k] == raised) occupied = true;
            if (occupied) {
                if (raised > 115) return 0;
                raised += 12;
            }
        }
        while (j < count && notes[j] < raised) {
            notes[j - 1] = notes[j];
            j++;
        }
        if (j < count && notes[j] == raised) return 0;
        notes[j - 1] = raised;
    }
    if (c->quantize) {
        uint8_t unique = 0;
        for (uint8_t i = 0; i < count; i++) {
            uint8_t note = seq_scale_quantize(notes[i], c->quantize_root, c->quantize_kind, 0);
            if (!unique || notes[unique - 1] != note) notes[unique++] = note;
        }
        count = unique;
    }
    return count;
}
static uint8_t seq_chord_count(uint8_t track) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++)
        count += seq_chord_events[i].track == track;
    return count;
}
static void seq_chord_clear_meta(uint8_t track) {
    for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++)
        if (seq_chord_events[i].track == track) seq_chord_events[i].track = 255;
}
static int16_t seq_chord_find(uint8_t track, uint16_t step) {
    for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++) {
        const seq_chord_event_t *c = &seq_chord_events[i];
        if (c->track == track && step >= c->start && step < c->start + c->length) return i;
    }
    return -1;
}
static bool seq_chord_matches(const seq_chord_event_t *c, const mel_event_snapshot_t *events, uint8_t size) {
    uint8_t pitches[8], count = seq_chord_render(c, pitches), seen = 0;
    if (!count || !c->length) return false;
    for (uint8_t i = 0; i < size; i++) {
        const mel_event_snapshot_t *e = &events[i];
        if (e->end_step < c->start || e->start_step >= c->start + c->length) continue;
        if (e->start_step != c->start || e->end_step != c->start + c->length - 1 || e->velocity != c->velocity) return false;
        uint8_t j = 0;
        while (j < count && pitches[j] != e->pitch)
            j++;
        if (j == count || (seen & (1u << j))) return false;
        seen |= 1u << j;
    }
    return seen == ((1u << count) - 1);
}
static void seq_chord_prune(void) {
    if (seq_chord_mutating) return;
    for (uint8_t t = 0; t < 12; t++) {
        if (!seq_chord_count(t)) continue;
        mel_event_snapshot_t notes[MEL_TRACK_MAX_EVENTS];
        uint8_t              count = mel_track_event_count(t);
        for (uint8_t n = 0; n < count; n++)
            mel_track_event_at(t, n, &notes[n]);
        for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++)
            if (seq_chord_events[i].track == t && !seq_chord_matches(&seq_chord_events[i], notes, count)) seq_chord_events[i].track = 255;
    }
}
static uint8_t seq_chord_root(uint8_t track, uint8_t note) {
    const seq_scale_track_t *t = &seq_scale_tracks[track];
    if (t->mode == 2 || (t->mode == 0 && !seq_scale_global.enabled)) return note;
    return seq_scale_quantize(note, t->mode == 1 ? t->root : seq_scale_global.root, t->mode == 1 ? t->kind : seq_scale_global.kind, t->direction);
}
static void seq_chord_context(uint8_t track, seq_chord_event_t *c) {
    const seq_scale_track_t *t = &seq_scale_tracks[track];
    c->scale_root              = t->mode == 1 ? t->root : seq_scale_global.root;
    c->scale_kind              = t->mode == 1 ? t->kind : seq_scale_global.kind;
    if (!c->scale_free) {
        c->root = seq_chord_root(track, c->root);
        if (seq_chord_scale_enabled(track)) c->quantize = 0;
        if (seq_chord_scale_enabled(track) && !seq_chord_scale_type(c->type)) {
            c->type = seq_chord_voices(c) >= 4 ? 11 : 10;
            if (c->inversion >= seq_chord_voices(c)) c->inversion = seq_chord_voices(c) - 1;
            c->quantize = 0;
        }
    }
    if (seq_chord_scale_type(c->type)) c->root = seq_scale_quantize(c->root, c->scale_root, c->scale_kind, 0);
}
/* Preflight, edit the existing shared rollback buffer, then publish metadata. */
static bool seq_chord_write(uint8_t track, uint16_t start, seq_chord_event_t c, bool erase) {
    seq_chord_prune();
    seq_chord_error = 0;
    int16_t owner = seq_chord_find(track, start), free = -1, previous = -1;
    if (erase) {
        if (owner < 0) return true;
        c     = seq_chord_events[owner];
        start = c.start;
    } else {
        if (start >= seq_track_length_steps(track)) {
            seq_chord_error = 1;
            return false;
        }
        c.start        = start;
        c.track        = track;
        uint16_t limit = seq_track_length_steps(track);
        for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++) {
            const seq_chord_event_t *e = &seq_chord_events[i];
            if (e->track == 255 && free < 0) free = i;
            if (e->track == track && e->start > start && e->start < limit) limit = e->start;
        }
        if (owner >= 0 && seq_chord_events[owner].start < start) {
            previous = owner;
            owner    = -1;
        }
        if (owner < 0 && free < 0) {
            seq_chord_error = 2;
            return false;
        }
        if (c.length > limit - start) c.length = limit - start;
    }
    uint8_t pitches[8], count = erase ? 0 : seq_chord_render(&c, pitches);
    if (!erase && (!count || !c.length)) {
        seq_chord_error = 1;
        return false;
    }
    mel_event_snapshot_t e;
    uint8_t              removed = 0;
    for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
        mel_track_event_at(track, i, &e);
        bool owned     = owner >= 0 && e.start_step == seq_chord_events[owner].start && e.end_step == seq_chord_events[owner].start + seq_chord_events[owner].length - 1;
        bool preceding = previous >= 0 && e.start_step == seq_chord_events[previous].start && e.end_step == seq_chord_events[previous].start + seq_chord_events[previous].length - 1;
        if (owned)
            removed++;
        else if (!preceding && e.start_step < c.start + c.length && e.end_step >= c.start) {
            seq_chord_error = 3;
            return false;
        }
    }
    if (mel_track_event_count(track) - removed + count > MEL_TRACK_MAX_EVENTS) {
        seq_chord_error = 2;
        return false;
    }
    seq_midi_stop_melodic_track(track, timer_read32());
    seq_chord_mutating = true;
    mel_track_edit_begin(track);
    for (int16_t i = mel_track_event_count(track) - 1; i >= 0; i--) {
        mel_track_event_at(track, i, &e);
        if (owner >= 0 && e.start_step == seq_chord_events[owner].start && e.end_step == seq_chord_events[owner].start + seq_chord_events[owner].length - 1)
            mel_track_remove_event(track, i);
        else if (previous >= 0 && e.start_step == seq_chord_events[previous].start && e.end_step == seq_chord_events[previous].start + seq_chord_events[previous].length - 1) {
            mel_track_remove_event(track, i);
            mel_track_add_note(track, e.start_step, start - 1, e.pitch, e.velocity);
        }
    }
    for (uint8_t i = 0; i < count; i++)
        mel_track_add_note(track, c.start, c.start + c.length - 1, pitches[i], c.velocity);
    bool ok            = mel_track_edit_end();
    seq_chord_mutating = false;
    if (!ok) {
        seq_chord_error = 2;
        return false;
    }
    if (previous >= 0) seq_chord_events[previous].length = start - seq_chord_events[previous].start;
    if (erase)
        seq_chord_events[owner].track = 255;
    else
        seq_chord_events[owner >= 0 ? owner : free] = c;
    seq_mark_dirty();
    return true;
}
