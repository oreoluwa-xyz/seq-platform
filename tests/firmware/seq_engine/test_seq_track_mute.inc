static void track_mute_gesture(uint8_t track, bool right_shift) {
    key(KC_SPC, true);
    key(right_shift ? KC_RSFT : KC_LSFT, true);
    key(KC_F1 + track, true);
    key(KC_F1 + track, false);
    key(right_shift ? KC_RSFT : KC_LSFT, false);
    key(KC_SPC, false);
}

static void test_track_mute_gestures(void) {
    reset(); seq_enter(test_now);
    drum_lane_muted[3][1] = true;
    uint8_t focus = view_slot, lane = drum_active_note[3];
    for (uint8_t t = 0; t < 12; t++) {
        track_mute_gesture(t, t % 2);
        assert(seq_track_muted[t] && !mute_pending.active);
        track_mute_gesture(t, !(t % 2));
        assert(!seq_track_muted[t]);
    }
    assert(seq_drum_pool_used() == 0 && drum_lane_muted[3][1]);
    assert(view_slot == focus && drum_active_note[3] == lane);
    /* A pending lane gesture must not redirect the whole-track mute. */
    key(KC_SPC, true); key(KC_F2, true); key(KC_F2, false);
    key(KC_LSFT, true); key(KC_F4, true); key(KC_F4, false);
    key(KC_LSFT, false); key(KC_SPC, false);
    assert(seq_track_muted[3] && !seq_track_muted[1] && !mute_pending.active);
    assert(drum_lane_muted[3][1] && !drum_lane_muted[1][3]);
    /* Lane choices remain editable under the track gate. */
    key(KC_SPC, true); key(KC_F4, true); key(KC_F4, false);
    key(KC_F3, true); key(KC_F3, false); key(KC_SPC, false);
    assert(drum_lane_muted[3][2] && seq_track_muted[3]);
    track_mute_gesture(3, false);
    assert(!seq_track_muted[3] && drum_lane_muted[3][1] && drum_lane_muted[3][2]);
    seq_slots[3].kind = SEQ_SLOT_KIND_MELODIC;
    seq_slots[3].wraps_drum = true;
    melodic_track_muted[3] = true;
    track_mute_gesture(3, false);
    seq_visual_capture(21);
    assert(seq_visual_image[6] & 8);
    assert(!(seq_visual_image[8] & 8));
    track_mute_gesture(3, true);
    seq_visual_capture(21);
    assert(!(seq_visual_image[6] & 8) && (seq_visual_image[8] & 8));
    assert(melodic_track_muted[3] && drum_lane_muted[3][1]);
    track_mute_gesture(3, false);
    key(KC_SPC, true); key(KC_BSPC, true); key(KC_BSPC, false); key(KC_SPC, false);
    assert(!seq_track_muted[3] && !melodic_track_muted[3] && !drum_lane_muted[3][1]);
    /* Shift-first order also works and does not change track focus. */
    key(KC_LSFT, true); key(KC_SPC, true); key(KC_F12, true); key(KC_F12, false);
    key(KC_SPC, false); key(KC_LSFT, false);
    assert(seq_track_muted[11] && view_slot == focus);
    track_press_ms[11] = test_now; /* A recent selection must not be replayed on release. */
    key(KC_SPC, true); key(KC_LSFT, true); key(KC_F12, true); key(KC_F12, true);
    assert(!seq_track_muted[11]);
    key(KC_SPC, false); key(KC_LSFT, false); key(KC_F12, false);
    assert(view_slot == focus && !seq_track_mute_keys);
    track_mute_gesture(11, false);
    seq_transfer_active = true;
    track_mute_gesture(11, false);
    assert(seq_track_muted[11]);
    seq_transfer_active = false;
}

