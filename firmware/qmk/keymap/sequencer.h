#pragma once

#include "action.h"
#include "quantum.h"

enum sequencer_keycodes {
    KC_BPM_DN = SAFE_RANGE,
    KC_BPM_UP,
    SEQ_SAFE_RANGE_END,
};

bool seq_is_active(void);
void seq_mode_on_load(void);
void seq_set_default_track(uint8_t track);
void seq_housekeeping(void);
void seq_matrix_scan(void);
bool seq_encoder_update(uint8_t index, bool clockwise);
bool seq_pre_process(uint16_t keycode, keyrecord_t *record);
bool seq_rgb_matrix_indicators(uint8_t led_min, uint8_t led_max);
bool seq_cc_encoder_update(uint8_t index, bool clockwise);
