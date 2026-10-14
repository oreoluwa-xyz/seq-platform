/* Real-engine regressions for shared timing, ratchets and external sync. */
#define main seq_existing_test_main
#pragma clang diagnostic push
/* The renamed existing main relies on main's implicit return of zero. */
#pragma clang diagnostic ignored "-Wreturn-type"
#include "test_seq_engine.c"
#pragma clang diagnostic pop
#undef main

static void timing_reset(void) {
    reset();
    seq_enter(test_now);
    virtual_midi_clear_events();
    seq_step_duration_ms = 125;
}

static void add_drum(uint8_t track, uint8_t lane, uint16_t step, uint8_t repeats) {
    assert(seq_drum_step_set(track, lane, step, true));
    if (repeats) {
        seq_drum_step_mod_t *mod = seq_drum_step_mod_ensure(track, lane, step);
        assert(mod);
        seq_drum_mod_set_ratchet(mod, repeats);
    }
}

static void check_grid(void) {
    timing_reset();
    bpm = 137;
    for (uint8_t track = 0; track < 12; track++) {
        track_length_steps[track] = track + 1;
        for (uint16_t step = 0; step < track_length_steps[track]; step++)
            for (uint8_t lane = 0; lane < 4; lane++) add_drum(track, lane, step, 0);
    }
    uint32_t start = test_now;
    seq_transport_start(start, 0);
    unsigned boundaries = 1;
    for (uint32_t elapsed = 1; elapsed <= 60000; elapsed++) {
        test_now = start + elapsed;
        uint32_t previous = seq_step_position;
        virtual_midi_clear_events();
        seq_matrix_scan();
        if (previous == seq_step_position) continue;
        boundaries++;
        assert(elapsed == (uint32_t)((uint64_t)seq_step_position * 15000u / bpm));
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 48);
        for (uint8_t track = 0; track < 12; track++)
            assert(seq_track_step_position[track] == seq_step_position % track_length_steps[track]);
        const virtual_midi_state_t *state = virtual_midi_state();
        for (uint8_t i = 0; i < state->event_count; i++)
            if (state->events[i].kind == VIRTUAL_MIDI_EVENT_NOTE_ON) assert(state->events[i].timestamp_ms == test_now);
    }
    assert(boundaries == 549);
    puts("PASS shared grid: 12 tracks / 48 lanes, different loop lengths, 137 BPM, 60 seconds; no logical drift.");
}

static void check_ratchet_capacity(void) {
    timing_reset();
    for (uint8_t track = 0; track < 12; track++)
        for (uint8_t lane = 0; lane < 4; lane++) add_drum(track, lane, 0, 3);
    seq_transport_start(test_now, 0);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 48);
    unsigned emitted[12] = {0};
    uint32_t start = test_now;
    for (uint8_t repeat = 1; repeat <= 3; repeat++) {
        virtual_midi_clear_events();
        test_now = start + (uint32_t)repeat * 125u / 4u;
        seq_matrix_scan();
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 48);
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_OFF) == 48);
        const virtual_midi_state_t *state = virtual_midi_state();
        for (uint8_t i = 0; i < state->event_count; i++) {
            if (state->events[i].kind != VIRTUAL_MIDI_EVENT_NOTE_ON) continue;
            for (uint8_t track = 0; track < 12; track++)
                if (state->events[i].channel == midi_channel_for_track(track)) emitted[track]++;
        }
    }
    for (uint8_t track = 0; track < 12; track++) assert(emitted[track] == 12);
    puts("PASS all 48 lanes receive all three ratchets, with balanced note-off/note-on retriggers.");
}

