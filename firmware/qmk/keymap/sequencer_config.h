#pragma once

#include <stdint.h>

#define SEQ_TRACK_COUNT 12
#define SEQ_DRUM_NOTE_COUNT 12

#ifndef SEQ_DRUM_LANE_POOL_CAPACITY
#    define SEQ_DRUM_LANE_POOL_CAPACITY 48
#endif

#define TRACK_1 0
#define TRACK_2 1
#define TRACK_3 2
#define TRACK_4 3
#define TRACK_5 4
#define TRACK_6 5
#define TRACK_7 6
#define TRACK_8 7
#define TRACK_9 8
#define TRACK_10 9
#define TRACK_11 10
#define TRACK_12 11

#ifndef SEQ_STEPS_PER_BAR
#    define SEQ_STEPS_PER_BAR 16
#endif

#ifndef SEQ_MAX_BARS
#    define SEQ_MAX_BARS 128
#endif

#ifndef SEQ_MAX_STEPS
#    define SEQ_MAX_STEPS (SEQ_STEPS_PER_BAR * SEQ_MAX_BARS)
#endif

#ifndef SEQ_DEFAULT_TRACK
#    define SEQ_DEFAULT_TRACK TRACK_1
#endif

#ifndef SEQ_CC_MAPPING_COUNT
#    define SEQ_CC_MAPPING_COUNT 16
#endif

typedef struct {
    uint16_t keycode;
    uint8_t  controller;
    uint8_t  channel; /* 0xFF => derive from track */
    uint8_t  default_value;
} seq_cc_mapping_t;

#ifndef SEQ_CC_MAP_DEFAULT
#    define SEQ_CC_MAP_DEFAULT {{KC_1, 16, 0xFF, 64}, {KC_2, 17, 0xFF, 64}, {KC_3, 18, 0xFF, 64}, {KC_4, 19, 0xFF, 64}, {KC_5, 20, 0xFF, 64}, {KC_6, 21, 0xFF, 64}, {KC_7, 22, 0xFF, 64}, {KC_8, 23, 0xFF, 64}, {KC_9, 24, 0xFF, 64}, {KC_0, 25, 0xFF, 64}, {KC_MINS, 26, 0xFF, 64}, {KC_EQL, 27, 0xFF, 64}, {KC_Q, 28, 0xFF, 64}, {KC_W, 29, 0xFF, 64}, {KC_E, 30, 0xFF, 64}, {KC_R, 31, 0xFF, 64}}
#endif

#ifndef SEQ_STEP_HOLD_MS
#    define SEQ_STEP_HOLD_MS 250
#endif
