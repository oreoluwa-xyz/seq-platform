static void test_navigation_length(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    assert(mel_track_add_note(0, 2, 5, 60, 100));
    uint8_t harmony = seq_harmony_request;
    key(KC_LCTL, true);
    key(KC_SPC, true);
    assert(!seq_scale_editing && !seq_live_can_select());
    assert(seq_cc_encoder_update(0, true));
    assert(seq_track_length_steps(0) == 17 && seq_track_length_steps(1) == 16);
    assert(bpm == 120 && seq_timing_swing == 0 && mel_track_event_count(0) == 1);
    key(KC_LSFT, true);
    seq_encoder_update(0, true);
    assert(seq_track_length_steps(0) == 33);
    seq_encoder_update(0, false);
    assert(seq_track_length_steps(0) == 17);
    key(KC_LSFT, false);
    key(KC_LCTL, false); /* Releasing Ctrl first must not open Scale or change swing. */
    seq_encoder_update(0, true);
    key(KC_SPC, false);
    assert(seq_track_length_steps(0) == 17 && seq_timing_swing == 0);
    assert(!seq_scale_editing && !seq_ctrl_space_held && seq_harmony_request == harmony);

    /* Space-first order, right modifiers, external clock, and non-bar lengths. */
    key(KC_SPC, true);
    key(KC_RCTL, true);
    key(KC_RSFT, true);
    seq_external_clock = true;
    seq_encoder_update(0, true);
    assert(seq_track_length_steps(0) == 33 && bpm == 120);
    seq_encoder_update(0, false);
    key(KC_RSFT, false);
    seq_encoder_update(0, false);
    assert(seq_track_length_steps(0) == 16);
    key(KC_SPC, false);
    key(KC_RCTL, false);
    assert(!seq_scale_editing && seq_harmony_request == harmony);
    seq_external_clock = false;

    /* Unmodified navigation still edits swing; Ctrl without Space still edits tempo. */
    key(KC_SPC, true); seq_encoder_update(0, true); key(KC_SPC, false);
    key(KC_LCTL, true); seq_encoder_update(0, true); key(KC_LCTL, false);
    assert(seq_timing_swing == 1 && bpm == 121);

    /* Shortening uses the existing destructive trim; expansion adds no notes. */
    assert(mel_track_add_note(0, 15, 15, 65, 100));
    assert(seq_automation_record(0, 0, 15, 90));
    key(KC_LCTL, true); key(KC_SPC, true);
    seq_encoder_update(0, false);
    assert(seq_track_length_steps(0) == 15 && mel_track_event_count(0) == 1 && seq_automation_count() == 0);
    seq_encoder_update(0, true);
    assert(mel_track_event_count(0) == 1);
    seq_track_set_steps(0, 1);
    seq_encoder_update(0, false);
    assert(seq_track_length_steps(0) == 1);
    seq_track_set_steps(0, SEQ_MAX_STEPS);
    key(KC_LSFT, true); seq_encoder_update(0, true);
    assert(seq_track_length_steps(0) == SEQ_MAX_STEPS);
    key(KC_LSFT, false); key(KC_SPC, false); key(KC_LCTL, false);
}

static void test_navigation_guards(void) {
    reset(); seq_enter(test_now);
    uint8_t harmony = seq_harmony_request;
    key(KC_LCTL, true); key(KC_LSFT, true); key(KC_SPC, true);
    seq_encoder_update(0, true);
    key(KC_SPC, false); key(KC_LSFT, false); key(KC_LCTL, false);
    assert(seq_track_length_steps(0) == 32 && seq_harmony_request == harmony && !seq_scale_editing);
    for (uint8_t blocked = 0; blocked < 4; blocked++) {
        record_state.armed = blocked == 0;
        seq_transfer_active = blocked == 1;
        step_edit.active = blocked == 2;
        cc_session.active = blocked == 3;
        key(KC_LCTL, true); key(KC_SPC, true);
        assert(seq_cc_encoder_update(0, true));
        key(KC_SPC, false); key(KC_LCTL, false);
        assert(seq_track_length_steps(0) == 32 && bpm == 120 && !seq_scale_editing);
    }
    record_state.armed = seq_transfer_active = step_edit.active = cc_session.active = false;
    key(KC_LCTL, true); key(KC_SPC, true);
    seq_backup_request(1, 0, 0, NULL, 0);
    seq_backup_request(7, 0, 0, NULL, 0);
    key(KC_SPC, false); key(KC_LCTL, false);
    assert(!seq_scale_editing); /* Completing a transfer must not revive the tap action. */
    key(KC_LCTL, true); key(KC_SPC, true); seq_exit();
    key(KC_SPC, false); key(KC_LCTL, false);
    assert(!seq_scale_editing && !seq_ctrl_space_held);
}

static void test_navigation_double_and_follow(void) {
    reset(); seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_track_set_steps(0, 7);
    assert(mel_track_add_note(0, 1, 2, 60, 100));
    assert(seq_automation_record(0, 0, 2, 90));
    bool followed = follow_mode;
    key(KC_SPC, true); key(KC_EQUAL, true); key(KC_EQUAL, false); key(KC_SPC, false);
    assert(seq_track_length_steps(0) == 14 && mel_track_event_count(0) == 2 && seq_automation_count() == 2);
    assert(follow_mode == followed);
    mel_event_snapshot_t event;
    assert(mel_track_event_at(0, 1, &event));
    assert(event.start_step == 8 && event.end_step == 9 && event.pitch == 60);
    key(KC_RSFT, true); key(KC_SPC, true); key(KC_EQUAL, true); key(KC_EQUAL, false);
    key(KC_SPC, false); key(KC_RSFT, false);
    assert(seq_track_length_steps(0) == 28 && mel_track_event_count(0) == 2 && seq_automation_count() == 2);
    key(KC_SPC, true); key(KC_F, true); key(KC_F, false);
    assert(follow_mode != followed && seq_track_length_steps(0) == 28);
    playhead_bar = 1; view_bar = 0;
    key(KC_F, true); key(KC_F, false); key(KC_SPC, false);
    assert(follow_mode == followed && view_bar == 1 && mel_track_event_count(0) == 2);

    /* Doubling at capacity fails atomically. Recording blocks the new shortcut. */
    seq_track_set_steps(0, SEQ_MAX_STEPS);
    key(KC_SPC, true); key(KC_EQUAL, true); key(KC_EQUAL, false); key(KC_SPC, false);
    assert(seq_track_length_steps(0) == SEQ_MAX_STEPS && mel_track_event_count(0) == 2);
    seq_track_set_steps(0, 16); record_state.armed = true;
    key(KC_SPC, true); key(KC_EQUAL, true); key(KC_EQUAL, false); key(KC_SPC, false);
    assert(seq_track_length_steps(0) == 16);
}