static void check_late_scan(void) {
    timing_reset();
    for (uint16_t step = 0; step < 16; step++) add_drum(0, 0, step, 1);
    uint32_t start = test_now;
    seq_transport_start(start, 0);
    test_now = start + 62;
    seq_matrix_scan();
    test_now = start + 145; /* Step deadline was 125. */
    seq_matrix_scan();
    assert(ratchet_events[0].active && ratchet_events[0].trigger_ms == start + 187);
    assert(seq_next_step_ms == start + 250);
    puts("PASS late step keeps repeat at 187 ms and next step at 250 ms.");

    timing_reset();
    for (uint16_t step = 0; step < 16; step++) add_drum(0, 0, step, 0);
    start = test_now;
    seq_transport_start(start, 0);
    virtual_midi_clear_events();
    test_now = start + 500;
    seq_matrix_scan();
    assert(seq_step_position == 4 && events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    const virtual_midi_state_t *state = virtual_midi_state();
    for (uint8_t i = 0; i < state->event_count; i++)
        if (state->events[i].kind == VIRTUAL_MIDI_EVENT_NOTE_ON) assert(state->events[i].timestamp_ms == start + 500);
    assert(events_of(VIRTUAL_MIDI_EVENT_CLOCK_HIGH) <= 1);
    assert(events_of(VIRTUAL_MIDI_EVENT_SONG_POSITION) == 1);
    assert(events_of(VIRTUAL_MIDI_EVENT_CONTINUE) == 1);
    puts("PASS stalled scan skips expired steps without note or clock bursts.");
}

static void check_swing(void) {
    timing_reset();
    seq_timing_swing = 50;
    add_drum(0, 0, 1, 3);
    uint32_t start = test_now;
    seq_transport_start(start, 0);
    test_now = start + 187;
    seq_matrix_scan();
    assert(seq_next_step_ms == start + 250);
    assert(ratchet_events[0].active && ratchet_events[0].span_ms == 63);
    for (uint8_t repeat = 1; repeat <= 3; repeat++) {
        virtual_midi_clear_events();
        test_now = start + 187 + (uint32_t)repeat * 63 / 4;
        seq_matrix_scan();
        assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    }
    assert(test_now == start + 234);
    puts("PASS swing subdivisions remain inside the short step (last repeat at 234, next step at 250 ms).");
}

static void check_wrap(void) {
    timing_reset();
    uint32_t start = UINT32_MAX - 40;
    seq_ratchet_schedule_repeats(0, 0, 100, 1, SEQ_DRUM_RATCHET_SPACING_STEPS, start, start);
    assert(ratchet_events[0].active && ratchet_events[0].trigger_ms - start == 62);
    puts("PASS ratchet deadline crosses 32-bit rollover with the correct 62 ms delay.");
}

static void check_external_batch(void) {
    timing_reset();
    seq_external_clock = true;
    seq_midi_realtime(0xFA, test_now);
    seq_midi_realtime(0xF8, test_now);
    seq_matrix_scan();
    test_now += 125;
    for (uint8_t pulse = 0; pulse < 6; pulse++) seq_midi_realtime(0xF8, test_now);
    seq_matrix_scan();
    assert(seq_external_step_interval == 125 && seq_track_step_position[0] == 1);
    seq_ratchet_schedule_repeats(0, 0, 100, 1, SEQ_DRUM_RATCHET_SPACING_STEPS, test_now, test_now);
    assert(ratchet_events[0].trigger_ms - test_now == 62);
    puts("PASS batched MIDI clock retains 125 ms steps and 62 ms half-step repeats.");
}

static void check_external_swing(void) {
    timing_reset();
    seq_external_clock = true;
    seq_timing_swing = 50;
    add_drum(0, 0, 0, 3);
    add_drum(0, 0, 1, 3);
    uint32_t start = test_now;
    seq_midi_realtime(0xFA, start);
    for (uint32_t elapsed = 0; elapsed <= 250; elapsed++) {
        test_now = start + elapsed;
        /* Deliver six-clock batches, like a host that coalesces arrivals. */
        if (!elapsed) seq_midi_realtime(0xF8, test_now);
        if (elapsed == 125 || elapsed == 250)
            for (uint8_t i = 0; i < 6; i++) seq_midi_realtime(0xF8, test_now);
        virtual_midi_clear_events();
        seq_matrix_scan();
        if (elapsed == 125) assert(seq_track_step_position[0] == 0);
        if (elapsed == 187) {
            assert(seq_track_step_position[0] == 1);
            assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
        }
        if (elapsed == 250) assert(seq_track_step_position[0] == 2);
    }
    puts("PASS external swing delays odd steps while even steps remain on the incoming clock.");
}

static void check_late_repeats(void) {
    timing_reset();
    add_drum(0, 0, 0, 3);
    uint32_t start = test_now;
    seq_transport_start(start, 0);
    virtual_midi_clear_events();
    test_now = start + 100;
    seq_matrix_scan();
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    virtual_midi_clear_events();
    test_now = start + 125;
    seq_matrix_scan();
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 0);
    timing_reset();
    add_drum(0, 0, 1, 3);
    start = test_now;
    seq_transport_start(start, 0);
    virtual_midi_clear_events();
    test_now = start + 225;
    seq_matrix_scan();
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) assert(!ratchet_events[i].active);
    puts("PASS overdue repeats coalesce and cannot leak across the next step boundary.");
}

