static void hold_five_step_loop(void) {
    key(KC_ENT, true);
    assert(seq_loop_held);
    for (uint8_t i = 0; i < 11; i++) assert(seq_cc_encoder_update(0, false));
    assert(seq_loop_steps == 5 && bpm == 120);
}

static uint32_t project_object_hash(uint8_t object) {
    uint8_t bytes[SEQ_TRANSFER_CAPACITY];
    uint16_t size = sizeof(bytes);
    assert(seq_project_object(object, bytes, &size, false, false));
    return seq_project_checksum(bytes, size);
}

static void test_global_loop_preservation(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    seq_chord_mode_set(0, true);
    seq_chord_event_t chord = {.root = 60, .type = 0, .length = 16, .velocity = 100};
    assert(seq_chord_write(0, 0, chord, false));
    seq_track_set_steps(1, 7);
    seq_track_set_steps(2, 32);
    seq_track_set_steps(3, 3);
    assert(seq_drum_step_set(1, 1, 6, true));
    assert(seq_drum_step_set(2, 2, 28, true));
    assert(seq_automation_record(2, 0, 28, 99));
    drum_active_note[2] = 2;
    seq_euclid_track_t *rhythm = seq_euclid_selected(2, true);
    assert(rhythm);
    rhythm->enabled = true;
    rhythm->steps = 13;
    rhythm->fills = 5;
    uint32_t hashes[13];
    for (uint8_t i = 0; i <= 12; i++) hashes[i] = project_object_hash(i);
    bool dirty = seq_dirty;
    unsigned writes = test_eeprom_writes;
    hold_five_step_loop();
    assert(seq_track_playback_length(0) == 5 && seq_track_playback_length(1) == 5);
    assert(seq_track_playback_length(2) == 5 && seq_track_playback_length(3) == 3);
    for (uint8_t i = 0; i <= 12; i++) assert(hashes[i] == project_object_hash(i));
    assert(seq_dirty == dirty && test_eeprom_writes == writes);
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(!backup_status && (seq_visual_image[638] & 128));
    assert(seq_visual_image[902] == 5 && seq_visual_image[903] == 0);
    assert(seq_visual_image[12] == 16); /* Authored length stays in the live model. */
    key(KC_ENT, false);
    assert(!seq_loop_steps && !seq_loop_held);
    assert(seq_track_playback_length(1) == 7 && seq_track_playback_length(2) == 32);
    for (uint8_t i = 0; i <= 12; i++) assert(hashes[i] == project_object_hash(i));
    assert(seq_dirty == dirty && test_eeprom_writes == writes);
}

static void test_global_loop_playback(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
    assert(mel_track_add_note(0, 0, 15, 60, 100));
    assert(mel_track_add_note(1, 6, 12, 67, 100));
    assert(seq_drum_step_set(2, 0, 4, true));
    seq_drum_step_mod_t *mod = seq_drum_step_mod_ensure(2, 0, 4);
    assert(mod);
    seq_drum_mod_set_ratchet(mod, 2);
    assert(seq_automation_record(3, 0, 4, 90));
    assert(seq_automation_record(3, 0, 12, 40));
    hold_five_step_loop();
    seq_transport_start(test_now, 0);
    for (uint8_t i = 1; i <= 12; i++) {
        seq_play_step(test_now += 125);
        assert(seq_step_position == i);
        for (uint8_t t = 0; t < 12; t++) assert(seq_track_step_position[t] == i % 5);
        assert(!melodic_active_any(1));
        if (i >= 4) assert(cc_values[3][0] == 90);
    }
    assert(melodic_active_any(0));
    seq_play_step(test_now += 125); /* Global 13. */
    seq_play_step(test_now += 125); /* Global 14, local 4: ratchets pending. */
    bool pending = false;
    for (uint8_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) pending |= ratchet_events[i].active;
    assert(pending);
    key(KC_ENT, false);
    assert(seq_step_position == 14 && seq_state.running && !melodic_active_any(0));
    for (uint8_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) assert(!ratchet_events[i].active);
    for (uint8_t t = 0; t < 12; t++) assert(seq_track_step_position[t] == 14);
    seq_play_step(test_now += 125);
    assert(melodic_active_any(0)); /* Sustained note rejoins without waiting for its onset. */
    assert(mel_track_event_count(0) == 1 && mel_track_event_count(1) == 1);
    assert(seq_drum_step_get(2, 0, 4) && seq_automation_track_count(3) == 2);
    seq_transport_stop(test_now);
}

