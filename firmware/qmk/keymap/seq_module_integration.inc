/* Engine-owned audition and stamping. Modules never send MIDI directly. */
static bool seq_module_preview_owns(uint8_t track, uint8_t note) {
    if (!module_preview.active || module_preview.track != track) return false;
    for (uint8_t i = 0; i < module_preview.count; i++)
        if (module_preview.notes[i] == note) return true;
    return false;
}
static void seq_module_preview_stop(uint32_t now) {
    if (!module_preview.active) return;
    uint8_t track         = module_preview.track;
    module_preview.active = false;
    for (uint8_t i = 0; i < module_preview.count; i++) {
        uint8_t note = module_preview.notes[i];
        if (!melodic_active_contains(track, note)) virtual_midi_note_off(module_preview.channel, note, now);
    }
    module_preview.count = 0;
}
static void seq_module_audition(uint8_t track, uint16_t key, uint32_t now) {
    seq_module_preview_stop(now);
    if (melodic_track_muted[track]) return;
    const seq_module_t     *m = &seq_modules[track];
    const seq_chord_bank_t *b = &m->config.chords;
    module_preview.count      = seq_module_chord_notes(m, b->selected, module_preview.notes);
    module_preview.track      = track;
    module_preview.channel    = midi_channel_for_track(track);
    module_preview.key        = key;
    module_preview.active     = module_preview.count > 0;
    uint16_t velocity         = (uint16_t)b->slots[b->selected].velocity * melodic_track_velocity[track] / 127u;
    for (uint8_t i = 0; i < module_preview.count; i++)
        if (!melodic_active_contains(track, module_preview.notes[i])) virtual_midi_note_on(module_preview.channel, module_preview.notes[i], velocity ? velocity : 1, now);
}
static bool seq_module_stamp(uint8_t track, uint16_t step) {
    const seq_module_t *m = &seq_modules[track];
    if (m->kind != SEQ_MODULE_CHORD_BANK || !slot_is_melodic(track)) return false;
    const seq_chord_bank_t *b    = &m->config.chords;
    const seq_chord_slot_t *slot = &b->slots[b->selected];
    uint8_t                 notes[SEQ_CHORD_VOICES];
    uint8_t                 count  = seq_module_chord_notes(m, b->selected, notes);
    uint16_t                length = seq_track_length_steps(track);
    if (step >= length) return false;
    uint16_t duration = slot->length > length ? length : slot->length;
    uint16_t end      = step + duration - 1u;
    uint16_t needed   = count * (end >= length ? 2u : 1u);
    if (!mel_track_can_add(track, needed)) {
        seq_drum_pool_report_full();
        return false;
    }
    mel_track_edit_begin(track);
    for (uint8_t i = 0; i < count; i++) {
        mel_track_add_note(track, step, end >= length ? length - 1u : end, notes[i], slot->velocity);
        if (end >= length) mel_track_add_note(track, 0, end - length, notes[i], slot->velocity);
    }
    return mel_track_edit_end();
}
static int8_t seq_module_bank_key(uint16_t key) {
    static const uint16_t keys[SEQ_CHORD_SLOTS] = {KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINUS, KC_EQUAL};
    for (uint8_t i = 0; i < SEQ_CHORD_SLOTS; i++)
        if (keys[i] == key) return i;
    return -1;
}
static bool seq_module_key(uint16_t key, bool pressed, uint32_t now) {
    if (!pressed && module_preview.active && module_preview.key == key) seq_module_preview_stop(now);
    uint8_t track = view_slot % 12;
    if (!seq_state.active || seq_modules[track].kind != SEQ_MODULE_CHORD_BANK) return false;
    if (seq_is_option_key(key)) {
        cmd_state.held     = pressed;
        cmd_state.consumed = true;
        if (!pressed) seq_module_preview_stop(now);
        return true;
    }
    if (!cmd_state.held || key == SEQ_SWITCH_KEY || key == KC_ESC) return false;
    int8_t bank = seq_module_bank_key(key);
    if (bank >= 0) {
        if (pressed) {
            seq_modules[track].config.chords.selected = bank;
            seq_mark_dirty();
            seq_module_audition(track, key, now);
            if (record_state.armed && seq_state.running) seq_module_stamp(track, seq_track_step_position[track]);
        }
        return true;
    }
    int8_t step = step_from_keycode(key);
    if (step >= 0) {
        if (pressed) {
            uint16_t absolute = view_bar * SEQ_STEPS_PER_BAR + step;
            if (!seq_module_stamp(track, absolute)) seq_drum_pool_report_full();
        }
        return true;
    }
    return true;
}
