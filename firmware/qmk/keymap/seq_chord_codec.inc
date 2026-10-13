static uint16_t seq_codec_chords;
static void     seq_chord_codec_custom(seq_codec_t *c, seq_chord_event_t *ev, uint8_t version) {
    ev->scale_free = version >= 4 ? seq_codec_u8(c, ev->scale_free) : 0;
    if (version < 4 && ev->type > SEQ_CHORD_CUSTOM) c->ok = false;
    ev->quantize      = version >= 3 ? seq_codec_u8(c, ev->quantize) : 0;
    ev->quantize_root = ev->quantize ? seq_codec_u8(c, ev->quantize_root) : 0;
    ev->quantize_kind = ev->quantize ? seq_codec_u8(c, ev->quantize_kind) : 0;
    if (version >= 3 && ev->type == SEQ_CHORD_CUSTOM) {
        ev->custom_count = seq_codec_u8(c, ev->custom_count);
        if (ev->custom_count < 2 || ev->custom_count > 8) {
            c->ok = false;
            return;
        }
        for (uint8_t i = 0; i < ev->custom_count; i++)
            ev->intervals[i] = (int8_t)seq_codec_u8(c, ev->intervals[i]);
        uint8_t length = 0;
        while (length < SEQ_CHORD_NAME_SIZE && ev->name[length])
            length++;
        length = seq_codec_u8(c, length);
        if (!length || length > SEQ_CHORD_NAME_SIZE) {
            c->ok = false;
            return;
        }
        for (uint8_t i = 0; i < length; i++)
            ev->name[i] = seq_codec_u8(c, ev->name[i]);
        if (length < SEQ_CHORD_NAME_SIZE) memset(ev->name + length, 0, SEQ_CHORD_NAME_SIZE - length);
    } else {
        if (ev->type == SEQ_CHORD_CUSTOM) c->ok = false;
        ev->custom_count = 0;
        memset(ev->intervals, 0, sizeof(ev->intervals));
        memset(ev->name, 0, sizeof(ev->name));
    }
}
static uint8_t seq_chord_codec_version(const seq_chord_event_t *c) {
    return c->scale_free || c->type > SEQ_CHORD_CUSTOM ? 4 : c->type == SEQ_CHORD_CUSTOM || c->quantize ? 3 : c->inversion ? 2 : 1;
}
static void seq_chord_codec(seq_codec_t *c, uint8_t track, const mel_event_snapshot_t *notes, uint8_t note_count, uint16_t length, bool *chord_mode) {
    *chord_mode = false;

    uint8_t           count = seq_chord_count(track), enabled = seq_chord_mode[track];
    seq_chord_event_t draft   = seq_chord_draft[track];
    bool              present = !c->read ? (enabled || count) : (c->pos < c->size && c->data[c->pos] == 'C');
    if (!present) {
        if (c->read && c->apply) {
            seq_chord_mode[track] = false;
            seq_chord_clear_meta(track);
        }
        return;
    }
    seq_codec_u8(c, 'C');
    uint8_t version = seq_chord_codec_version(&draft);
    for (uint8_t i = 0; i < SEQ_CHORD_EVENT_CAPACITY; i++)
        if (seq_chord_events[i].track == track && seq_chord_codec_version(&seq_chord_events[i]) > version) version = seq_chord_codec_version(&seq_chord_events[i]);
    version = seq_codec_u8(c, version);
    if (version < 1 || version > 4) {
        c->ok = false;
        return;
    }
    enabled = seq_codec_u8(c, enabled);
    count   = seq_codec_u8(c, count);
    seq_codec_chords += count;
    if (enabled > 1 || count > SEQ_CHORD_EVENT_CAPACITY) {
        c->ok = false;
        return;
    }
    *chord_mode      = enabled;
    draft.root       = seq_codec_u8(c, draft.root);
    draft.type       = seq_codec_u8(c, draft.type);
    draft.length     = seq_codec_u16(c, draft.length);
    draft.velocity   = seq_codec_u8(c, draft.velocity);
    draft.scale_root = seq_codec_u8(c, draft.scale_root);
    draft.scale_kind = seq_codec_u8(c, draft.scale_kind);
    draft.inversion  = version >= 2 ? seq_codec_u8(c, draft.inversion) : 0;
    seq_chord_codec_custom(c, &draft, version);
    if (!c->ok) return;
    uint8_t pitches[8];
    if (!seq_chord_render(&draft, pitches) || !draft.length || draft.length > SEQ_MAX_STEPS) c->ok = false;
    uint8_t  source = 0;
    uint16_t starts[SEQ_CHORD_EVENT_CAPACITY], ends[SEQ_CHORD_EVENT_CAPACITY];
    if (c->read && c->apply) seq_chord_clear_meta(track);
    for (uint8_t i = 0; i < count; i++) {
        seq_chord_event_t ev = {.track = track};
        if (!c->read) {
            while (source < SEQ_CHORD_EVENT_CAPACITY && seq_chord_events[source].track != track)
                source++;
            if (source >= SEQ_CHORD_EVENT_CAPACITY) {
                c->ok = false;
                return;
            }
            ev = seq_chord_events[source++];
        }
        ev.start      = seq_codec_u16(c, ev.start);
        ev.length     = seq_codec_u16(c, ev.length);
        ev.root       = seq_codec_u8(c, ev.root);
        ev.type       = seq_codec_u8(c, ev.type);
        ev.velocity   = seq_codec_u8(c, ev.velocity);
        ev.scale_root = seq_codec_u8(c, ev.scale_root);
        ev.scale_kind = seq_codec_u8(c, ev.scale_kind);
        ev.inversion  = version >= 2 ? seq_codec_u8(c, ev.inversion) : 0;
        seq_chord_codec_custom(c, &ev, version);
        if (!c->ok) return;
        if (!ev.length || ev.start >= length || ev.length > length - ev.start || !seq_chord_render(&ev, pitches)) c->ok = false;
        for (uint8_t j = 0; j < i; j++)
            if (ev.start < ends[j] && ev.start + ev.length > starts[j]) c->ok = false;
        starts[i] = ev.start;
        ends[i]   = ev.start + ev.length;
        if (c->read && !seq_chord_matches(&ev, notes, note_count)) c->ok = false;
        if (c->read && c->apply && c->ok) {
            uint8_t j = 0;
            while (j < SEQ_CHORD_EVENT_CAPACITY && seq_chord_events[j].track != 255)
                j++;
            if (j == SEQ_CHORD_EVENT_CAPACITY)
                c->ok = false;
            else
                seq_chord_events[j] = ev;
        }
    }
    if (c->read && c->apply && c->ok) {
        seq_chord_mode[track]  = enabled;
        seq_chord_draft[track] = draft;
    }
}
