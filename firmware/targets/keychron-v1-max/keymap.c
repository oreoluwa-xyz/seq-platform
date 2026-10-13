// Keychron V1 Max (ANSI encoder) — Sequencer-enabled keymap wrapper
// Pull in the stock Keychron layout and layer handling, then route
// QMK hooks into the custom sequencer implementation.

#include QMK_KEYBOARD_H
#include "sequencer.h"

// Alias the default Keychron hook so we can wrap it below.
#define process_record_user default_process_record_user
#include "../default/keymap.c"
#undef process_record_user

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!seq_pre_process(keycode, record)) {
        return false;
    }
    return default_process_record_user(keycode, record);
}

void housekeeping_task_user(void) {
    seq_housekeeping();
}

void matrix_scan_user(void) {
    seq_matrix_scan();
}

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (seq_cc_encoder_update(index, clockwise)) {
        return false;
    }

    if (!seq_encoder_update(index, clockwise)) {
        return false;
    }

    if (index != 0) {
        return true;
    }

    uint8_t layer = get_highest_layer(layer_state | default_layer_state);

    switch (layer) {
        case MAC_FN:
        case WIN_FN:
            tap_code(clockwise ? RGB_VAI : RGB_VAD);
            break;
        default:
            tap_code(clockwise ? KC_VOLU : KC_VOLD);
            break;
    }

    return false;
}

#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    return seq_rgb_matrix_indicators(led_min, led_max);
}
#endif
