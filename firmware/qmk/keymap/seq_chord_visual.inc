static void seq_chord_snapshot(void) {
    seq_chord_prune();
    uint8_t                  t   = view_slot;
    uint8_t                 *out = seq_visual_image;
    const seq_chord_event_t *d   = &seq_chord_draft[t];
    out[0]                       = 4;
    out[1]                       = t;
    out[2]                       = seq_chord_mode[t];
    out[3]                       = d->root;
    out[4]                       = d->type;
    seq_live_u16(out + 5, d->length);
    out[7] = seq_chord_parameter;
    seq_live_u16(out + 8, seq_chord_selected);
    out[10] = seq_chord_error;
    out[11] = seq_chord_count(t);
    out[12] = seq_chord_render(d, out + 16);
    out[13] = !!seq_chord_held;
    out[14] = d->scale_root;
    out[15] = d->scale_kind;
    seq_live_u16(out + 24, view_bar);
    seq_live_u16(out + 26, seq_track_length_steps(t));
    uint16_t mask = 0;
    for (uint8_t i = 0; i < 12; i++)
        if (seq_chord_mode[i]) mask |= 1u << i;
    seq_live_u16(out + 28, mask);
    out[30] = d->inversion;
    out[31] = d->quantize;
    out[32] = seq_chord_voices(d);
    memcpy(out + 33, d->intervals, 8);
    memcpy(out + 41, d->name, SEQ_CHORD_NAME_SIZE);
    out[53] = d->velocity;
    out[54] = d->quantize_root;
    out[55] = d->quantize_kind;
    out[56] = d->scale_free;
    out[57] = seq_chord_scale_enabled(t);
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t *cell  = out + 64 + i * 24;
        int16_t  index = seq_chord_find(t, view_bar * 16u + i);
        if (index < 0) continue;
        const seq_chord_event_t *e = &seq_chord_events[index];
        cell[0]                    = (e->scale_free ? 4 : 0) | 1 | (e->start == view_bar * 16u + i ? 2 : 0);
        cell[1]                    = e->root;
        cell[2]                    = e->type;
        cell[7]                    = e->inversion;
        cell[8]                    = e->quantize;
        cell[9]                    = seq_chord_voices(e);
        memcpy(cell + 10, e->name, SEQ_CHORD_NAME_SIZE);
        cell[22] = e->scale_root;
        cell[23] = e->scale_kind;
        seq_live_u16(cell + 3, e->start);
        seq_live_u16(cell + 5, e->length);
    }
}
