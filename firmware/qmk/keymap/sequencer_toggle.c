#include "sequencer_toggle.h"

#ifndef SEQ_TOGGLE_WINDOW_MIN
#    define SEQ_TOGGLE_WINDOW_MIN 1u
#endif

static inline uint16_t clamp_interval(uint32_t delta) {
    if (delta > UINT16_MAX) {
        return UINT16_MAX;
    }
    return (uint16_t)delta;
}

void seq_toggle_init(seq_toggle_state_t *state) {
    if (!state) {
        return;
    }
    state->last_tap_ms      = 0;
    state->last_interval_ms = 0;
    state->tap_count        = 0;
}

void seq_toggle_reset(seq_toggle_state_t *state) {
    seq_toggle_init(state);
}

bool seq_toggle_press(seq_toggle_state_t *state, uint32_t now, uint16_t window_ms) {
    if (!state) {
        return false;
    }

    if (state->tap_count == 0) {
        state->tap_count        = 1;
        state->last_interval_ms = 0;
        state->last_tap_ms      = now;
        return false;
    }

    uint32_t delta = (uint32_t)(now - state->last_tap_ms);
    state->last_interval_ms = clamp_interval(delta);

    uint16_t window = window_ms < SEQ_TOGGLE_WINDOW_MIN ? SEQ_TOGGLE_WINDOW_MIN : window_ms;

    if (delta > window) {
        state->tap_count = 1;
        state->last_tap_ms = now;
        return false;
    }

    state->tap_count++;
    state->last_tap_ms = now;

    if (state->tap_count >= 2) {
        state->tap_count = 0;
        return true;
    }

    return false;
}

uint16_t seq_toggle_last_interval(const seq_toggle_state_t *state) {
    if (!state) {
        return 0;
    }
    return state->last_interval_ms;
}