static void test_global_loop_external(void) {
    reset();
    seq_enter(test_now);
    seq_track_set_steps(1, 7);
    seq_external_clock = true;
    hold_five_step_loop();
    seq_midi_realtime(0xFA, test_now);
    for (uint8_t i = 0; i <= 42; i++) {
        if (i) test_now += 20;
        seq_midi_realtime(0xF8, test_now);
        seq_matrix_scan();
    }
    assert(seq_step_position == 7 && seq_track_step_position[0] == 2 && seq_track_step_position[1] == 2);
    key(KC_ENT, false);
    assert(seq_step_position == 7 && seq_track_step_position[0] == 7 && seq_track_step_position[1] == 0);
    assert(seq_state.running && seq_external_clock && bpm == 120);
    hold_five_step_loop();
    seq_midi_realtime(0xFC, test_now);
    assert(!seq_state.running && !seq_loop_steps && !seq_loop_held);
    key(KC_ENT, false);
}

static void test_global_loop_gestures(void) {
    reset();
    key(KC_ENT, true);
    assert(!seq_loop_held);
    key(KC_ENT, false);
    seq_enter(test_now);
    record_state.armed = true;
    key(KC_ENT, true);
    assert(!seq_loop_held);
    key(KC_ENT, false);
    record_state.armed = false;
    seq_transfer_active = true;
    key(KC_ENT, true);
    assert(!seq_loop_held);
    key(KC_ENT, false);
    seq_transfer_active = false;
    seq_scale_editing = true;
    key(KC_ENT, true);
    assert(!seq_loop_held && !seq_scale_editing); /* Enter still applies the scale. */
    key(KC_ENT, false);
    key(KC_SPC, true);
    key(KC_ENT, true);
    assert(!seq_loop_held && !seq_loop_steps); /* Space+Enter remains on-board save. */
    key(KC_ENT, false);
    key(KC_SPC, false);
    key(KC_BSLS, true);
    key(KC_BSLS, false); /* Latched CC view still allows the global loop. */
    hold_five_step_loop();
    uint8_t cc = cc_values[0][0];
    assert(seq_cc_encoder_update(0, false) && seq_loop_steps == 4 && cc_values[0][0] == cc);
    key(KC_LCTL, true);
    assert(seq_cc_encoder_update(0, true) && bpm == 121 && seq_loop_steps == 4);
    key(KC_LCTL, false);
    key(KC_ENT, false);
    bpm = 120;
    key(KC_BSLS, true);
    key(KC_BSLS, false);
    hold_five_step_loop();
    key(KC_ESC, true); /* Enter+Esc retains its existing TEMP gesture. */
    assert(!seq_loop_steps && !seq_loop_held && seq_temp_active);
    key(KC_ESC, false);
    key(KC_ENT, false);
    uint16_t snapshot = seq_temp_length;
    hold_five_step_loop();
    key(KC_ENT, false);
    assert(seq_temp_active && seq_temp_length == snapshot);
    key(KC_ESC, true);
    key(KC_ESC, false);
    assert(!seq_temp_active);
    hold_five_step_loop();
    seq_backup_request(1, 0, 0, NULL, 0);
    assert(!backup_status && !seq_loop_steps && !seq_loop_held && seq_transfer_active);
    key(KC_ENT, false);
    seq_backup_request(7, 0, 0, NULL, 0);
    hold_five_step_loop();
    for (uint8_t i = 0; i < 10; i++) seq_encoder_update(0, false);
    assert(seq_loop_steps == 1);
    for (uint8_t i = 0; i < 20; i++) seq_encoder_update(0, true);
    assert(seq_loop_steps == 16);
    seq_exit();
    assert(!seq_loop_steps && !seq_loop_held);
    key(KC_ENT, false);
}
