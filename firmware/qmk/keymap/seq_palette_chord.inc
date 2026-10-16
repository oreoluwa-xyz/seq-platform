/* Palette qualities reuse chord rendering without allocating stored chord events. */
static const uint8_t seq_palette_types[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 10, 11};
static const uint16_t seq_palette_type_keys[] = {KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_Z, KC_X};

static uint8_t seq_palette_chord_notes(uint8_t track, uint8_t slot, uint8_t *notes) {
    const seq_euclid_track_t *e = &euclid_tracks[track];
    if (slot >= e->note_count) return 0;
    int16_t root = seq_melodic_note_pitch(track, e->palette_root) + e->note_pool[slot];
    if (root < 0 || root > 127) return 0;
    uint8_t type = seq_modules[track].config.euclid.qualities[slot];
    seq_chord_event_t chord = {.root = root, .type = type, .velocity = MEL_DEFAULT_VELOCITY, .scale_free = 1};
    if (type == 10 || type == 11) {
        /* Disabling scale keeps Auto's last scale context available for re-enable;
         * playback falls back to the root, rather than inventing a major chord. */
        if (!seq_chord_scale_enabled(track)) { notes[0] = root; return 1; }
        chord.scale_free = 0;
        seq_chord_context(track, &chord);
    }
    return seq_chord_render(&chord, notes);
}

static uint8_t seq_palette_resolved_type(const uint8_t *notes, uint8_t count) {
    if (count < 3 || count > 4) return 255;
    for (uint8_t i = 0; i < 11; i++) {
        seq_chord_event_t chord = {.root = notes[0], .type = seq_palette_types[i], .velocity = MEL_DEFAULT_VELOCITY};
        uint8_t candidate[8], size = seq_chord_render(&chord, candidate);
        if (size == count && !memcmp(candidate, notes, count)) return i;
    }
    return 255;
}