static void test_track_mute_playback(void) {
    reset(); seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_slots[0].wraps_drum = true;
    assert(mel_track_add_note(0, 0, 15, 60, 100));
    for (uint16_t step = 0; step < 16; step++) {
        assert(seq_drum_step_set(0, 0, step, true));
        assert(seq_drum_step_set(0, 1, step, true));
        assert(seq_drum_step_set(1, 2, step, true));
    }
    drum_lane_muted[0][1] = true;
    seq_transport_start(test_now, 0);
    assert(drum_note_gate_mask[0] == 1 && melodic_active_contains(0, 60));
    seq_ratchet_schedule_repeats(0, 0, 100, 3, 0, test_now, test_now);
    virtual_midi_clear_events();
    track_mute_gesture(0, false);
    assert(!drum_note_gate_mask[0] && !melodic_active_any(0));
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_OFF) == 2);
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) assert(!ratchet_events[i].active);
    virtual_midi_clear_events();
    seq_ratchet_process(test_now + 100);
    assert(!events_of(VIRTUAL_MIDI_EVENT_NOTE_ON));
    seq_play_step(test_now += 125);
    assert(!drum_note_gate_mask[0] && !melodic_active_any(0));
    assert(drum_note_gate_mask[1] == 4 && events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    track_mute_gesture(0, true);
    seq_play_step(test_now += 125);
    assert(drum_note_gate_mask[0] == 1 && melodic_active_contains(0, 60));
    assert(drum_lane_muted[0][1] && mel_track_event_count(0) == 1);
    /* Variation playback and chord audition obey the same gate. */
    seq_variations[0].enabled = true;
    track_mute_gesture(0, false);
    seq_play_step(test_now += 125);
    assert(!melodic_active_any(0));
    seq_chord_mode_set(2, true);
    track_mute_gesture(2, false);
    virtual_midi_clear_events();
    seq_chord_audition(2, KC_Q);
    assert(!module_preview.active && !events_of(VIRTUAL_MIDI_EVENT_NOTE_ON));
    seq_transport_stop(test_now);
}

static void test_track_mute_storage(void) {
    reset(); seq_enter(test_now);
    drum_lane_muted[0][2] = true;
    melodic_track_muted[0] = true;
    uint8_t old[SEQ_TRANSFER_CAPACITY], gated[SEQ_TRANSFER_CAPACITY];
    uint16_t old_size = sizeof(old), gated_size = sizeof(gated);
    assert(seq_project_object(1, old, &old_size, false, false));
    track_mute_gesture(0, false);
    assert(seq_project_object(1, gated, &gated_size, false, false));
    assert(gated_size == old_size + 3);
    /* With an empty project, the extension starts after 53 fixed bytes,
     * 12 empty four-byte lane records, one note count and two CC count bytes. */
    uint16_t extension = 53 + 12 * 4 + 1 + 2;
    assert(gated[extension] == 'T' && gated[extension + 1] == 1 && gated[extension + 2] == 1);
    gated[extension + 2] = 2;
    assert(!seq_project_restore_object(1, gated, gated_size));
    assert(seq_track_muted[0] && drum_lane_muted[0][2]);
    gated[extension + 2] = 1;
    seq_mute_clear_all(test_now);
    assert(seq_project_restore_object(1, gated, gated_size));
    assert(seq_track_muted[0] && melodic_track_muted[0] && drum_lane_muted[0][2]);
    seq_dirty = true; seq_storage_save_if_dirty();
    assert(test_eeprom_valid && !seq_dirty);
    seq_mute_clear_all(test_now); seq_storage_load();
    assert(seq_track_muted[0] && melodic_track_muted[0] && drum_lane_muted[0][2]);
    assert(seq_temp_begin(test_now));
    seq_mute_clear_all(test_now);
    assert(seq_temp_restore(test_now));
    assert(seq_track_muted[0] && melodic_track_muted[0] && drum_lane_muted[0][2]);
    assert(seq_project_restore_object(1, old, old_size));
    assert(!seq_track_muted[0] && melodic_track_muted[0] && drum_lane_muted[0][2]);
    track_mute_gesture(0, false); seq_exit();
    assert(seq_track_muted[0]);
}
