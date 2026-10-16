static void variation_setup(void) {
    reset(); seq_enter(test_now); seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    euclid_tracks[0].enabled = false;
    euclid_tracks[0].note_count = 3;
    euclid_tracks[0].note_pool[0] = 0; euclid_tracks[0].note_pool[1] = 2; euclid_tracks[0].note_pool[2] = 7;
    seq_modules[0].composed = true; seq_modules[0].rhythm.kind = 1;
}
static void variation_chord(uint16_t step, uint16_t end, uint8_t root, uint8_t voices) {
    const uint8_t intervals[] = {0, 3, 7, 10};
    for (uint8_t i = 0; i < voices; i++) assert(mel_track_add_note(0, step, end, root + intervals[i], 100));
}
static void test_accumulator_chords(void) {
    variation_setup(); seq_chord_mode[0] = true;
    variation_chord(0, 2, 60, 3); variation_chord(4, 6, 62, 4);
    seq_accumulators[0].enabled = true;
    seq_transport_start(test_now, 0);
    assert(seq_accumulators[0].index == 1);
    assert(melodic_active_contains(0, 60) && melodic_active_contains(0, 63) && melodic_active_contains(0, 67));
    seq_play_step(test_now += 125); assert(seq_accumulators[0].index == 1 && melodic_active_contains(0, 63));
    seq_play_step(test_now += 125); seq_play_step(test_now += 125);
    assert(!melodic_active_any(0));
    seq_play_step(test_now += 125);
    assert(seq_accumulators[0].index == 2 && melodic_active_contains(0, 64) && melodic_active_contains(0, 67) && melodic_active_contains(0, 71) && melodic_active_contains(0, 74));
    seq_transport_stop(test_now); assert(!melodic_active_any(0));
    /* A high chord shifts only as far as every voice fits, without clipping. */
    mel_track_reset(0); variation_chord(0, 0, 117, 4); seq_accumulators[0].index = 2;
    seq_track_step_position[0] = 0; seq_variation_emit(0, test_now, true);
    assert(melodic_active_contains(0, 117) && melodic_active_contains(0, 127));
    /* Euclidean chord palettes advance once per successful hit, including random runs. */
    palette_chord_setup(); seq_accumulators[0].enabled = true; seq_accumulators[0].random = true; seq_accumulators[0].run_length = 2;
    euclid_tracks[0].palette_pin = 0;
    seq_transport_start(test_now, 0); assert(seq_accumulators[0].run_count == 1 && seq_accumulators[0].index == 1);
    for (uint8_t i = 0; i < 4; i++) seq_play_step(test_now += 125);
    uint8_t root = seq_melodic_note_pitch(0, 0);
    assert(!seq_accumulators[0].run_count && !seq_accumulators[0].index);
    assert(melodic_active_contains(0, root + 2) && melodic_active_contains(0, root + 6) && melodic_active_contains(0, root + 9));
    euclid_tracks[0].probability = 0;
    for (uint8_t i = 0; i < 4; i++) seq_play_step(test_now += 125);
    assert(!seq_accumulators[0].index && !melodic_active_any(0));
}
static void test_variation_groups(void) {
    variation_setup(); variation_chord(0, 1, 60, 3); variation_chord(4, 6, 65, 4);
    uint32_t hash = project_object_hash(1);
    seq_variations[0] = (seq_variation_t){.enabled = 1, .locked = 1, .order = 1};
    seq_transport_start(test_now, 0);
    assert(melodic_active_contains(0, 65) && melodic_active_contains(0, 68) && melodic_active_contains(0, 72) && melodic_active_contains(0, 75));
    seq_play_step(test_now += 125); assert(melodic_active_contains(0, 75));
    seq_play_step(test_now += 125); assert(!melodic_active_any(0));
    seq_play_step(test_now += 125); seq_play_step(test_now += 125);
    assert(melodic_active_contains(0, 60) && melodic_active_contains(0, 63) && melodic_active_contains(0, 67) && !melodic_active_contains(0, 75));
    seq_transport_stop(test_now);
    seq_variations[0] = (seq_variation_t){0}; assert(project_object_hash(1) == hash);
    /* Original order retains unequal per-voice releases. */
    mel_track_reset(0); assert(mel_track_add_note(0, 0, 0, 60, 90)); assert(mel_track_add_note(0, 0, 3, 64, 90));
    seq_variations[0].enabled = true; seq_transport_start(test_now, 0); seq_play_step(test_now += 125);
    assert(!melodic_active_contains(0, 60) && melodic_active_contains(0, 64));
    seq_transport_stop(test_now);
    /* Shuffles are a permutation; random selection can repeat. */
    seq_variation_t v = {.enabled = 1, .order = 3};
    for (uint8_t n = 1; n <= 128; n++) {
        uint8_t seen[16] = {0};
        for (uint8_t i = 0; i < n; i++) { uint8_t picked = seq_variation_select(&v, n, i, 12345); assert(picked < n && !(seen[picked >> 3] & (1u << (picked & 7)))); seen[picked >> 3] |= 1u << (picked & 7); }
    }
    v.order = 1; assert(seq_variation_select(&v, 5, 0, 0) == 4);
    v.order = 2; v.rotate = 2; assert(seq_variation_select(&v, 5, 4, 0) == 1);
    v.order = 4; assert(seq_variation_select(&v, 5, 11, 8) == seq_variation_select(&v, 5, 11, 8));
}
static void variation_request(uint8_t action, uint8_t scope, bool queued, const seq_variation_t *value) {
    uint8_t data[16] = {action, scope, queued, seq_variation_revision & 255, seq_variation_revision >> 8};
    if (value) seq_variation_pack(value, data + 5);
    seq_backup_request(27, 0, 0, data, value ? 16 : 5);
}
static void test_variation_controls_storage(void) {
    variation_setup(); seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
    variation_chord(0, 3, 60, 3);
    seq_variation_t v = {.seed = 1234, .enabled = 1, .locked = 1, .order = 3, .mutation = 50, .range = 7, .voicing = 4};
    variation_request(0, 1, false, &v); assert(!backup_status && seq_variations[0].seed == 1234 && seq_variations[1].seed == 1234 && !seq_variations[2].enabled);
    uint8_t stale[] = {3, 0, 0, 0, 0}; seq_backup_request(27, 0, 0, stale, sizeof(stale)); assert(backup_status == 2 && seq_variations[0].enabled);
    uint32_t hash = project_object_hash(1);
    seq_dirty = true; seq_storage_save_if_dirty(); assert(!seq_dirty);
    seq_variations[0] = (seq_variation_t){0}; seq_storage_load(); assert(project_object_hash(1) == hash);
    assert(seq_temp_begin(test_now)); variation_request(1, 0, false, NULL); assert(!backup_status && seq_variations[0].seed != v.seed);
    seq_transport_start(test_now, 1); variation_request(3, 1, true, NULL); assert(seq_variation_pending == 3);
    assert(seq_temp_restore(test_now)); assert(!seq_variation_pending && project_object_hash(1) == hash);
    variation_request(3, 1, true, NULL); assert(seq_variation_pending == 3 && seq_variations[0].enabled);
    seq_step_position = 15; seq_first_step = false; seq_play_step(test_now += 125);
    assert(!seq_variation_pending && !seq_variations[0].enabled && !seq_variations[1].enabled);
    variation_request(0, 1, true, &v); variation_request(4, 0, false, NULL);
    assert(seq_variation_pending == 2); /* Cancelling one track cannot apply another's queue. */
    variation_request(0, 0, false, &v); assert(seq_variation_pending == 2);
    seq_transport_stop(test_now); assert(!seq_variation_pending && seq_variations[1].seed == v.seed);
    seq_backup_request(26, 0, 0, NULL, 0); assert(!backup_status && backup_size == 32 && backup_data[31]);
    /* Lock captures the currently heard effective seed. */
    seq_variations[0].locked = false; seq_step_position = 48;
    uint32_t seed = seq_variation_seed(0); variation_request(2, 0, false, NULL);
    assert(seq_variations[0].locked && seq_variation_seed(0) == seed);
    seq_step_position += 16; assert(seq_variation_seed(0) == seed);
    uint8_t saved[SEQ_TRANSFER_CAPACITY]; uint16_t size = sizeof(saved);
    assert(seq_project_object(1, saved, &size, false, false));
    uint8_t wire[11]; seq_variation_pack(&seq_variations[0], wire);
    uint16_t extension = 0;
    for (uint16_t i = 0; i + 13 <= size; i++) if (saved[i] == 'V' && saved[i + 1] == 1 && !memcmp(saved + i + 2, wire, 11)) extension = i;
    assert(extension);
    for (uint16_t i = extension + 1; i < extension + 13; i++) { uint16_t short_size = i; assert(!seq_project_object(1, saved, &short_size, true, false)); }
    saved[extension + 6] = 255; hash = project_object_hash(1);
    assert(!seq_project_restore_object(1, saved, size) && project_object_hash(1) == hash);
}
static void test_variation_mutation_projection(void) {
    variation_setup(); variation_chord(0, 3, 60, 3);
    seq_variations[0] = (seq_variation_t){.seed = 42, .enabled = 1, .locked = 1, .mutation = 100, .range = 7, .voicing = 1};
    uint8_t output[128] = {0}, attacks[16] = {0}, projected[16] = {0};
    seq_variation_frame(0, 0, true, true, output, attacks);
    seq_variation_project(0, 0, projected);
    for (uint8_t i = 0; i < 128; i++) assert(!!output[i] == !!(projected[i >> 3] & (1u << (i & 7))));
    seq_scale_global = (seq_scale_context_t){.enabled = 1};
    for (uint32_t random = 0; random < 10000; random += 137) {
        int16_t delta = seq_variation_mutation(0, 60, random);
        assert(seq_scale_contains(60 + delta, 0, 0));
    }
    seq_scale_global.enabled = false;
    for (uint32_t random = 0; random < 10000; random += 137) {
        int16_t note = 60 + seq_variation_mutation(0, 60, random);
        uint8_t base = seq_melodic_note_pitch(0, euclid_tracks[0].palette_root);
        assert(note == 60 || note == base || note == base + 2 || note == base + 7);
    }
    seq_accumulators[0].enabled = true; seq_accumulators[0].index = 2;
    uint32_t hash = project_object_hash(1);
    seq_backup_request(17, 0, 0, NULL, 0); seq_backup_request(26, 0, 0, NULL, 0);
    assert(seq_accumulators[0].index == 2 && project_object_hash(1) == hash);
}
static void test_variation_track_scope_clock(void) {
    for (uint8_t external = 0; external < 2; external++) {
        variation_setup(); seq_slots[1].kind = SEQ_SLOT_KIND_MELODIC;
        assert(mel_track_add_note(0, 0, 15, 60, 100));
        assert(mel_track_add_note(1, 0, 15, 72, 100));
        seq_external_clock = external;
        seq_transport_start(test_now, 0);
        if (external) { seq_midi_realtime(0xF8, test_now); seq_matrix_scan(); }
        seq_variation_t v = {.enabled = 1, .locked = 1, .order = 1};
        variation_request(0, 0, false, &v);
        virtual_midi_clear_events();
        seq_play_step(test_now += 125);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1 && melodic_active_contains(1, 72));
        variation_request(3, 0, true, NULL); assert(seq_variation_pending == 1);
        if (!external) {
            test_now += 2200; seq_matrix_scan(); /* Catch-up crosses the queued boundary. */
        } else {
            for (uint8_t i = 0; i < 100; i++) { test_now += 21; seq_midi_realtime(0xF8, test_now); seq_matrix_scan(); }
        }
        assert(!seq_variation_pending && !seq_variations[0].enabled);
        seq_transport_stop(test_now);
    }
    variation_setup(); seq_variations[0] = (seq_variation_t){.enabled = 1, .seed = 42};
    seq_loop_steps = 5; seq_step_position = 4; uint32_t before = seq_variation_seed(0);
    seq_step_position = 5; assert(seq_variation_seed(0) != before);
}
