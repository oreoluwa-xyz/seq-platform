#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_tap_ms;
    uint16_t last_interval_ms;
    uint8_t  tap_count;
} seq_toggle_state_t;

void     seq_toggle_init(seq_toggle_state_t *state);
void     seq_toggle_reset(seq_toggle_state_t *state);
bool     seq_toggle_press(seq_toggle_state_t *state, uint32_t now, uint16_t window_ms);
uint16_t seq_toggle_last_interval(const seq_toggle_state_t *state);