static void check_external_multistep_batch(void) {
    timing_reset();
    seq_external_clock = true;
    seq_timing_swing = 50;
    for (uint8_t step = 0; step < 3; step++) add_drum(0, 0, step, 3);
    seq_midi_realtime(0xFA, test_now);
    seq_midi_realtime(0xF8, test_now);
    seq_matrix_scan();
    test_now += 250;
    for (uint8_t i = 0; i < 12; i++) seq_midi_realtime(0xF8, test_now);
    virtual_midi_clear_events();
    seq_matrix_scan();
    assert(seq_external_step_interval == 125);
    assert(seq_track_step_position[0] == 2 && !seq_external_pending);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    assert(seq_step_duration_ms == 187);
    puts("PASS multi-step external batches preserve tempo and cannot block an even step behind a delayed odd step.");
}

static void check_tempo_swing_matrix(void) {
    const uint16_t tempos[] = {10, 137, 400};
    const uint8_t swings[] = {0, 25, 50};
    for (unsigned tempo = 0; tempo < ARRAY_SIZE(tempos); tempo++) {
        for (unsigned swing = 0; swing < ARRAY_SIZE(swings); swing++) {
            timing_reset();
            bpm = tempos[tempo];
            seq_timing_swing = swings[swing];
            /* Cross rollover while six drum and six melodic tracks run. */
            test_now = UINT32_MAX - 500;
            for (uint8_t track = 0; track < 12; track++) {
                track_length_steps[track] = track + 1;
                if (track & 1u) seq_slots[track].kind = SEQ_SLOT_KIND_MELODIC;
                for (uint8_t step = 0; step <= track; step++) {
                    if (track & 1u) assert(mel_track_add_note(track, step, step, 60, 100));
                    else add_drum(track, 0, step, 3);
                }
            }
            uint32_t start = test_now;
            seq_transport_start(start, 0);
            unsigned repeats = 0;
            uint32_t pair_span = 30000u / bpm;
            for (uint32_t elapsed = 1; elapsed <= pair_span; elapsed++) {
                uint32_t previous = seq_step_position;
                test_now = start + elapsed;
                virtual_midi_clear_events();
                seq_matrix_scan();
                if (seq_step_position != previous) {
                    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 12);
                    for (uint8_t track = 0; track < 12; track++)
                        assert(seq_track_step_position[track] == seq_step_position % (track + 1u));
                } else {
                    unsigned on = events_of(VIRTUAL_MIDI_EVENT_NOTE_ON);
                    assert(on == 0 || on == 6);
                    repeats += on;
                }
            }
            assert(seq_step_position == 2 && repeats == 36);
        }
    }
    puts("PASS mixed drum/melodic polymeter and every ratchet at 10/137/400 BPM, 0/25/50% swing, across timer rollover.");
}

