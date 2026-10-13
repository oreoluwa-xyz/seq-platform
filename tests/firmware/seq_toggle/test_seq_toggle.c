#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "sequencer_toggle.h"

static void test_initial_press_does_not_toggle(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    assert(!seq_toggle_press(&state, 1000u, 300u));
    assert(seq_toggle_last_interval(&state) == 0u);
}

static void test_double_tap_within_window(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    assert(!seq_toggle_press(&state, 1000u, 300u));
    assert(seq_toggle_press(&state, 1200u, 300u));
    assert(seq_toggle_last_interval(&state) == 200u);
}

static void test_double_tap_outside_window(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    assert(!seq_toggle_press(&state, 1000u, 300u));
    assert(!seq_toggle_press(&state, 1500u, 300u));
    assert(seq_toggle_last_interval(&state) == 500u);
    assert(seq_toggle_press(&state, 1800u, 300u));
    assert(!seq_toggle_press(&state, 2050u, 300u));
}

static void test_wraparound_interval(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    uint32_t first  = 0xFFFFFF80u;
    uint32_t second = first + 200u;

    assert(!seq_toggle_press(&state, first, 300u));
    assert(seq_toggle_press(&state, second, 300u));
    assert(seq_toggle_last_interval(&state) == 200u);
}

static void test_reset(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    assert(!seq_toggle_press(&state, 100u, 300u));
    seq_toggle_reset(&state);
    assert(!seq_toggle_press(&state, 200u, 300u));
    assert(!seq_toggle_press(&state, 601u, 300u));
    assert(seq_toggle_press(&state, 850u, 300u));
    assert(!seq_toggle_press(&state, 1100u, 300u));
}

static void test_interval_saturation(void) {
    seq_toggle_state_t state;
    seq_toggle_init(&state);

    assert(!seq_toggle_press(&state, 0u, 300u));
    assert(!seq_toggle_press(&state, 200000u, 300u));
    assert(seq_toggle_last_interval(&state) == UINT16_MAX);
}

int main(void) {
    test_initial_press_does_not_toggle();
    test_double_tap_within_window();
    test_double_tap_outside_window();
    test_wraparound_interval();
    test_reset();
    test_interval_saturation();

    puts("seq_toggle tests passed");
    return 0;
}
