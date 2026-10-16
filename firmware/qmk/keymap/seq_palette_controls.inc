static bool seq_palette_ready(void) {
    return seq_state.active && slot_uses_melodic(view_slot) && !slot_viewing_child(view_slot) &&
           seq_euclid_editing_enabled() && !seq_chord_mode[view_slot] && !seq_transfer_active &&
           !record_state.armed && !step_edit.active && !seq_step_hold.active && !chord_capture.active &&
           !euclid_palette_capture.active && !melodic_key_mask[view_slot] && !seq_scale_editing &&
           !cc_context.active && !cc_mode.held && !switch_state.held && !seq_mod_del() &&
           !seq_shift_active() && !(seq_ctrl_mask | get_mods()) && !seq_loop_held;
}

static void seq_palette_set_quality(uint8_t track, uint8_t digit, uint8_t type) {
    if ((type == 10 || type == 11) && !seq_chord_scale_enabled(track)) return;
    uint8_t count = euclid_tracks[track].note_count;
    for (uint8_t i = 0; i < count; i++) {
        if (!digit || i == digit - 1) seq_modules[track].config.euclid.qualities[i] = type;
    }
    seq_mark_dirty();
}

static bool seq_palette_key(uint16_t key, bool pressed) {
    int8_t digit = seq_digit_from_keycode(key);
    if (!pressed && key == KC_C && seq_palette_toggle_held) { seq_palette_toggle_held = false; return true; }
    if (!pressed && digit >= 0 && (seq_palette_digit_held & (1u << digit))) {
        seq_palette_digit_held &= ~(1u << digit);
        if (seq_palette_edit_digit == digit) {
            if (!seq_palette_edit_used && seq_palette_ready() && view_slot == seq_palette_edit_track)
                seq_euclid_palette_pin_set(view_slot, digit - 1, timer_read32());
            seq_palette_cancel();
        }
        return true;
    }
    for (uint8_t i = 0; i < sizeof(seq_palette_types); i++) {
        if (!pressed && key == seq_palette_type_keys[i] && (seq_palette_type_held & (1u << i))) {
            seq_palette_type_held &= ~(1u << i);
            return true;
        }
    }
    if (!seq_palette_ready()) { seq_palette_cancel(); return false; }
    if (pressed && key == KC_C && cmd_state.held) {
        seq_palette_cancel();
        seq_palette_toggle_held = true;
        cmd_state.consumed = true;
        seq_midi_stop_melodic_track(view_slot, timer_read32());
        euclid_tracks[view_slot].pitch_mode = euclid_tracks[view_slot].pitch_mode == 2 ? 0 : 2;
        seq_mark_dirty();
        return true;
    }
    if (euclid_tracks[view_slot].pitch_mode != 2) { seq_palette_cancel(); return false; }
    if (pressed && digit >= 0 && digit <= 8) {
        /* Consume empty slots too; they must never turn into piano notes. */
        seq_palette_digit_held |= 1u << digit;
        if (seq_palette_edit_digit != 255 || (seq_palette_digit_held & (seq_palette_digit_held - 1))) seq_palette_cancel();
        else if (!digit || digit <= euclid_tracks[view_slot].note_count) {
            seq_palette_edit_digit = digit;
            seq_palette_edit_track = view_slot;
            seq_palette_edit_used = false;
        }
        if (cmd_state.held) cmd_state.consumed = true;
        return true;
    }
    if (seq_palette_edit_digit != 255 && pressed) {
        for (uint8_t i = 0; i < sizeof(seq_palette_types); i++) {
            if (key != seq_palette_type_keys[i]) continue;
            seq_palette_type_held |= 1u << i;
            seq_palette_edit_used = true;
            seq_palette_set_quality(view_slot, seq_palette_edit_digit, seq_palette_types[i]);
            return true;
        }
        seq_palette_cancel();
    }
    return false;
}

static bool seq_palette_encoder(uint8_t index, bool clockwise) {
    if (index || seq_palette_edit_digit == 255) return false;
    if (!seq_palette_ready() || seq_palette_edit_track != view_slot) { seq_palette_cancel(); return false; }
    seq_palette_edit_used = true;
    uint8_t slot = seq_palette_edit_digit ? seq_palette_edit_digit - 1 : 0;
    uint8_t type = seq_modules[view_slot].config.euclid.qualities[slot], pos = 0;
    while (pos + 1 < sizeof(seq_palette_types) && seq_palette_types[pos] != type) pos++;
    uint8_t count = seq_chord_scale_enabled(view_slot) ? 13 : 11;
    pos = clockwise ? (pos + 1) % count : (pos ? pos - 1 : count - 1);
    if (pos >= count) pos = count - 1;
    seq_palette_set_quality(view_slot, seq_palette_edit_digit, seq_palette_types[pos]);
    return true;
}
