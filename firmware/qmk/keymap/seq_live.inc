/* Read-only runtime snapshots. Kept separate from the project transfer lock. */
#define SEQ_LIVE_VERSION 1
#define SEQ_LIVE_SIZE 60
static void seq_select_root_track(uint8_t track);

static void seq_live_u16(uint8_t *out, uint16_t value) {
    out[0] = value & 255u;
    out[1] = value >> 8;
}

static void seq_live_note(uint8_t *mask, uint8_t note) {
    if (note < 128) mask[note >> 3] |= (uint8_t)(1u << (note & 7));
}

static void seq_live_snapshot(uint8_t *out) {
    memset(out, 0, SEQ_LIVE_SIZE);
    uint8_t track = view_slot % 12;
    uint8_t drum = slot_drum_track(track), melodic = slot_melodic_track(track);
    bool    melodic_view = slot_uses_melodic(track);
    out[0]               = SEQ_LIVE_VERSION;
    out[1]               = (seq_state.active ? 1 : 0) | (seq_state.running ? 2 : 0) | (record_state.armed ? 4 : 0) | (seq_external_clock ? 8 : 0) | (seq_transfer_active ? 16 : 0) | (slot_viewing_child(track) ? 32 : 0);
    if (seq_modules[track].composed) out[1] |= 64 | (seq_modules[track].rhythm.kind == 2 ? 128 : 0);
    out[2]               = track;
    out[3]               = slot_is_melodic(track) ? (slot_is_wrapper(track) ? 2 : 1) : 0;
    out[4]               = seq_modules[track].kind;
    out[5]               = midi_channel_for_track(melodic_view ? melodic : drum);
    out[6]               = 255;
    out[7]               = seq_drum_pool_used();
    seq_live_u16(out + 8, bpm);
    seq_live_u16(out + 10, seq_track_step_position[track]);
    seq_live_u16(out + 12, seq_track_length_steps(track));
    seq_live_u16(out + 14, view_bar);
    uint16_t lanes = melodic_view ? 0 : (note_hold_mask ? note_hold_mask : (1u << drum_active_note[drum]));
    seq_live_u16(out + 16, lanes);
    uint16_t melodic_mask = 0, wrapper_mask = 0, module_mask = 0;
    for (uint8_t i = 0; i < 12; i++) {
        if (slot_is_melodic(i)) melodic_mask |= 1u << i;
        if (slot_is_melodic(i) && slot_is_wrapper(i)) wrapper_mask |= 1u << i;
    }
    seq_live_u16(out + 18, melodic_mask);
    seq_live_u16(out + 20, wrapper_mask);
    seq_live_u16(out + 22, module_mask);
    if (slot_is_melodic(track)) memcpy(out + 24, melodic_active_map[melodic], 16);
    if (slot_has_drum_layer(track)) {
        for (uint8_t lane = 0; lane < SEQ_DRUM_NOTE_COUNT; lane++) {
            if (drum_note_gate_mask[drum] & (1u << lane)) seq_live_note(out + 24, drum_note_for_track(lane));
        }
    }
    if (module_preview.active && module_preview.track == track) {
        for (uint8_t i = 0; i < module_preview.count; i++)
            seq_live_note(out + 24, module_preview.notes[i]);
    }
    if(seq_chord_mode[track] && melodic_view){uint8_t pitches[8],count=seq_chord_render(&seq_chord_draft[track],pitches);for(uint8_t n=0;n<count;n++)seq_live_note(out+40,pitches[n]);}
    else if (step_edit.active && step_edit.track == (step_edit.is_drum ? drum : melodic)) {
        seq_live_note(out + 40, step_edit.is_drum ? drum_note_for_track(step_edit.note) : step_edit.pitch);
    } else if (!melodic_view) {
        for (uint8_t lane = 0; lane < SEQ_DRUM_NOTE_COUNT; lane++) {
            if (lanes & (1u << lane)) seq_live_note(out + 40, drum_note_for_track(lane));
        }
    } else {
        uint16_t keys = melodic_key_mask[melodic] ? melodic_key_mask[melodic] : melodic_last_mask[melodic];
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            if (keys & (1u << i)) seq_live_note(out + 40, seq_melodic_note_pitch(melodic, i));
        }
    }
    seq_live_u16(out + 56, seq_slot_playback_mask(track, view_bar));
    seq_live_u16(out + 58, step_edit.active ? step_edit.local_step : UINT16_MAX);
}

static bool seq_live_can_select_without_space(void) {
    if (seq_loop_held || seq_palette_digit_held || seq_palette_type_held) return false;
    if (seq_chord_held || seq_chord_note_held || seq_step_hold.active || seq_scale_editing || seq_transfer_active || step_edit.active || chord_capture.active || record_state.capturing || note_hold_mask || cmd_state.held) return false;
    for (uint8_t i = 0; i < 12; i++) {
        if (track_held[i] || melodic_key_mask[i]) return false;
    }
    return true;
}

static bool seq_live_can_select(void) {
    return !switch_state.held && !seq_ctrl_space_held && seq_live_can_select_without_space();
}
