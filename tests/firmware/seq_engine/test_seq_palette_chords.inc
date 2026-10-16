static void palette_chord_setup(void) {
    reset();
    seq_enter(test_now);
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    euclid_tracks[0] = (seq_euclid_track_t){.enabled = true, .steps = 16, .fills = 4, .probability = 100, .note_count = 2, .palette_pin = 255, .pitch_mode = 2};
    euclid_tracks[0].note_pool[1] = 2;
    euclid_mode_active = true;
}
static void test_palette_chord_gestures(void) {
    palette_chord_setup();
    key(KC_2, true);
    assert(seq_palette_edit_digit == 2 && euclid_tracks[0].palette_pin == 255);
    uint16_t fills = euclid_tracks[0].fills;
    assert(seq_cc_encoder_update(0, true));
    assert(seq_modules[0].config.euclid.qualities[1] == 1);
    assert(!seq_modules[0].config.euclid.qualities[0] && euclid_tracks[0].fills == fills);
    key(KC_2, false);
    assert(euclid_tracks[0].palette_pin == 255 && seq_palette_edit_digit == 255 && !melodic_key_mask[0]);
    key(KC_2, true); key(KC_2, false);
    assert(euclid_tracks[0].palette_pin == 1);
    key(KC_0, true); key(KC_0, false);
    assert(euclid_tracks[0].palette_pin == 255);
    key(KC_0, true); key(KC_L, true); key(KC_0, false); key(KC_L, false);
    assert(seq_modules[0].config.euclid.qualities[0] == 8 && seq_modules[0].config.euclid.qualities[1] == 8);
    assert(!seq_palette_digit_held && !seq_palette_type_held && !mel_track_event_count(0));
    key(KC_1, true); key(KC_ESC, true); key(KC_ESC, false); key(KC_1, false);
    assert(euclid_tracks[0].palette_pin == 255);
    euclid_mode_active = true;
    key(KC_8, true); key(KC_8, false);
    assert(!melodic_key_mask[0] && euclid_tracks[0].palette_pin == 255);
    key(KC_1, true); key(KC_2, true); key(KC_1, false); key(KC_2, false);
    assert(euclid_tracks[0].palette_pin == 255);
    key(KC_1, true); key(KC_LCTL, true); seq_cc_encoder_update(0, true); key(KC_LCTL, false); key(KC_1, false);
    assert(euclid_tracks[0].palette_pin == 255 && bpm == 121);
    key(KC_LALT, true); key(KC_C, true); key(KC_LALT, false); key(KC_C, false);
    assert(euclid_tracks[0].pitch_mode == 0 && !mel_track_event_count(0));
    key(KC_LALT, true); key(KC_C, true); key(KC_C, false); key(KC_LALT, false);
    assert(euclid_tracks[0].pitch_mode == 2);
}
static void test_palette_chord_playback(void) {
    palette_chord_setup();
    uint8_t root = seq_melodic_note_pitch(0, 0), notes[12];
    seq_modules[0].config.euclid.qualities[0] = 1;
    seq_modules[0].config.euclid.qualities[1] = 8;
    seq_scale_global = (seq_scale_context_t){.enabled = 1, .kind = 0};
    assert(seq_melodic_euclid_fallback(0, 0, notes) == 3 && notes[1] == root + 3);
    assert(seq_melodic_euclid_fallback(0, 4, notes) == 4 && notes[0] == root + 2 && notes[1] == root + 5);
    seq_transport_start(test_now, 0);
    virtual_midi_clear_events();
    seq_emit_step(test_now, true);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 3 && melodic_active_contains(0, root + 3) && !melodic_active_contains(0, root + 4));
    seq_play_step(test_now += 125);
    assert(!melodic_active_any(0));
    seq_transport_stop(test_now);
    seq_modules[0].config.euclid.qualities[1] = 11;
    assert(seq_palette_chord_notes(0, 1, notes) == 4 && notes[3] == root + 12);
    assert(seq_palette_resolved_type(notes, 4) == 8);
    seq_scale_tracks[0] = (seq_scale_track_t){.mode = 1, .root = 2};
    assert(seq_palette_chord_notes(0, 1, notes) == 4 && notes[1] == root + 6 && notes[3] == root + 13);
    seq_scale_tracks[0].mode = 2;
    assert(seq_palette_chord_notes(0, 1, notes) == 1 && notes[0] == root + 2);
    seq_modules[0].config.euclid.qualities[1] = 8;
    euclid_tracks[0].palette_pin = 1;
    assert(mel_track_add_note(0, 0, 0, root + 9, 80));
    uint8_t projected[16] = {0};
    seq_projected_step_notes(0, 0, projected);
    assert(projected[(root + 9) >> 3] == (1u << ((root + 9) & 7)));
    memset(projected, 0, sizeof(projected));
    seq_projected_step_notes(0, 4, projected);
    assert(projected[(root + 5) >> 3] & (1u << ((root + 5) & 7)));
    seq_scale_tracks[0].mode = 0;
    seq_modules[0].config.euclid.qualities[1] = 0; /* D major contains F-sharp outside C major. */
    euclid_mode_active = true;
    assert(seq_euclid_print_loop(0, test_now));
    assert(mel_track_event_count(0) == 10 && !euclid_tracks[0].enabled);
    assert(seq_scale_tracks[0].mode == 2 && seq_scale_global.enabled && seq_scale_tracks[1].mode == 0);
    seq_transport_start(test_now, 4);
    assert(melodic_active_contains(0, root + 6) && !melodic_active_contains(0, root + 5));
    seq_transport_stop(test_now);
}
static void test_palette_chord_storage(void) {
    palette_chord_setup();
    seq_modules[0].config.euclid.qualities[0] = 16;
    seq_modules[0].config.euclid.qualities[1] = 11;
    for (uint8_t composed = 0; composed < 2; composed++) {
        seq_module_t module, decoded;
        seq_module_export(0, &module);
        module.composed = composed;
        if (composed) module.rhythm = (seq_rhythm_component_t){.kind = 1};
        uint8_t wire[192]; uint16_t size = sizeof(wire);
        assert(seq_module_encode(&module, wire, &size));
        assert(seq_module_decode(&decoded, wire, size));
        assert(decoded.config.euclid.pitch_mode == 2 && decoded.config.euclid.qualities[0] == 16 && decoded.config.euclid.qualities[1] == 11);
        for (uint16_t i = 0; i < size; i++) assert(!seq_module_decode(&decoded, wire, i));
        wire[size - 1] = 12;
        assert(!seq_module_decode(&decoded, wire, size));
    }
    uint32_t hash = project_object_hash(1);
    assert(seq_temp_begin(test_now));
    seq_modules[0].config.euclid.qualities[0] = 1;
    assert(seq_temp_restore(test_now));
    assert(project_object_hash(1) == hash);
    seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
    memset(seq_modules[0].config.euclid.qualities, 0, 8);
    seq_storage_load();
    assert(project_object_hash(1) == hash);
    assert(seq_euclid_palette_remove(0, 0, test_now));
    assert(seq_modules[0].config.euclid.qualities[0] == 11 && seq_modules[0].config.euclid.qualities[1] == 0);
    euclid_mode_active = true;
    seq_euclid_palette_capture_record(0, 4);
    seq_euclid_palette_capture_commit(test_now);
    assert(euclid_tracks[0].note_count == 2 && seq_modules[0].config.euclid.qualities[0] == 11 && !seq_modules[0].config.euclid.qualities[1]);
    seq_palette_replace[0] = true;
    seq_euclid_palette_capture_record(0, 5);
    seq_euclid_palette_capture_commit(test_now);
    assert(euclid_tracks[0].note_count == 1 && !seq_modules[0].config.euclid.qualities[0]);
}
static void test_palette_chord_protocol(void) {
    palette_chord_setup();
    uint8_t data[21] = {4, 1, 2, 0, 0, 2}; data[20] = 8;
    seq_backup_request(18, 0, 0, data, sizeof(data));
    assert(!backup_status && seq_modules[0].config.euclid.qualities[1] == 8);
    seq_backup_request(18, 0, 0, data, sizeof(data));
    assert(backup_status == 2); /* Stale qualities reject an old UI action. */
    data[13] = 8; data[20] = 10;
    seq_backup_request(18, 0, 0, data, sizeof(data)); assert(backup_status == 2);
    seq_scale_global.enabled = 1;
    seq_backup_request(18, 0, 0, data, sizeof(data)); assert(!backup_status);
    seq_backup_request(17, 0, 0, NULL, 0);
    assert(seq_visual_image[954] == 2 && seq_visual_image[955] == 2 && seq_visual_image[921] == 10 && seq_visual_image[928] == 255);
    key(KC_2, true);
    seq_backup_request(17, 0, 0, NULL, 0); assert(seq_visual_image[928] == 2);
    data[13] = 10; data[20] = 1;
    seq_backup_request(18, 0, 0, data, sizeof(data)); assert(backup_status == 2);
    key(KC_2, false);
    seq_backup_request(18, 0, 0, data, sizeof(data)); assert(!backup_status);
}