static void check_long_stall_state(void) {
    timing_reset();
    bpm = 400;
    track_length_steps[0] = 256;
    seq_slots[0].kind = SEQ_SLOT_KIND_MELODIC;
    assert(mel_track_add_note(0, 128, 132, 60, 100));
    assert(seq_automation_record(0, 0, 1, 10));
    assert(seq_automation_record(0, 0, 2, 20));
    assert(seq_automation_record(0, 0, 3, 30));
    assert(seq_automation_record(0, 1, 3, 40));
    assert(seq_automation_record(0, 1, 129, 45));
    uint32_t start = test_now;
    seq_transport_start(start, 0);
    virtual_midi_clear_events();
    test_now = start + 15000u * 129u / bpm;
    seq_matrix_scan(); /* The bounded catch-up pass stops after 128 steps. */
    assert(seq_step_position == 128);
    assert(!events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) && !events_of(VIRTUAL_MIDI_EVENT_CC));
    seq_matrix_scan();
    assert(seq_step_position == 129);
    assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
    assert(events_of(VIRTUAL_MIDI_EVENT_CC) == 2);
    assert(cc_values[0][0] == 30 && cc_values[0][1] == 45);
    assert(!seq_resume_after_skip && !seq_cc_pending[0]);
    puts("PASS bounded long-stall recovery restores sustained notes and coalesces CC to the latest values.");
}

#include "test_seq_live_phase.inc"

static void check_temp_bar_restore(void) {
    for (uint8_t external = 0; external < 2; external++) {
        timing_reset();
        seq_external_clock = external;
        seq_timing_swing = external ? 0 : 50;
        track_length_steps[0] = 7; /* The boundary follows global time, not this track. */
        add_drum(1, 0, 0, 0);
        seq_transport_start(test_now, 0);
        if (external) { seq_midi_realtime(0xF8, test_now); seq_matrix_scan(); }
        assert(seq_step_position == 0 && seq_temp_begin(test_now));
        drum_lane_muted[1][0] = true;
        queue_temp_restore();
        for (uint8_t step = 1; step <= 32; step++) {
            virtual_midi_clear_events();
            if (external) {
                test_now += 125;
                for (uint8_t i = 0; i < 6; i++) seq_midi_realtime(0xF8, test_now);
            } else test_now = seq_next_step_ms;
            seq_matrix_scan();
            assert(seq_step_position == step && seq_state.running);
            if (step < 32) {
                assert(seq_temp_active && drum_lane_muted[1][0]);
                assert(seq_temp_restore_pending == (step <= 8 || step > 24));
                assert(!events_of(VIRTUAL_MIDI_EVENT_NOTE_ON));
                if (step == 8 || step == 24) queue_temp_restore(); /* Cancel, cross a bar, then requeue. */
            } else {
                assert(!seq_temp_active && !seq_temp_restore_pending && !drum_lane_muted[1][0]);
                assert(events_of(VIRTUAL_MIDI_EVENT_NOTE_ON) == 1);
                assert(seq_track_step_position[0] == 4 && seq_track_step_position[1] == 0);
            }
        }
    }
    timing_reset(); add_drum(0, 0, 0, 0); seq_transport_start(test_now, 0);
    assert(seq_temp_begin(test_now)); drum_lane_muted[0][0] = true; queue_temp_restore();
    virtual_midi_clear_events(); test_now += 2125; seq_matrix_scan();
    assert(seq_step_position == 17 && !seq_temp_active && !seq_temp_restore_pending);
    assert(!events_of(VIRTUAL_MIDI_EVENT_NOTE_ON)); /* No replay of a missed downbeat. */
    puts("PASS TEMP queue cancellation extends the buildup; requeue restores on the next bar under internal/external clocks, swing and polymeter. Skipped steps do not replay notes.");
}

int main(void) {
    check_temp_bar_restore();
    check_live_tab_bass();
    check_live_phase_edits();
    check_live_edit_ratchet_isolation();
    check_grid();
    check_ratchet_capacity();
    check_late_scan();
    check_swing();
    check_wrap();
    check_external_batch();
    check_external_swing();
    check_late_repeats();
    check_external_multistep_batch();
    check_tempo_swing_matrix();
    check_long_stall_state();
    puts("Timing regressions passed.");
}
