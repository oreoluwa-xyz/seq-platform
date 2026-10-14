/* TEMP owns the project scratch buffer until rollback or commit. Neither action writes EEPROM. */
static bool seq_temp_decode(bool apply) {
    uint16_t pos = sizeof(seq_temp_context_t), lanes = 0, events = 0, chords = 0;
    for (uint8_t object = 0; object <= 12; object++) {
        if (pos + 2u > seq_temp_length) return false;
        uint16_t size = seq_transfer_buffer[pos] | ((uint16_t)seq_transfer_buffer[pos + 1] << 8);
        pos += 2;
        if (size > seq_temp_length - pos) return false;
        uint16_t used = size;
        if (!seq_project_object(object, seq_transfer_buffer + pos, &used, true, apply)) return false;
        lanes += seq_codec_lanes;
        events += seq_codec_cc;
        chords += seq_codec_chords;
        if (lanes > SEQ_DRUM_LANE_POOL_CAPACITY || events > SEQ_CC_EVENT_CAPACITY || chords > SEQ_CHORD_EVENT_CAPACITY) return false;
        pos += size;
    }
    return pos == seq_temp_length;
}

static bool seq_temp_begin(uint32_t now) {
    if (seq_temp_active) return true; /* Never replace the original drop. */
    if (!seq_state.active || !seq_live_can_select() || record_state.armed || cc_session.active || cc_mode.held || cc_mode.latched_temporary || seq_track_confirmation_active()) goto full;
    seq_flush_pending_erase();
    seq_temp_context_t *context = &seq_project_scratch.temp;
    context->slot = view_slot;
    context->child_slot = view_child_slot;
    context->bar = view_bar;
    context->child = view_child_active;
    context->dirty = seq_dirty;
    context->save_status = seq_save_status;
    memcpy(context->euclid, euclid_tracks, sizeof(context->euclid));
    memcpy(context->drafts, seq_chord_draft, sizeof(context->drafts));
    memcpy(context->accumulators, seq_accumulators, sizeof(context->accumulators));
    memcpy(context->picked, melodic_last_mask, sizeof(context->picked));
    memcpy(context->last_semitone, melodic_last_semitone, sizeof(context->last_semitone));
    memcpy(context->drum_note, drum_active_note, sizeof(context->drum_note));
    uint16_t pos = sizeof(*context);
    for (uint8_t object = 0; object <= 12; object++) {
        if (pos + 2u >= SEQ_TEMP_CAPACITY) goto full;
        uint16_t size = SEQ_TEMP_CAPACITY - pos - 2u;
        if (!seq_project_object(object, seq_transfer_buffer + pos + 2u, &size, false, false)) goto full;
        seq_transfer_buffer[pos++] = size & 255u;
        seq_transfer_buffer[pos++] = size >> 8;
        pos += size;
    }
    seq_temp_length = pos;
    if (!seq_temp_decode(false)) goto full;
    seq_temp_restore_pending = false;
    seq_temp_active = true;
    seq_harmony_source_size = 0;
    return true;
full:
    seq_temp_length = 0;
    seq_drum_pool_report_full();
    return false;
}

static void seq_temp_commit(void) {
    if (!seq_temp_active) return;
    seq_flush_pending_erase();
    seq_temp_restore_pending = false;
    seq_temp_active = false;
    seq_temp_length = 0;
    seq_harmony_source_size = 0;
    seq_storage_save_pending = false;
    seq_mark_dirty();
}

static bool seq_temp_restore(uint32_t now) {
    if (!seq_temp_active || !seq_temp_decode(false)) return false;
    const seq_temp_context_t *context = &seq_project_scratch.temp;
    /* Cancel unfinished gestures before replacing their underlying data. */
    seq_step_hold_finish(false);
    seq_chord_cancel();
    seq_handle_escape(now);
    seq_scale_editing = false;
    seq_harmony_request = 0;
    seq_pending_erase = false;
    seq_record_reset();
    seq_midi_all_notes_off(now);
    seq_ratchet_clear();
    memset(melodic_key_mask, 0, sizeof(melodic_key_mask));
    memset(melodic_hold_offset, 0, sizeof(melodic_hold_offset));
    memset(melodic_tie_pending, 0, sizeof(melodic_tie_pending));
    note_hold_mask = 0;
    memset(track_held, 0, sizeof(track_held));
    memset(track_tap_count, 0, sizeof(track_tap_count));
    memset(step_last_tap_info, 0, sizeof(step_last_tap_info));
    memset(&octave_hold_positive, 0, sizeof(octave_hold_positive));
    memset(&octave_hold_negative, 0, sizeof(octave_hold_negative));
    seq_chord_tap_reset();
    seq_loading = true;
    /* Free shared pools first: the saved project may distribute them differently. */
    seq_drum_pool_init();
    seq_automation_init();
    memset(seq_chord_events, 255, sizeof(seq_chord_events));
    bool ok = seq_temp_decode(true);
    seq_loading = false;
    if (!ok) return false; /* Preflight above validates every object and pool. */
    memcpy(euclid_tracks, context->euclid, sizeof(context->euclid));
    memcpy(seq_chord_draft, context->drafts, sizeof(context->drafts));
    memcpy(seq_accumulators, context->accumulators, sizeof(context->accumulators));
    memcpy(melodic_last_mask, context->picked, sizeof(context->picked));
    memcpy(melodic_last_semitone, context->last_semitone, sizeof(context->last_semitone));
    memcpy(drum_active_note, context->drum_note, sizeof(context->drum_note));
    view_slot = context->slot;
    view_child_slot = context->child_slot;
    view_child_active = context->child;
    view_bar = context->bar;
    seq_update_total_steps();
    for (uint8_t t = 0; t < 12; t++) {
        seq_track_sync_position(t);
        for (uint8_t i = 0; i < SEQ_CC_MAPPING_COUNT; i++) seq_cc_send_value(t, i, cc_values[t][i], now);
    }
    seq_clamp_view_bar();
    seq_apply_layout(current_layout);
    seq_clock_error = seq_clock_denominator = 0;
    seq_resume_after_skip = true; /* Rejoin sustained notes on the next clock tick. */
    seq_dirty = context->dirty;
    seq_save_status = context->save_status;
    seq_storage_save_pending = false;
    seq_temp_restore_pending = false;
    seq_temp_active = false;
    seq_temp_length = 0;
    return true;
}
