/* Live edits must use the song phase, including non-power-of-two loops. */
static void phase_scan_ms(unsigned ms) {
    for (unsigned i = 0; i < ms; i++) {
        test_now++;
        seq_matrix_scan();
    }
}

static void phase_tap(uint16_t code) {
    key(code, true);
    phase_scan_ms(2);
    key(code, false);
    phase_scan_ms(2);
}

static uint16_t phase_collect_bass_bar(void) {
    uint16_t hits = 0;
    for (unsigned i = 0; i < 2000; i++) {
        virtual_midi_clear_events();
        phase_scan_ms(1);
        const virtual_midi_state_t *state = virtual_midi_state();
        for (uint8_t j = 0; j < state->event_count; j++) {
            const virtual_midi_event_t *ev = &state->events[j];
            if (ev->kind == VIRTUAL_MIDI_EVENT_NOTE_ON && ev->channel == midi_channel_for_track(1))
                hits |= 1u << (seq_step_position % 16);
        }
    }
    return hits;
}

static void check_live_tab_bass(void) {
    timing_reset();
    for (uint8_t step = 0; step < 16; step += 4) add_drum(0, 0, step, 0);
    seq_transport_start(test_now, 0);
    phase_scan_ms(625);
    key(KC_TAB, true);
    phase_tap(KC_F2);
    phase_tap(KC_F2);
    key(KC_TAB, false);
    assert(view_slot == 1 && slot_uses_melodic(1));
    assert(seq_track_step_position[0] == 5 && seq_track_step_position[1] == 5);
    key(KC_Q, true);
    phase_tap(KC_D);
    phase_tap(KC_J);
    phase_tap(KC_C);
    phase_tap(KC_M);
    key(KC_Q, false);
    assert(mel_track_event_count(1) == 4);
    assert(phase_collect_bass_bar() == 0x4444);
    seq_transport_stop(test_now);
    seq_transport_start(test_now, 0);
    assert(phase_collect_bass_bar() == 0x4444);
    puts("PASS live Tab+F2 offbeat bass has the same phase before and after restart.");
}

static void phase_assert_all(void) {
    for (uint8_t track = 0; track < 12; track++)
        assert(seq_track_step_position[track] == seq_step_position % seq_track_length_steps(track));
}

static void check_live_phase_edits(void) {
    for (uint8_t external = 0; external < 2; external++) {
        timing_reset();
        seq_external_clock = external;
        seq_timing_swing = 25;
        seq_track_set_steps(2, 3);
        seq_track_set_steps(3, 5);
        seq_transport_start(test_now, 21);
        if (external) {
            seq_midi_realtime(0xF8, test_now);
            phase_scan_ms(40); /* Odd step's swing delay. */
        }
        phase_assert_all();
        seq_slot_mark_melodic(1, false);
        phase_assert_all();
        seq_slot_mark_drum(1, false);
        phase_assert_all();
        seq_slot_enable_wrapper(1);
        phase_assert_all();
        seq_clear_track(1, test_now);
        phase_assert_all();
        seq_clear_track(3, test_now); /* Five-step loop must stay at step one. */
        phase_assert_all();
        seq_track_set_bars(1, 2);
        assert(seq_track_step_position[1] == 21);
        phase_assert_all();
        seq_track_set_steps(1, 5);
        assert(seq_track_step_position[1] == 1);
        phase_assert_all();
        seq_track_set_steps(1, 3);
        phase_assert_all();
        seq_track_double_length(1, false); /* Three to six within one bar. */
        assert(seq_track_length_steps(1) == 6 && seq_track_step_position[1] == 3);
        phase_assert_all();
        for (uint8_t step = 0; step < 12; step++) {
            if (external) {
                for (uint8_t pulse = 0; pulse < 6; pulse++) {
                    phase_scan_ms(21);
                    seq_midi_realtime(0xF8, test_now);
                    seq_matrix_scan();
                }
            } else phase_scan_ms(125);
            phase_assert_all();
        }
    }
    puts("PASS conversion, clear, shrink, grow and doubling preserve shared phase with swing and internal/external clocks.");
}

static void check_live_edit_ratchet_isolation(void) {
    for (uint8_t edit = 0; edit < 3; edit++) {
        timing_reset();
        add_drum(0, 0, 0, 3);
        add_drum(1, 0, 0, 3);
        seq_transport_start(test_now, 0);
        uint32_t start = test_now;
        phase_scan_ms(5);
        virtual_midi_clear_events();
        if (edit == 0) seq_clear_track(1, test_now);
        if (edit == 1) seq_slot_mark_melodic(1, false);
        if (edit == 2) seq_track_set_steps(1, 5);
        unsigned pending[2] = {0};
        for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++)
            if (ratchet_events[i].active) pending[ratchet_events[i].channel]++;
        assert(pending[0] == 1 && pending[1] == 0);
        virtual_midi_clear_events();
        phase_scan_ms(start + 31 - test_now);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
        const virtual_midi_state_t *state = virtual_midi_state();
        for (uint8_t i = 0; i < state->event_count; i++)
            if (state->events[i].kind == VIRTUAL_MIDI_EVENT_NOTE_ON) assert(state->events[i].channel == midi_channel_for_track(0));
    }
    puts("PASS editing one track cancels only its obsolete ratchets; other tracks keep playing.");
}
