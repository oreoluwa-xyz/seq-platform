/* Defer Ctrl+Space editors until release so turning the knob edits length. */
static bool seq_ctrl_space_key(uint16_t key, bool pressed) {
    if (seq_ctrl_space_held) {
        if (key == KC_SPC) {
            if (!pressed) {
                seq_ctrl_space_held = false;
                if (!seq_ctrl_space_used && seq_ctrl_space_ready && seq_state.active && seq_live_can_select() &&
                    !record_state.armed && !seq_track_confirmation_active()) {
                    if (seq_ctrl_space_harmony) seq_harmony_request++;
                    else {
                        seq_scale_pending = seq_scale_global;
                        seq_scale_editing = true;
                    }
                }
            }
            return true;
        }
        if (key == KC_LCTL || key == KC_RCTL || key == KC_LSFT || key == KC_RSFT) return false;
        if (pressed) seq_ctrl_space_used = true;
        return false;
    }
    if (key != KC_SPC || !pressed || !seq_state.active || seq_scale_editing ||
        !((seq_ctrl_mask | get_mods()) & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_RCTL)))) return false;
    seq_ctrl_space_ready = seq_live_can_select() && !record_state.armed && !seq_track_confirmation_active();
    seq_ctrl_space_held = true;
    seq_ctrl_space_used = false;
    seq_ctrl_space_harmony = seq_shift_active() || (get_mods() & (MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT)));
    return true;
}
static const uint16_t seq_scale_mode_keys[] = {KC_A,KC_S,KC_D,KC_F,KC_G,KC_H,KC_J,KC_K,KC_L,KC_SCLN,KC_QUOT};
static void seq_scale_set_global(seq_scale_context_t value) {
    for (uint8_t track = 0; track < 12; track++)
        if (seq_scale_tracks[track].mode == 0 && seq_modules[track].kind != SEQ_MODULE_CHORD_BANK)
            seq_midi_stop_melodic_track(track, timer_read32());
    seq_scale_global = value; seq_scale_revision++; seq_mark_dirty();
}
static bool seq_scale_key(uint16_t key, bool pressed) {
    if (!seq_state.active || seq_transfer_active) return false;
    if (key == KC_LCTL || key == KC_RCTL || key == KC_LSFT || key == KC_RSFT) return false;
    if (seq_scale_editing) {
        if (pressed) {
            if (key == KC_ESC) seq_scale_editing = false;
            else if (key == KC_ENT) { seq_scale_set_global(seq_scale_pending); seq_scale_editing = false; }
            else if (key == KC_0) seq_scale_pending.enabled = !seq_scale_pending.enabled;
            else {
                for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) { seq_melodic_key_t note = SEQ_MELODIC_KEY(i); if (key == note.keycode) { seq_scale_pending.root = note.semitone; seq_scale_pending.enabled = 1; } }
                for (uint8_t i = 0; i < SEQ_SCALE_COUNT; i++) if (key == seq_scale_mode_keys[i]) { seq_scale_pending.kind = i; seq_scale_pending.enabled = 1; }
            }
        }
        return true;
    }
    return false;
}
static void seq_scale_request(uint8_t track, uint16_t context, const uint8_t *data, uint8_t size) {
    uint8_t status = 0;
    if (!data || size < 1 || data[0] > 1 || (data[0] == 0 ? size != 6 || track || context : size != 7 || track >= 12 || context > 1)) status = 3;
    else if (!seq_state.active || !seq_live_can_select() || record_state.armed || seq_track_confirmation_active() ||
             (data[size-2] | ((uint16_t)data[size-1]<<8)) != seq_scale_revision ||
             (data[0] == 1 && (track != view_slot || context != slot_viewing_child(track)))) status = 2;
    else if (data[0] == 0) {
        if (data[1] > 1 || data[2] > 11 || data[3] >= SEQ_SCALE_COUNT) status = 3;
        else seq_scale_set_global((seq_scale_context_t){data[1],data[2],data[3]});
    } else if (data[1] > 2 || data[2] > 11 || data[3] >= SEQ_SCALE_COUNT || data[4] > 2) status = 3;
    else {
        seq_midi_stop_melodic_track(slot_melodic_track(track), timer_read32());
        seq_scale_tracks[track] = (seq_scale_track_t){data[1],data[2],data[3],data[4]};
        seq_scale_revision++; seq_mark_dirty();
    }
    virtual_midi_backup_reply(20,track,context,status,NULL,0);
}
