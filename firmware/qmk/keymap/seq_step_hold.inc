/* Defer removal of existing steps until a short tap is released. New hits stay
 * immediate; a hold or encoder turn adopts the existing bounded step editor. */
static bool seq_step_hold_arm(uint16_t key, uint8_t slot, uint16_t step, bool tap, uint32_t now) {
    if (seq_step_hold_replaying || step_edit.active || seq_shift_active() || seq_ctrl_mask || get_mods() ||
        cmd_state.held || switch_state.held || note_hold_mask || chord_capture.active || record_state.armed ||
        euclid_mode_active || seq_transfer_active || seq_modules[slot].kind == SEQ_MODULE_CHORD_BANK) return false;
    for (uint8_t i = 0; i < 12; i++) if (track_held[i] || melodic_key_mask[i]) return false;
    if (slot_uses_melodic(slot)) {
        mel_note_descriptor_t note;
        if (!mel_track_note_descriptor(slot_melodic_track(slot), step % seq_track_length_steps(slot), &note)) return false;
    } else if (!seq_drum_step_get(slot_drum_track(slot), drum_active_note[slot_drum_track(slot)], step)) return false;
    seq_step_hold.active = true;
    seq_step_hold.editing = false;
    seq_step_hold.tap = tap;
    seq_step_hold.key = key;
    seq_step_hold.slot = slot;
    seq_step_hold.step = step;
    seq_step_hold.started = now;
    return true;
}
static void seq_step_hold_begin(void) {
    if (!seq_step_hold.active || seq_step_hold.editing) return;
    if (!seq_state.active || seq_transfer_active || view_slot != seq_step_hold.slot) {
        seq_step_hold_finish(false);
        return;
    }
    if (seq_step_edit_try_begin(seq_step_hold.slot, seq_step_hold.step, true, timer_read32())) {
        seq_step_hold.editing = true;
        step_edit.param = step_edit.is_drum ? STEP_EDIT_PARAM_VELOCITY : STEP_EDIT_PARAM_LENGTH;
        step_last_tap_info[seq_step_hold.slot] = 0;
    } else seq_step_hold_finish(false);
}
static void seq_step_hold_finish(bool tap) {
    if (!seq_step_hold.active) return;
    bool replay = tap && seq_step_hold.tap && !seq_step_hold.editing;
    uint16_t key = seq_step_hold.key;
    if (seq_step_hold.editing) seq_step_edit_reset();
    memset(&seq_step_hold, 0, sizeof(seq_step_hold));
    if (replay) {
        seq_step_hold_replaying = true;
        keyrecord_t record = {.event.pressed = true};
        seq_pre_process(key, &record);
        seq_step_hold_replaying = false;
    }
}
