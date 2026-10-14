#pragma once

// Double-tap window for PgUp/Esc to toggle sequencer mode (in ms)
#define SEQ_DOUBLE_TAP_MS 300

// Fresh firmware/settings: light pressed keys, then fade them out.
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_SOLID_REACTIVE_SIMPLE

// If your board uses RGB Matrix, you can optionally cap brightness for clarity
// #define RGB_MATRIX_MAXIMUM_BRIGHTNESS 180

// Good general default for tap timing (not required since we don't use Tap Dance here)
// #define TAPPING_TERM 200

// Keep wireless stack happy; no USB/power tweaks here.

#ifndef SEQ_SWITCH_KEY
#    define SEQ_SWITCH_KEY KC_SPC
#endif

#ifndef SEQ_DEBUG_LOGGING
#    define SEQ_DEBUG_LOGGING 1
#endif

#ifndef SEQ_MIDI_TRANSPORT_SEND
#    define SEQ_MIDI_TRANSPORT_SEND 1
#endif

#ifndef EECONFIG_USER_DATA_SIZE
#    define EECONFIG_USER_DATA_SIZE 3072
#endif
#ifndef EECONFIG_USER_DATA_VERSION
#    define EECONFIG_USER_DATA_VERSION 0x53455134u
#endif

#undef WEAR_LEVELING_BACKING_SIZE
#define WEAR_LEVELING_BACKING_SIZE 8192
#undef WEAR_LEVELING_LOGICAL_SIZE
#define WEAR_LEVELING_LOGICAL_SIZE 4096

// Keep the diagnostic history small on hardware; host tests retain full traces.
#if defined(MIDI_ENABLE) && defined(PROTOCOL_CHIBIOS)
#    define VIRTUAL_MIDI_MAX_EVENTS 16
#endif
