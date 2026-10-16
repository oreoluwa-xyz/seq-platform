#include "sequencer.h"
#include "sequencer_config.h"
#include "melodic_sequencer.h"
#include "virtual_midi.h"
#include "sequencer_toggle.h"
#include "drum_lane_pool.h"
#include "seq_automation.h"
#include "seq_module.h"
#include "seq_scale.h"
#include "seq_chord.h"
#include "seq_firmware_identity.h"
static seq_scale_context_t seq_scale_global, seq_scale_pending;
static seq_scale_track_t seq_scale_tracks[12];
static bool seq_scale_editing;
static uint16_t seq_scale_revision;
static uint8_t seq_ctrl_mask;
static uint8_t seq_harmony_request;
static bool seq_harmony_shortcut_held;

#include "eeconfig.h"
#include "eeprom.h"
#include "timer.h"
#include "progmem.h"
#if defined(SEQ_DEBUG_LOGGING) && (SEQ_DEBUG_LOGGING)
#    ifdef CONSOLE_ENABLE
#        include "print.h"
#        define SEQ_LOG(...) uprintf(__VA_ARGS__)
#    else
#        define SEQ_LOG(...) \
            do {             \
            } while (0)
#    endif
#else
#    define SEQ_LOG(...) \
        do {             \
        } while (0)
#endif

#ifndef SEQ_FEATURE_RGB
#    define SEQ_FEATURE_RGB 1
#endif
#ifndef SEQ_FEATURE_EUCLID
#    define SEQ_FEATURE_EUCLID 1
#endif
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifndef KC_MCTL
#    ifdef KC_MCTRL
#        define KC_MCTL KC_MCTRL
#    endif
#endif

#ifndef KC_LPAD
#    ifdef KC_LNPAD
#        define KC_LPAD KC_LNPAD
#    endif
#endif

#ifndef SEQ_TRACK_HOLD_MS
#    define SEQ_TRACK_HOLD_MS 250
#endif

#ifndef SEQ_DOUBLE_TAP_MS
#    define SEQ_DOUBLE_TAP_MS 300
#endif

#ifndef SEQ_TRACK_LENGTH_CAPTURE_DELAY_MS
#    define SEQ_TRACK_LENGTH_CAPTURE_DELAY_MS 220
#endif

#ifndef SEQ_TRACK_STEP_MODE_KEY
#    define SEQ_TRACK_STEP_MODE_KEY KC_BSPC
#endif

#ifndef SEQ_CLEAR_MUTES_KEY
#    define SEQ_CLEAR_MUTES_KEY KC_BSPC
#endif

#ifndef SEQ_CC_MODE_KEY
#    define SEQ_CC_MODE_KEY KC_BSLS
#endif

#ifndef SEQ_STEPS_PER_BAR
#    define SEQ_STEPS_PER_BAR 16
#endif

#ifndef SEQ_MAX_BARS
#    define SEQ_MAX_BARS 128
#endif

#ifndef SEQ_MAX_STEPS
#    define SEQ_MAX_STEPS (SEQ_STEPS_PER_BAR * SEQ_MAX_BARS)
#endif

static inline void seq_mark_dirty(void);

#ifndef SEQ_MELODIC_KEY_COUNT
#    define SEQ_MELODIC_KEY_COUNT 12
#endif

extern const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS];

/*
 * Feature ideas backlog (in priority-neutral order):
 *  - Chord palette stamps mapped to the number row for fast harmonic sketching.
 *  - Step length / swing macros that resize a held step via numeric shortcuts.
 *  - Probability & ratcheting per lane to spice up repetitive patterns.
 */

#ifndef SEQ_DRUM_NOTE_BASE
#    define SEQ_DRUM_NOTE_BASE 36
#endif
#ifndef SEQ_DRUM_VELOCITY
#    define SEQ_DRUM_VELOCITY 110
#endif

#ifndef SEQ_EUCLID_NOTE_POOL_MAX
#    define SEQ_EUCLID_NOTE_POOL_MAX 8
#endif

typedef enum {
    SEQ_LAYOUT_DEFAULT = 0,
    SEQ_LAYOUT_GRID_4X4,
} seq_layout_mode_t;

static inline uint8_t clamp_note_index(uint8_t note) {
    return (uint8_t)(note % SEQ_DRUM_NOTE_COUNT);
}

static inline uint8_t first_note_from_mask(uint16_t mask, uint8_t fallback) {
    uint16_t filtered = mask & ((uint16_t)((1u << SEQ_DRUM_NOTE_COUNT) - 1));
    if (!filtered) {
        return clamp_note_index(fallback);
    }
#if defined(__GNUC__)
    return (uint8_t)__builtin_ctz(filtered);
#else
    for (uint8_t i = 0; i < SEQ_DRUM_NOTE_COUNT; i++) {
        if (filtered & (1u << i)) {
            return i;
        }
    }
    return clamp_note_index(fallback);
#endif
}

typedef enum {
    SEQ_SLOT_KIND_DRUM,
    SEQ_SLOT_KIND_MELODIC,
    SEQ_SLOT_KIND_CHORD, /* Request kind only; storage remains melodic. */
} seq_slot_kind_t;

typedef struct {
    seq_slot_kind_t kind;
    bool            wraps_drum;
    uint8_t         drum_track;
    uint8_t         melodic_track;
} seq_slot_t;

typedef struct {
    bool     active;
    uint8_t  slot;
    uint8_t  track;
    uint16_t chord_mask;
    uint16_t start_absolute;
    uint32_t activated_ms;
} seq_chord_capture_t;

#define SEQ_RATCHET_EVENT_CAP SEQ_DRUM_LANE_POOL_CAPACITY

typedef struct {
    bool     active;
    uint8_t  channel;
    uint8_t  note;
    uint8_t  velocity;
    uint32_t trigger_ms;
    uint16_t span_ms;
    uint8_t  index;
    uint8_t  divisions;
} seq_ratchet_event_t;

static bool         drum_lane_muted[12][SEQ_DRUM_NOTE_COUNT];
static bool         drum_pool_full_preview;
static uint32_t     drum_pool_full_ms;
/* The first tap deletes a step; a second tap must restore its editor values. */
static seq_drum_step_mod_t drum_deleted_mod[12];
static uint8_t             drum_deleted_mod_note[12];

static void seq_drum_pool_report_full(void) {
    drum_pool_full_preview = true;
    drum_pool_full_ms      = timer_read32();
}

void seq_melodic_capacity_exceeded(void) {
    seq_drum_pool_report_full();
}

static bool seq_drum_reserve(uint8_t track, uint16_t notes) {
    if (!seq_drum_pool_reserve(track, notes)) {
        seq_drum_pool_report_full();
        return false;
    }
    return true;
}

static bool seq_drum_can_reserve(uint8_t track, uint16_t notes) {
    if (!seq_drum_pool_can_reserve(track, notes)) {
        seq_drum_pool_report_full();
        return false;
    }
    return true;
}
static seq_ratchet_event_t ratchet_events[SEQ_RATCHET_EVENT_CAP];
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
static uint8_t      option_leds[2];
static uint8_t      cmd_leds[2];
static uint8_t      record_led = NO_LED;
#endif
static seq_chord_capture_t chord_capture = {0};
static uint32_t            chord_last_tap_ms = 0;
static uint16_t            chord_last_step   = 0;
static uint8_t             chord_last_track  = 0xFF;
static uint16_t            chord_last_mask   = 0;

typedef struct {
    bool     armed;
    bool     capturing;
    bool     is_melodic;
    bool     flush_requested;
    uint8_t  capture_track;
    uint32_t start_step;
    uint16_t start_local;
    uint16_t held_mask;
    uint16_t capture_mask;
} seq_record_state_t;

static seq_record_state_t record_state = {0};

static seq_slot_t      seq_slots[12];
static bool         view_child_active = false;
static uint8_t      view_child_slot   = 0;
static uint16_t     mel_last_start_mask = 0;
static uint16_t     mel_last_stop_mask  = 0;
static uint16_t     melodic_gate_mask   = 0;
static uint8_t      melodic_active_map[MEL_TRACK_COUNT][16];
static uint8_t      view_slot           = 0;
static uint8_t      drum_active_note[12];
static uint16_t     drum_note_gate_mask[12];
static uint16_t     note_hold_mask = 0;
static uint16_t     melodic_key_mask[MEL_TRACK_COUNT];
static uint16_t     melodic_last_mask[MEL_TRACK_COUNT];
static bool         melodic_tie_pending[MEL_TRACK_COUNT];
static uint8_t      melodic_last_semitone[MEL_TRACK_COUNT];
static bool         melodic_track_muted[MEL_TRACK_COUNT];
static uint32_t     track_last_tap_ms[12];
static uint32_t     track_press_ms[12];
static uint8_t      track_tap_count[12];
static uint32_t     track_tab_last_tap_ms[12];
static uint8_t      track_tab_tap_count[12];
static uint32_t     track_caps_last_tap_ms[12];
static uint8_t      track_caps_tap_count[12];
static bool         track_held[12];
static uint8_t      seq_modifiers    = 0;
static int8_t       melodic_octave_shift[MEL_TRACK_COUNT];
static int8_t       melodic_hold_offset[MEL_TRACK_COUNT];
static bool         melodic_rest_pending[MEL_TRACK_COUNT];



typedef struct {
    bool    active;
    bool    primed;
    uint8_t track;
    int8_t  origin;
    uint8_t count;
    int8_t  values[SEQ_EUCLID_NOTE_POOL_MAX];
    uint8_t retained;
    bool replace, full;
} seq_euclid_palette_capture_t;

static void seq_euclid_reset_track(uint8_t track);
static void seq_euclid_sync_track(uint8_t track);
static void seq_euclid_enable(uint8_t track, bool enabled);
static void seq_euclid_adjust_steps(uint8_t track, int16_t delta);
static void seq_euclid_adjust_fills(uint8_t track, int16_t delta);
static void seq_euclid_adjust_rotate(uint8_t track, int16_t delta);
static void seq_euclid_adjust_swing(uint8_t track, int16_t delta);
static void seq_euclid_capture_window_template(uint32_t now);
static bool seq_euclid_step_hit(uint8_t track, uint16_t step, uint16_t *hit_index_out, int8_t *pool_value_out);
static bool seq_euclid_any_enabled(void);

static seq_euclid_palette_capture_t euclid_palette_capture = {0};
static uint16_t seq_lane_selection_used;
static uint8_t seq_palette_edit_digit = 255, seq_palette_edit_track;
static bool seq_palette_edit_used;
static uint16_t seq_palette_digit_held, seq_palette_type_held;
static bool seq_palette_toggle_held;
static bool seq_palette_key(uint16_t key, bool pressed);
static void seq_palette_set_quality(uint8_t track, uint8_t digit, uint8_t type);
static bool seq_palette_encoder(uint8_t index, bool clockwise);
static void seq_palette_cancel(void) { seq_palette_edit_digit = 255; }
static bool seq_palette_replace[12]; /* Capture preference; Add notes on startup. */

typedef struct {
    bool     active;
    uint8_t  mapping_index;
    bool     temporary;
    uint8_t  prev_value;
    uint8_t  track;
} seq_cc_session_t;

typedef struct {
    bool     held;
    bool     consumed;
    bool     temporary;
    bool     latched;
    bool     latched_temporary;
    bool     pending_track_valid;
    bool     simple_toggle;
    uint8_t  snapshot_track;
    uint8_t  pending_track;
    uint32_t press_time;
    uint32_t last_tap_ms;
    uint8_t  tap_count;
} seq_cc_mode_t;

typedef struct {
    bool active;
    uint8_t track;
    uint8_t prev_track;
    bool track_override;
    bool hold_override_active;
    uint8_t hold_track;
} seq_cc_context_t;

static const seq_cc_mapping_t PROGMEM cc_mappings[SEQ_CC_MAPPING_COUNT] = SEQ_CC_MAP_DEFAULT;
static uint8_t                cc_values[12][SEQ_CC_MAPPING_COUNT];
static uint16_t               seq_cc_pending[12];
static bool                   cc_automation_manual[12];
static uint8_t                cc_snapshot[12][SEQ_CC_MAPPING_COUNT];
static bool                   cc_key_pressed[12][SEQ_CC_MAPPING_COUNT];
static uint32_t               cc_key_last_release[12][SEQ_CC_MAPPING_COUNT];
static seq_cc_session_t       cc_session  = {0};
static seq_cc_mode_t          cc_mode     = {0};
static seq_cc_context_t       cc_context  = {0};
static seq_module_t                   seq_modules[SEQ_TRACK_COUNT];
static struct {
    bool     active;
    uint8_t  track, channel, count, notes[SEQ_CHORD_VOICES];
    uint16_t key;
} module_preview;
static void                   seq_module_preview_stop(uint32_t now);
static bool                   seq_module_preview_owns(uint8_t track, uint8_t note);
static seq_euclid_track_t     euclid_tracks[12];
static uint8_t                drum_track_velocity[12];
static uint8_t                melodic_track_velocity[MEL_TRACK_COUNT];
static uint8_t                track_midi_channel[12];
static uint8_t                drum_note_map[SEQ_DRUM_NOTE_COUNT];
#define SEQ_STEP_EDIT_HISTORY_SLOTS 12
static uint32_t               step_last_tap_info[SEQ_STEP_EDIT_HISTORY_SLOTS];
typedef struct {
    bool    enabled;
    bool    random;
    uint8_t index;
    uint8_t run_length;
    uint8_t run_count;
    int8_t  direction;
} seq_accumulator_state_t;
static seq_accumulator_state_t seq_accumulators[12];
/* Persistent playback transforms; originals remain available for reset. */
typedef struct {
    uint32_t seed;
    uint8_t order, rotate, mutation, range, voicing, enabled, locked;
} seq_variation_t;
static seq_variation_t seq_variations[12], seq_variation_queued[12];
static uint16_t seq_variation_pending, seq_variation_revision, seq_variation_playing, seq_variation_rejoin;
static void seq_variation_apply_pending(uint32_t now);
static void seq_variation_apply_mask(uint16_t mask, uint32_t now);
static void seq_variation_project(uint8_t track, uint16_t step, uint8_t *notes);

/* Normal playback maps base to output. Grouped playback reuses this as an
 * encoded onset-offset cache; 0xFF is unset in either representation. */
static uint8_t seq_accumulator_active_notes[12][128];

static void seq_accumulator_reset_runtime(uint8_t track);
static void seq_accumulator_clear_active(uint8_t track);

static void seq_accumulator_reset_runtime(uint8_t track) {
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    acc->index     = 0;
    acc->run_count = 0;
}

static int8_t seq_accumulator_next(uint8_t track) {
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    if (!acc->enabled) {
        return 0;
    }

    seq_euclid_track_t *euclid = &euclid_tracks[track % 12];
    uint8_t count = euclid->note_count;
    if (count == 0) {
        count = 1;
    }

    uint8_t current_index = acc->index;
    if (current_index >= count) {
        current_index = 0;
    }

    int8_t offset = 0;
    if (euclid->note_count > 0) {
        offset = euclid->note_pool[current_index % euclid->note_count];
    }

    uint8_t next_index = current_index;
    if (count > 1) {
        if (acc->random) {
            uint8_t candidate = current_index;
            while (candidate == current_index) {
                candidate = (uint8_t)(rand() % count);
            }
            next_index = candidate;
        } else {
            int16_t candidate = (int16_t)current_index + (acc->direction >= 0 ? 1 : -1);
            while (candidate < 0) {
                candidate += count;
            }
            next_index = (uint8_t)(candidate % count);
        }
    } else {
        next_index = 0;
    }

    acc->index = next_index;

    if (acc->run_length) {
        acc->run_count++;
        if (acc->run_count >= acc->run_length) {
            acc->run_count = 0;
            acc->index     = 0;
        }
    }

    return offset;
}

static void seq_accumulator_toggle(uint8_t track) {
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    acc->enabled = !acc->enabled;
    if (acc->direction == 0) {
        acc->direction = 1;
    }
    seq_accumulator_reset_runtime(track);
    seq_variation_revision++;
    seq_mark_dirty();
}

static void seq_accumulator_set_direction(uint8_t track, int8_t direction) {
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    acc->direction = (direction >= 0) ? 1 : -1;
    seq_accumulator_reset_runtime(track);
    seq_variation_revision++;
    seq_mark_dirty();
}

static void seq_accumulator_cycle_run_length(uint8_t track) {
    static const uint8_t options[] = {0, 2, 4};
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    uint8_t current = acc->run_length;
    uint8_t option_count = (uint8_t)(sizeof(options) / sizeof(options[0]));
    uint8_t next_index   = 0;
    for (uint8_t i = 0; i < option_count; i++) {
        if (options[i] == current) {
            next_index = (uint8_t)((i + 1u) % option_count);
            break;
        }
    }
    acc->run_length = options[next_index];
    seq_accumulator_reset_runtime(track);
    seq_variation_revision++;
    seq_mark_dirty();
}

static void seq_accumulator_toggle_random(uint8_t track) {
    seq_accumulator_state_t *acc = &seq_accumulators[track % 12];
    acc->random = !acc->random;
    seq_accumulator_reset_runtime(track);
    seq_variation_revision++;
    seq_mark_dirty();
}

static void seq_accumulator_register_output(uint8_t track, uint8_t base, uint8_t output) {
    seq_accumulator_active_notes[track % 12][base] = output;
}

static uint8_t seq_accumulator_release_output(uint8_t track, uint8_t base, uint8_t fallback) {
    uint8_t output                                 = seq_accumulator_active_notes[track % 12][base];
    seq_accumulator_active_notes[track % 12][base] = 0xFF;
    return output == 0xFF ? fallback : output;
}

static bool seq_accumulator_lookup_output(uint8_t track, uint8_t base, uint8_t *output) {
    uint8_t value = seq_accumulator_active_notes[track % 12][base];
    if (value == 0xFF) return false;
    if (output) *output = value;
    return true;
}

static void seq_accumulator_clear_active(uint8_t track) {
    memset(seq_accumulator_active_notes[track % 12], 0xFF, 128);
}
static bool                seq_cc_block_record = false;

static bool         seq_dirty          = false;
static bool         seq_loading        = false;

static inline void seq_mark_dirty(void) {
    if (!seq_loading) {
        seq_dirty = true;
    }
}

void seq_melodic_notify_changed(void) {
    seq_mark_dirty();
}

static void seq_cc_automation_clear_track(uint8_t track);
static void seq_cc_automation_trim(uint8_t track, uint16_t max_step);
static void seq_cc_automation_record(uint8_t track, uint8_t index, uint16_t step, uint8_t value);
static void seq_cc_automation_apply(uint8_t track, uint16_t step, uint32_t now);
static void seq_cc_set_value(uint8_t track, uint8_t index, uint8_t value, uint32_t now);

typedef enum {
    EUCLID_PARAM_KIND_NONE = 0,
    EUCLID_PARAM_KIND_FILLS,
    EUCLID_PARAM_KIND_ROTATE,
    EUCLID_PARAM_KIND_STEPS,
    EUCLID_PARAM_KIND_SWING,
    EUCLID_PARAM_KIND_PROBABILITY,
    EUCLID_PARAM_KIND_RATCHET_THRESHOLD,
} seq_euclid_param_kind_t;

typedef enum {
    EUCLID_PARAM_MODE_NONE = 0,
    EUCLID_PARAM_MODE_FILLS,
    EUCLID_PARAM_MODE_ROTATE,
    EUCLID_PARAM_MODE_STEPS,
    EUCLID_PARAM_MODE_PROBABILITY,
    EUCLID_PARAM_MODE_ACCENT,
    EUCLID_PARAM_MODE_RATCHET_THRESHOLD,
} seq_euclid_param_mode_t;

static inline bool    seq_euclid_editing_enabled(void);
static void           seq_euclid_capture_preview(uint8_t track, seq_euclid_param_kind_t kind, uint16_t value, uint32_t now);
static uint16_t       seq_euclid_param_value_for_kind(uint8_t track, seq_euclid_param_kind_t kind);

static bool                    euclid_mode_active            = false;
static seq_euclid_param_mode_t euclid_param_mode             = EUCLID_PARAM_MODE_NONE;
static uint16_t                euclid_param_keycode          = KC_NO;
static seq_euclid_param_kind_t euclid_param_preview_kind     = EUCLID_PARAM_KIND_NONE;
static uint16_t                euclid_param_preview_value    = 0;
static uint32_t                euclid_param_preview_expires  = 0;
static bool                    velocity_preview_active       = false;
static uint8_t                 velocity_preview_track        = 0;
static bool                    velocity_preview_is_melodic   = false;
static uint16_t                velocity_preview_value        = 0;
static uint32_t                velocity_preview_expires      = 0;
static bool                    bpm_preview_active            = false;
static uint32_t                bpm_preview_expires           = 0;
static inline void             seq_track_velocity_preview(uint8_t track, bool is_melodic, uint16_t value, uint32_t now);
static inline void             seq_bpm_preview_arm(uint32_t now);

typedef struct {
    bool held;
    bool consumed;
    uint8_t edit_track;
    uint32_t last_tap_ms;
    uint8_t  tap_count;
} seq_cmd_state_t;

typedef struct {
    bool     active;
    uint8_t  track;
} seq_euclid_edit_state_t;

typedef enum {
    STEP_EDIT_PARAM_NONE    = 0,
    STEP_EDIT_PARAM_LENGTH  = 1,
    STEP_EDIT_PARAM_PITCH   = 2,
    STEP_EDIT_PARAM_VELOCITY = 3,
    STEP_EDIT_PARAM_RATCHET  = 4,
    STEP_EDIT_PARAM_RATCHET_SPACING = 5,
} seq_step_edit_param_t;

typedef struct {
    bool     active;
    bool     is_drum;
    uint8_t  track;
    uint16_t absolute_step;
    uint16_t local_step;
    uint8_t  param;
    uint8_t  event_index;
    uint8_t  note; /* for drum */
    uint8_t  pitch;
    uint8_t  velocity;
    uint16_t start_step;
    uint16_t end_step;
    uint8_t  velocity_mode;
    uint8_t  ratchet;
    uint8_t  spacing;
    uint32_t last_input_ms;
} seq_step_edit_state_t;

typedef struct {
    bool            active;
    uint8_t         track;
    seq_slot_kind_t target_kind;
    bool            switch_after;
} seq_track_confirm_t;

typedef enum {
    SEQ_SWITCH_STAGE_INACTIVE = 0,
    SEQ_SWITCH_STAGE_ROOT,
    SEQ_SWITCH_STAGE_SEQUENCER,
    SEQ_SWITCH_STAGE_COMMITTED,
} seq_switch_stage_t;

typedef struct {
    bool               held;
    seq_switch_stage_t stage;
    bool               consumed;
    bool               synthetic;
} seq_switch_state_t;

static bool     seq_pending_erase;
static uint8_t  seq_pending_erase_track;
static uint16_t seq_pending_erase_step;
static uint32_t seq_pending_erase_deadline;
static void     seq_flush_pending_erase(void) {
    if (seq_pending_erase) {
        mel_track_remove_step_all(seq_pending_erase_track, seq_pending_erase_step);
        seq_pending_erase = false;
    }
}
static seq_track_confirm_t track_confirm;
static seq_switch_state_t  switch_state = {0};
static seq_layout_mode_t   current_layout = SEQ_LAYOUT_DEFAULT;
static seq_cmd_state_t     cmd_state     = {0};
static seq_euclid_edit_state_t euclid_edit = {0};
static seq_step_edit_state_t   step_edit   = {0};
static struct {
    bool active, editing, tap;
    uint8_t slot;
    uint16_t key, step;
    uint32_t started;
} seq_step_hold;
static bool seq_step_hold_replaying;
static void seq_step_hold_begin(void);
static void seq_step_hold_finish(bool tap);


static void seq_step_edit_reset(void);
static bool seq_step_edit_handle_digit(uint8_t digit, bool pressed);
static bool seq_step_edit_try_begin(uint8_t slot_index, uint16_t absolute_step, bool allow_without_shift, uint32_t now);
static bool seq_step_edit_encoder_delta(int8_t delta);
static void seq_step_edit_refresh_highlight(void);

static inline uint8_t seq_drum_mod_velocity(const seq_drum_step_mod_t *mod) {
    return (uint8_t)(mod->config & 0x03u);
}

static inline uint8_t seq_drum_mod_ratchet(const seq_drum_step_mod_t *mod) {
    return (uint8_t)((mod->config >> 2) & 0x03u);
}

static inline void seq_drum_mod_set_velocity(seq_drum_step_mod_t *mod, uint8_t value) {
    uint8_t prev = mod->config;
    mod->config  = (uint8_t)((mod->config & ~0x03u) | (value & 0x03u));
    if (mod->config != prev) {
        seq_mark_dirty();
    }
}

static inline void seq_drum_mod_set_ratchet(seq_drum_step_mod_t *mod, uint8_t value) {
    uint8_t prev = mod->config;
    mod->config  = (uint8_t)((mod->config & ~(0x03u << 2)) | ((value & 0x03u) << 2));
    if (mod->config != prev) {
        seq_mark_dirty();
    }
}

static inline uint8_t seq_drum_mod_spacing_raw(const seq_drum_step_mod_t *mod) {
    if (!mod) {
        return SEQ_DRUM_RATCHET_SPACING_DEFAULT;
    }
    uint8_t raw = mod->spacing;
    if (raw >= SEQ_DRUM_RATCHET_SPACING_STEPS) {
        raw = SEQ_DRUM_RATCHET_SPACING_DEFAULT;
    }
    return raw;
}

static inline uint8_t seq_drum_mod_spacing_units(const seq_drum_step_mod_t *mod) {
    return (uint8_t)(seq_drum_mod_spacing_raw(mod) + 1u);
}

static inline void seq_drum_mod_set_spacing(seq_drum_step_mod_t *mod, uint8_t value) {
    if (!mod) {
        return;
    }
    if (value >= SEQ_DRUM_RATCHET_SPACING_STEPS) {
        value = (uint8_t)(SEQ_DRUM_RATCHET_SPACING_STEPS - 1u);
    }
    if (mod->spacing != value) {
        mod->spacing = value;
        seq_mark_dirty();
    }
}

static inline void seq_drum_mod_reset_spacing(seq_drum_step_mod_t *mod) {
    if (mod) {
        if (mod->spacing != SEQ_DRUM_RATCHET_SPACING_DEFAULT) {
            mod->spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT;
            seq_mark_dirty();
        }
    }
}

static inline bool seq_drum_mod_spacing_is_default(const seq_drum_step_mod_t *mod) {
    return seq_drum_mod_spacing_raw(mod) == SEQ_DRUM_RATCHET_SPACING_DEFAULT;
}

static seq_drum_step_mod_t *seq_drum_step_mod_lookup(uint8_t track, uint8_t note, uint16_t step_local) {
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track % 12, note % SEQ_DRUM_NOTE_COUNT);
    if (!lane) {
        return NULL;
    }
    seq_drum_step_mod_t *mods = lane->mods;
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        if (mods[i].step == step_local) {
            return &mods[i];
        }
    }
    seq_drum_pool_report_full();
    return NULL;
}

static seq_drum_step_mod_t *seq_drum_step_mod_ensure(uint8_t track, uint8_t note, uint16_t step_local) {
    track %= 12;
    note %= SEQ_DRUM_NOTE_COUNT;
    if (!seq_drum_reserve(track, (uint16_t)(1u << note))) {
        return NULL;
    }
    seq_drum_step_mod_t *mods = seq_drum_pool_lane(track, note)->mods;
    seq_drum_step_mod_t *slot = seq_drum_step_mod_lookup(track, note, step_local);
    if (slot) {
        return slot;
    }
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        if (mods[i].step == 0xFFFF) {
            mods[i].step   = step_local;
            mods[i].config = 0;
            mods[i].spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT;
            return &mods[i];
        }
    }
    return NULL;
}

static void seq_drum_step_mod_prune(uint8_t track, uint8_t note, uint16_t step_local) {
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track % 12, note % SEQ_DRUM_NOTE_COUNT);
    if (!lane) {
        return;
    }
    seq_drum_step_mod_t *mods = lane->mods;
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        if (mods[i].step == step_local) {
            if (seq_drum_mod_velocity(&mods[i]) == 0 && seq_drum_mod_ratchet(&mods[i]) == 0 && seq_drum_mod_spacing_is_default(&mods[i])) {
                mods[i].step   = 0xFFFF;
                mods[i].config = 0;
                seq_drum_mod_reset_spacing(&mods[i]);
            }
        }
    }
    seq_drum_pool_prune(track % 12, note % SEQ_DRUM_NOTE_COUNT);
}

static void     seq_track_ensure_steps(uint8_t slot, uint16_t steps);
static uint16_t seq_track_length_steps(uint8_t slot);
static bool     slot_has_activity(uint8_t slot);
static uint8_t  seq_melodic_note_pitch(uint8_t track, uint8_t semitone);
static void     seq_cc_exit_mode(uint32_t now);
static bool     seq_track_confirmation_active(void);
static void     seq_track_cancel_confirmation(void);
static void     seq_bar_nav_reset(void);
static bool     seq_track_length_cancel_all(void);
static void     seq_mute_pending_reset(void);
static void     seq_ratchet_clear(void);
static void     seq_ratchet_clear_track(uint8_t track);
static void     seq_midi_stop_drum_track(uint8_t track, uint32_t now);
static void     seq_ratchet_process(uint32_t now);
static void     seq_ratchet_schedule_repeats(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t repeats, uint8_t spacing_units, uint32_t now, uint32_t emitted_ms);

typedef struct {
    bool     active;
    uint8_t  digit;
    uint8_t  taps;
    uint32_t last_press_ms;
} seq_bar_nav_state_t;

typedef struct {
    bool     active;
    uint8_t  track;
    uint32_t timestamp_ms;
} seq_mute_pending_t;

static seq_bar_nav_state_t bar_nav_state = {0};
static seq_mute_pending_t  mute_pending  = {0};

static inline void seq_chord_tap_reset(void) {
    chord_last_track  = 0xFF;
    chord_last_step   = 0;
    chord_last_mask   = 0;
    chord_last_tap_ms = 0;
}

static inline void seq_chord_capture_clear(void) {
    chord_capture = (seq_chord_capture_t){0};
}

static inline void seq_chord_record_tap(uint8_t track, uint16_t step, uint16_t mask, uint32_t now) {
    chord_last_track  = track;
    chord_last_step   = step;
    chord_last_mask   = mask;
    chord_last_tap_ms = now;
}

static inline bool seq_chord_is_double_tap(uint8_t track, uint16_t step, uint16_t mask, uint32_t now) {
    if (chord_last_track == 0xFF) {
        return false;
    }
    if (chord_last_track != track) {
        return false;
    }
    if (chord_last_step != step) {
        return false;
    }
    if (chord_last_mask != mask) {
        return false;
    }
    if (TIMER_DIFF_32(now, chord_last_tap_ms) > SEQ_DOUBLE_TAP_MS) {
        return false;
    }
    return true;
}

static void seq_chord_capture_begin(uint8_t slot, uint8_t track, uint16_t absolute_step, uint16_t chord_mask, uint32_t now) {
    chord_capture.active         = true;
    chord_capture.slot           = slot % 12;
    chord_capture.track          = track % MEL_TRACK_COUNT;
    chord_capture.chord_mask     = chord_mask;
    chord_capture.start_absolute = absolute_step;
    chord_capture.activated_ms   = now;
}

static void seq_chord_capture_cancel(void) {
    seq_chord_capture_clear();
    seq_chord_tap_reset();
}

static bool seq_chord_capture_commit(uint8_t slot, uint16_t absolute_step) {
    if (!chord_capture.active) {
        return false;
    }

    if ((slot % 12) != chord_capture.slot) {
        seq_chord_capture_clear();
        return false;
    }

    uint16_t chord_mask = chord_capture.chord_mask;
    if (!chord_mask) {
        seq_chord_capture_clear();
        return false;
    }

    uint8_t track = chord_capture.track % MEL_TRACK_COUNT;
    uint16_t start_abs = chord_capture.start_absolute;
    uint16_t end_abs   = absolute_step;
    if (end_abs < start_abs) {
        uint16_t tmp = start_abs;
        start_abs    = end_abs;
        end_abs      = tmp;
    }

    seq_track_ensure_steps(slot, (uint16_t)(end_abs + 1u));

    uint16_t track_length = seq_track_length_steps(slot);
    if (!track_length) {
        track_length = SEQ_STEPS_PER_BAR;
    }

    uint16_t start_local = (uint16_t)(start_abs % track_length);
    uint16_t end_local   = (uint16_t)(end_abs % track_length);
    if (end_local < start_local) {
        uint16_t tmp = start_local;
        start_local  = end_local;
        end_local    = tmp;
    }

    for (uint8_t semitone = 0; semitone < SEQ_MELODIC_KEY_COUNT; semitone++) {
        if (!(chord_mask & (uint16_t)(1u << semitone))) {
            continue;
        }
        uint8_t pitch = seq_melodic_note_pitch(track, semitone);
        uint8_t velocity = MEL_DEFAULT_VELOCITY;
        mel_note_descriptor_t desc;
        bool found = false;
        if (mel_track_note_descriptor(track, start_local, &desc) && desc.pitch == pitch) {
            found = true;
        } else if (mel_track_note_descriptor(track, end_local, &desc) && desc.pitch == pitch) {
            found = true;
        }
        if (found) {
            velocity = desc.velocity;
            mel_track_remove_event(track, desc.event_index);
        }
        mel_track_add_note(track, start_local, end_local, pitch, velocity);
    }

    melodic_last_mask[track] = chord_mask;
    seq_chord_capture_clear();
    seq_chord_tap_reset();
    return true;
}

typedef struct {
    uint8_t default_track;
} sequencer_module_t;

typedef struct {
    bool active;
    bool running;
} sequencer_state_t;

static sequencer_module_t seq_config = {
    .default_track = SEQ_DEFAULT_TRACK % 12,
};

static sequencer_state_t seq_state = {
    .active  = false,
    .running = false,
};

static bool seq_temp_restore(uint32_t now);
static void seq_euclid_mode_exit(bool reset, uint32_t now);
static bool seq_handle_escape(uint32_t now) {
    if (seq_chord_mode[view_slot] && (seq_chord_selected != SEQ_CHORD_NONE || seq_chord_note_held)) { seq_chord_cancel(); return true; }
    if (seq_step_hold.active) seq_step_hold_finish(false);
    bool consumed = false;
    if (euclid_mode_active) {
        seq_euclid_mode_exit(false, now);
        consumed = true;
    }

    if (chord_capture.active) {
        seq_chord_capture_cancel();
        consumed = true;
    }

    if (step_edit.active) {
        seq_step_edit_reset();
        consumed = true;
    }

    if (cc_context.active || cc_mode.held || cc_mode.latched || cc_mode.temporary || cc_mode.latched_temporary) {
        seq_cc_exit_mode(now);
        cc_mode.latched             = false;
        cc_mode.simple_toggle       = false;
        cc_mode.held                = false;
        cc_mode.consumed            = false;
        cc_mode.temporary           = false;
        cc_mode.latched_temporary   = false;
        cc_mode.pending_track_valid = false;
        cc_mode.tap_count           = 0;
        consumed = true;
    }

    if (seq_track_confirmation_active()) {
        seq_track_cancel_confirmation();
        consumed = true;
    }

    if (seq_track_length_cancel_all()) {
        consumed = true;
    }

    if (bar_nav_state.active) {
        seq_bar_nav_reset();
        consumed = true;
    }

    if (mute_pending.active) {
        seq_mute_pending_reset();
        consumed = true;
    }

    if (consumed) {
        SEQ_LOG("[seq] escape consumed active=%d\n", seq_state.active);
    }

    return consumed;
}

static const uint8_t       MEL_DEFAULT_BASE_NOTE = 60;

static bool     seq_running     = false;
static uint16_t bpm             = 120;
static uint8_t  step_idx        = 0;
static uint32_t step_started_ms = 0;
static uint8_t  seq_shift_mask  = 0;

static bool     seq_first_step = true;
static bool     seq_resume_after_skip;
static uint32_t seq_next_step_ms;
static uint32_t seq_step_duration_ms = 125;
static uint32_t seq_clock_error;
static uint32_t seq_clock_denominator;
static uint8_t  seq_timing_swing;
static bool     seq_external_clock;
static uint8_t  seq_external_pulses;
static uint8_t  seq_external_pending;
static uint8_t  seq_external_head;
static uint32_t seq_external_due[8];
static uint32_t seq_external_measure_ms;
static uint16_t seq_external_measure_pulses;
static uint32_t seq_external_last_clock;
static bool     seq_external_have_clock;
static uint32_t seq_external_step_interval = 125;
static uint32_t seq_external_song_position;
static bool     seq_storage_save_pending;
static void     seq_transport_start(uint32_t now, uint32_t position);
static void     seq_transport_stop(uint32_t now);

static uint16_t     track_length_bars[12];
static uint16_t     track_length_steps[12];
static uint16_t     view_bar           = 0;
static uint16_t     playhead_bar       = 0;
static bool         follow_mode        = true;
static uint32_t     seq_step_position  = 0;
static uint32_t     seq_track_step_position[12] = {0};
static uint32_t     seq_total_steps    = SEQ_STEPS_PER_BAR;
static uint16_t     seq_loop_steps;
static bool         seq_loop_held;
static void         seq_loop_set(uint16_t steps, uint32_t now);

typedef struct {
    bool     active;
    bool     step_mode;
    uint16_t value;
    uint8_t  digits;
} seq_track_length_input_t;

static seq_track_length_input_t track_length_input[12];

static inline uint8_t slot_drum_track(uint8_t slot);
static inline bool    slot_is_melodic(uint8_t slot);
static inline bool    slot_is_wrapper(uint8_t slot);
static inline uint8_t slot_melodic_track(uint8_t slot);
static inline bool    slot_viewing_child(uint8_t slot);
static inline bool    slot_uses_melodic(uint8_t slot);
static inline bool    slot_has_drum_layer(uint8_t slot);
static inline bool    seq_mod_del(void);
static inline bool    seq_shift_active(void);
static inline int8_t  track_from_keycode(uint16_t keycode);
static inline uint16_t clamp_bpm(int32_t candidate);

static inline uint16_t seq_track_length_steps(uint8_t slot) {
    uint16_t length = track_length_steps[slot % 12];
    return length ? length : SEQ_STEPS_PER_BAR;
}

static inline uint16_t seq_track_playback_length(uint8_t slot) {
    uint16_t length = seq_track_length_steps(slot);
    return seq_loop_steps && seq_loop_steps < length ? seq_loop_steps : length;
}

static void seq_track_sync_position(uint8_t slot) {
    uint8_t track = slot % 12;
    /* Editing content or loop length never creates a new transport origin. */
    seq_track_step_position[track] = seq_step_position % seq_track_playback_length(track);
}

static inline uint16_t seq_track_length_bars(uint8_t slot) {
    uint16_t bars = track_length_bars[slot % 12];
    return bars ? bars : 1;
}

static inline bool seq_drum_step_set(uint8_t channel, uint8_t note, uint16_t step, bool value) {
    uint16_t length = seq_track_length_steps(channel);
    if (!length) {
        return false;
    }
    step %= length;
    bool present = seq_drum_pool_step_get(channel, note, step);
    if (!seq_drum_pool_step_set(channel, note, step, value)) {
        seq_drum_pool_report_full();
        return false;
    }
    if (present != value) {
        seq_mark_dirty();
    }
    return true;
}

static inline void seq_drum_step_toggle(uint8_t channel, uint8_t note, uint16_t step) {
    uint16_t length = seq_track_length_steps(channel);
    if (!length) {
        return;
    }
    step %= length;
    seq_drum_step_set(channel, note, step, !seq_drum_pool_step_get(channel, note, step));
}

static inline bool seq_drum_step_base_hit(uint8_t channel, uint8_t note, uint16_t step) {
    uint16_t length = seq_track_length_steps(channel);
    if (!length) {
        return false;
    }
    uint16_t local_step = (uint16_t)(step % length);
    return seq_drum_pool_step_get(channel, note, local_step);
}

/* Old projects keep their shared generator until a lane is explicitly edited.
 * An override belongs to the allocated lane and never changes its neighbours. */
static seq_euclid_track_t *seq_drum_euclid_state(uint8_t track, uint8_t note) {
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    return lane && lane->euclid.steps ? &lane->euclid : &euclid_tracks[track];
}

static seq_euclid_track_t *seq_euclid_selected(uint8_t track, bool write) {
    track %= 12;
    if (slot_uses_melodic(track)) return &euclid_tracks[track];
    uint8_t note = drum_active_note[track];
    if (!write) return seq_drum_euclid_state(track, note);
    if (!seq_drum_pool_reserve(track, 1u << note)) {
        seq_drum_pool_report_full();
        return NULL;
    }
    seq_euclid_track_t *e = &seq_drum_pool_lane(track, note)->euclid;
    if (!e->steps) {
        *e = euclid_tracks[track];
        if (!e->steps) e->steps = seq_track_length_steps(track);
        /* A lane owns its sound; the old track palette only routes legacy hits. */
        e->note_count = 1;
        e->note_pool[0] = note;
        e->palette_pin = 255;
    }
    return e;
}

static bool seq_euclid_state_hit(uint8_t track, const seq_euclid_track_t *state, uint16_t step, uint16_t *hit_index_out, int8_t *pool_value_out);
static bool seq_drum_euclid_hit(uint8_t track, uint8_t note, uint16_t step, uint16_t *index) {
    int8_t routed_note = -1;
    return seq_euclid_state_hit(track, seq_drum_euclid_state(track, note), step, index, &routed_note) && routed_note == note;
}

static void seq_euclid_reset_selected(uint8_t track) {
    if (slot_uses_melodic(track)) { seq_euclid_reset_track(track); return; }
    uint8_t note = drum_active_note[track];
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    if (!euclid_tracks[track].enabled) {
        if (lane) { lane->euclid = (seq_euclid_track_t){0}; seq_drum_pool_prune(track, note); }
    } else {
        seq_euclid_track_t *e = seq_euclid_selected(track, true);
        if (!e) return;
        *e = (seq_euclid_track_t){.steps = seq_track_length_steps(track), .probability = 100, .note_count = 1, .palette_pin = 255};
        e->note_pool[0] = note;
    }
    seq_mark_dirty();
}

static inline bool seq_drum_step_get(uint8_t channel, uint8_t note, uint16_t step) {
    uint16_t length = seq_track_length_steps(channel);
    if (!length) {
        return false;
    }
    uint16_t absolute_step = step;
    uint16_t local_step    = (uint16_t)(absolute_step % length);
    bool     base_hit      = seq_drum_pool_step_get(channel, note, local_step);
    if (seq_drum_euclid_hit(channel, note, absolute_step, NULL)) return true;
    return base_hit;
}

static inline uint16_t seq_drum_window_mask(uint8_t channel, uint8_t note, uint16_t bar) {
    uint16_t length = seq_track_length_steps(channel);
    if (!length) {
        return 0;
    }

    uint16_t window_start = (uint16_t)(bar * SEQ_STEPS_PER_BAR);
    uint16_t mask         = 0;

    for (uint8_t i = 0; i < SEQ_STEPS_PER_BAR; i++) {
        uint16_t step = window_start + i;
        if (step >= length) {
            break;
        }
        if (seq_drum_step_get(channel, note, step)) {
            mask |= (uint16_t)(1u << i);
        }
    }

    return mask;
}

static inline void seq_drum_step_clear_range(uint8_t channel, uint8_t note, uint16_t from_step) {
    seq_drum_pool_trim(channel, note, from_step);
    seq_mark_dirty();
}

static void seq_euclid_reset_track(uint8_t track) {
    track %= 12;
    seq_euclid_track_t *state = &euclid_tracks[track];
    *state                    = (seq_euclid_track_t){0};
    seq_palette_replace[track] = false;
    memset(seq_modules[track].config.euclid.qualities, 0, 8);
    uint16_t length           = seq_track_length_steps(track);
    if (!length) {
        length = SEQ_STEPS_PER_BAR;
    }
    if (!length) {
        length = 1;
    }
    state->steps      = length;
    state->note_count = 1;
    state->note_pool[0] = 0;
    state->swing_amount     = 0;
    state->probability      = 100;
    state->ratchet_threshold = 0;
    state->palette_pin      = 0xFF;
    for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
        seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, n);
        if (lane) { lane->euclid = (seq_euclid_track_t){0}; seq_drum_pool_prune(track, n); }
    }
    seq_mark_dirty();
}

static void seq_euclid_seed_pool(uint8_t track) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    if (slot_uses_melodic(track) && state->palette_authored) return;
    if (!state->note_count) {
        state->note_count = 1;
    }
    seq_slot_t *slot = &seq_slots[track % 12];
    if (slot_uses_melodic(track)) {
        state->note_pool[0] = 0;
    } else {
        uint8_t drum_index = slot->drum_track % 12;
        state->note_pool[0] = (int8_t)drum_active_note[drum_index];
    }
    seq_mark_dirty();
}

static void seq_euclid_sync_track(uint8_t track) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, false);
    if (!state) return;
    uint16_t length           = seq_track_length_steps(track);
    if (!length) {
        length = SEQ_STEPS_PER_BAR;
    }
    if (!length) {
        length = 1;
    }
    if (state->steps == 0 || state->steps > length) {
        state->steps = length;
    }
    if (state->steps == 0) {
        state->steps = 1;
    }
    if (state->fills > state->steps) {
        state->fills = state->steps;
    }
    if (state->note_count == 0) {
        seq_euclid_seed_pool(track);
    }
    if (state->steps) {
        int32_t rotate = state->rotate;
        int32_t mod    = state->steps;
        if (mod) {
            rotate %= mod;
            if (rotate < 0) {
                rotate += mod;
            }
            state->rotate = (int16_t)rotate;
        } else {
            state->rotate = 0;
        }
    } else {
        state->rotate = 0;
    }

    uint8_t pool_count = state->note_count;
    if (state->palette_pin != 0xFF && (pool_count == 0 || state->palette_pin >= pool_count)) {
        state->palette_pin = (pool_count > 0) ? (uint8_t)(pool_count - 1u) : 0xFF;
    }
    if (state->probability > 100) {
        state->probability = 100;
    }
    if (state->ratchet_threshold > state->steps) {
        state->ratchet_threshold = state->steps;
    }
}

static void seq_euclid_sync_lengths(uint8_t track) {
    uint16_t length = seq_track_length_steps(track);
    for (uint8_t n = 0; n <= SEQ_DRUM_NOTE_COUNT; n++) {
        seq_drum_lane_storage_t *lane = n < SEQ_DRUM_NOTE_COUNT ? seq_drum_pool_lane(track, n) : NULL;
        seq_euclid_track_t *e = n == SEQ_DRUM_NOTE_COUNT ? &euclid_tracks[track] : lane ? &lane->euclid : NULL;
        if (!e || !e->steps) continue;
        if (e->steps > length) e->steps = length;
        if (e->fills > e->steps) e->fills = e->steps;
        e->rotate = ((e->rotate % e->steps) + e->steps) % e->steps;
        if (e->ratchet_threshold > e->steps) e->ratchet_threshold = e->steps;
    }
}

static void seq_euclid_enable(uint8_t track, bool enabled) {
    if (seq_modules[track % 12].kind != SEQ_MODULE_EUCLID || (seq_modules[track % 12].composed && seq_modules[track % 12].rhythm.kind != 2)) return;
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    if (state->enabled == enabled) {
        return;
    }
    state->enabled = enabled;
    seq_euclid_sync_track(track);
    if (enabled) {
        if (state->fills == 0) {
            uint16_t steps = state->steps;
            if (!steps) {
                steps = seq_track_length_steps(track);
            }
            if (!steps) {
                steps = SEQ_STEPS_PER_BAR;
            }
            if (!steps) {
                steps = 1;
            }
            uint16_t guess = (steps >= 4) ? (steps / 4) : 1;
            if (guess == 0) {
                guess = 1;
            }
            if (guess > steps) {
                guess = steps;
            }
            state->fills = guess;
        }
        if (state->note_count == 0) {
            seq_euclid_seed_pool(track);
        }
        seq_euclid_seed_pool(track);
    }
    seq_mark_dirty();
}

static void seq_euclid_adjust_steps(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    state->template_active    = false;
    uint16_t length           = seq_track_length_steps(track);
    if (!length) {
        length = SEQ_STEPS_PER_BAR;
    }
    if (!length) {
        length = 1;
    }
    int32_t current = state->steps ? (int32_t)state->steps : (int32_t)length;
    int32_t next    = current + delta;
    if (next < 1) {
        next = 1;
    }
    if (next > (int32_t)length) {
        next = (int32_t)length;
    }
    uint16_t prev_steps = state->steps;
    state->steps        = (uint16_t)next;
    if (state->fills > state->steps) {
        state->fills = state->steps;
    }
    seq_euclid_sync_track(track);
    if (state->steps != prev_steps) {
        seq_mark_dirty();
    }
}

static void seq_euclid_adjust_fills(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    state->template_active    = false;
    uint16_t steps           = state->steps;
    if (!steps) {
        steps = seq_track_length_steps(track);
    }
    if (!steps) {
        steps = SEQ_STEPS_PER_BAR;
    }
    if (!steps) {
        steps = 1;
    }
    int32_t next = (int32_t)state->fills + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > (int32_t)steps) {
        next = (int32_t)steps;
    }
    uint16_t prev = state->fills;
    state->fills  = (uint16_t)next;
    if (state->fills != prev) {
        seq_mark_dirty();
    }
}

static void seq_euclid_adjust_rotate(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    if (state->steps == 0) {
        seq_euclid_sync_track(track);
    }
    uint16_t steps = state->steps;
    if (!steps) {
        return;
    }
    int32_t next = (int32_t)state->rotate + delta;
    next %= (int32_t)steps;
    if (next < 0) {
        next += steps;
    }
    int16_t prev = state->rotate;
    state->rotate = (int16_t)next;
    if (state->rotate != prev) {
        seq_mark_dirty();
    }
}

static void seq_euclid_adjust_swing(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    int16_t next = (int16_t)state->swing_amount + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > 48) {
        next = 48;
    }
    if ((uint8_t)next != state->swing_amount) {
        state->swing_amount = (uint8_t)next;
        seq_mark_dirty();
    }
}

static void seq_euclid_adjust_probability(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    int16_t next = (int16_t)state->probability + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > 100) {
        next = 100;
    }
    if ((uint8_t)next != state->probability) {
        state->probability = (uint8_t)next;
        seq_mark_dirty();
    }
}

static void seq_euclid_adjust_threshold(uint8_t track, int16_t delta) {
    track %= 12;
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    uint16_t steps = state->steps;
    if (!steps) {
        steps = seq_track_length_steps(track);
    }
    if (!steps) {
        steps = SEQ_STEPS_PER_BAR;
    }
    if (!steps) {
        steps = 1;
    }
    if (steps > 255) {
        steps = 255;
    }
    int16_t next = (int16_t)state->ratchet_threshold + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > (int16_t)steps) {
        next = (int16_t)steps;
    }
    if ((uint8_t)next != state->ratchet_threshold) {
        state->ratchet_threshold = (uint8_t)next;
        seq_mark_dirty();
    }
}

static void seq_euclid_set_melodic_pool(uint8_t track, uint16_t mask, uint8_t base_semitone) {
    track %= 12;
    seq_euclid_track_t *state = &euclid_tracks[track];

    uint16_t limited_mask = mask & (uint16_t)((1u << SEQ_MELODIC_KEY_COUNT) - 1u);
    if (!limited_mask) {
        state->note_count   = 1;
        state->note_pool[0] = 0;
        for (uint8_t i = 1; i < SEQ_EUCLID_NOTE_POOL_MAX; i++) {
            state->note_pool[i] = 0;
        }
        return;
    }

    state->palette_authored = true;
    uint8_t pivot = base_semitone % SEQ_MELODIC_KEY_COUNT;
    limited_mask |= (uint16_t)(1u << pivot);

    int8_t  buffer[SEQ_EUCLID_NOTE_POOL_MAX] = {0};
    uint8_t count                            = 0;

    if (count < SEQ_EUCLID_NOTE_POOL_MAX) {
        buffer[count++] = 0;
    }

    for (uint8_t step = 1; step < SEQ_MELODIC_KEY_COUNT && count < SEQ_EUCLID_NOTE_POOL_MAX; step++) {
        uint8_t positive = (uint8_t)((pivot + step) % SEQ_MELODIC_KEY_COUNT);
        if (limited_mask & (uint16_t)(1u << positive)) {
            buffer[count++] = (int8_t)step;
            if (count >= SEQ_EUCLID_NOTE_POOL_MAX) {
                break;
            }
        }

        uint8_t negative = (uint8_t)((pivot + SEQ_MELODIC_KEY_COUNT - step) % SEQ_MELODIC_KEY_COUNT);
        if (negative == positive) {
            continue;
        }
        if (limited_mask & (uint16_t)(1u << negative) && count < SEQ_EUCLID_NOTE_POOL_MAX) {
            buffer[count++] = (int8_t)(-((int8_t)step));
        }
    }

    if (count == 0) {
        buffer[count++] = 0;
    }

    state->note_count = count;
    for (uint8_t i = 0; i < state->note_count; i++) {
        state->note_pool[i] = buffer[i];
    }
    for (uint8_t i = state->note_count; i < SEQ_EUCLID_NOTE_POOL_MAX; i++) {
        state->note_pool[i] = 0;
    }
    seq_mark_dirty();
}

static void seq_euclid_learn_melodic_keys(uint8_t slot, uint8_t melodic_track) {
    slot %= 12;
    if (melodic_track >= MEL_TRACK_COUNT) {
        return;
    }
    uint16_t mask = melodic_key_mask[melodic_track];
    if (!mask) {
        return;
    }
    uint8_t base = melodic_last_semitone[melodic_track] % SEQ_MELODIC_KEY_COUNT;
    seq_euclid_set_melodic_pool(slot, mask, base);
}

static void seq_euclid_palette_capture_reset(void) {
    euclid_palette_capture = (seq_euclid_palette_capture_t){0};
}

static void seq_euclid_palette_capture_record(uint8_t track, int8_t semitone) {
    track %= 12;
    if (!seq_euclid_editing_enabled()) {
        return;
    }
    if (!euclid_palette_capture.active || euclid_palette_capture.track != track) {
        euclid_palette_capture = (seq_euclid_palette_capture_t){.active = true, .track = track, .replace = seq_palette_replace[track] || !euclid_tracks[track].palette_authored || ((seq_ctrl_mask | get_mods()) & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_RCTL)))};
        const seq_euclid_track_t *saved = &euclid_tracks[track];
        if (!euclid_palette_capture.replace && saved->note_count) {
            euclid_palette_capture.origin = saved->palette_root;
            euclid_palette_capture.count = saved->note_count;
            euclid_palette_capture.retained = saved->note_count;
            euclid_palette_capture.primed = true;
            memcpy(euclid_palette_capture.values, saved->note_pool, saved->note_count);
        }
    }
    if (!euclid_palette_capture.primed) {
        euclid_palette_capture.origin = semitone;
        euclid_palette_capture.primed = true;
        euclid_palette_capture.values[0] = 0;
        euclid_palette_capture.count     = 1;
        return;
    }
    int8_t offset = (int8_t)(semitone - euclid_palette_capture.origin);
    for (uint8_t i = 0; i < euclid_palette_capture.count; i++) {
        if (euclid_palette_capture.values[i] == offset) {
            return;
        }
    }
    if (euclid_palette_capture.count < SEQ_EUCLID_NOTE_POOL_MAX) {
        euclid_palette_capture.values[euclid_palette_capture.count++] = offset;
    } else {
        euclid_palette_capture.full = true;
    }
}

static void seq_euclid_palette_capture_commit(uint32_t now) {
    if (!euclid_palette_capture.active || !euclid_palette_capture.primed) {
        seq_euclid_palette_capture_reset();
        return;
    }
    uint8_t track = euclid_palette_capture.track % 12;
    seq_euclid_track_t *state = &euclid_tracks[track];
    uint8_t count = euclid_palette_capture.count ? euclid_palette_capture.count : 1u;
    if (count > SEQ_EUCLID_NOTE_POOL_MAX) {
        count = SEQ_EUCLID_NOTE_POOL_MAX;
    }
    if (!euclid_palette_capture.replace && count == euclid_palette_capture.retained) {
        seq_euclid_palette_capture_reset();
        return; /* Duplicate/full captures leave playback and the saved palette intact. */
    }
    for (uint8_t i = euclid_palette_capture.replace ? 0 : euclid_palette_capture.retained; i < 8; i++)
        seq_modules[track].config.euclid.qualities[i] = 0;
    state->palette_authored = true;
    state->note_count = count;
    for (uint8_t i = 0; i < count; i++) {
        state->note_pool[i] = euclid_palette_capture.values[i];
    }
    for (uint8_t i = count; i < SEQ_EUCLID_NOTE_POOL_MAX; i++) {
        state->note_pool[i] = 0;
    }
    if (euclid_palette_capture.replace) state->palette_pin = 0xFF;
    state->palette_root = euclid_palette_capture.origin < 0 ? 0 : euclid_palette_capture.origin % 12;
    seq_accumulator_reset_runtime(track);
    seq_euclid_sync_track(track);
    seq_mark_dirty();
    seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_FILLS, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_FILLS), now);
    seq_euclid_palette_capture_reset();
}

static void seq_midi_stop_melodic_track(uint8_t track_index, uint32_t now);

static bool seq_euclid_palette_remove(uint8_t track, uint8_t index, uint32_t now) {
    if (track >= 12 || !slot_uses_melodic(track) || index >= euclid_tracks[track].note_count) return false;
    seq_euclid_track_t *state = &euclid_tracks[track];
    seq_midi_stop_melodic_track(slot_melodic_track(track), now);
    for (uint8_t i = index; i + 1 < state->note_count; i++) state->note_pool[i] = state->note_pool[i + 1];
    for (uint8_t i = index; i + 1 < state->note_count; i++)
        seq_modules[track].config.euclid.qualities[i] = seq_modules[track].config.euclid.qualities[i + 1];
    seq_modules[track].config.euclid.qualities[state->note_count - 1] = 0;
    state->note_pool[--state->note_count] = 0;
    if (state->palette_pin == index) state->palette_pin = 255;
    else if (state->palette_pin != 255 && state->palette_pin > index) state->palette_pin--;
    /* Retain the root so removing the first slot does not transpose the others. */
    state->palette_authored = true;
    seq_accumulator_reset_runtime(track);
    seq_mark_dirty();
    return true;
}

static void seq_euclid_palette_pin_set(uint8_t track, int8_t slot, uint32_t now) {
    if (!slot_uses_melodic(track)) return;
    track %= 12;
    seq_euclid_track_t *state = &euclid_tracks[track];
    uint8_t previous = state->palette_pin;
    if (slot < 0) {
        state->palette_pin = 0xFF;
    } else {
        uint8_t pool_count = state->note_count;
        uint8_t index      = (uint8_t)slot;
        if (!pool_count) return;
        if (index >= pool_count) {
            return;
        }
        state->palette_pin = index;
    }
    if (state->palette_pin != previous) {
        seq_mark_dirty();
        uint16_t preview_value = (state->palette_pin == 0xFF) ? 0u : (uint16_t)(state->palette_pin + 1u);
        seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_FILLS, preview_value, now);
    }
}

static inline bool seq_euclid_probability_allows(uint8_t track) {
    seq_euclid_track_t *state = &euclid_tracks[track % 12];
    uint8_t probability = state->probability;
    if (probability >= 100) {
        return true;
    }
    if (probability == 0) {
        return false;
    }
    return (uint8_t)(rand() % 100) < probability;
}

static bool seq_euclid_state_hit(uint8_t track, const seq_euclid_track_t *state, uint16_t step, uint16_t *hit_index_out, int8_t *pool_value_out) {
    if (seq_modules[track % 12].kind != SEQ_MODULE_EUCLID || (seq_modules[track % 12].composed && seq_modules[track % 12].rhythm.kind != 2)) return false;
    track %= 12;
    if (!state->enabled || state->fills == 0) {
        return false;
    }
    uint16_t length = seq_track_length_steps(track);
    if (!length) {
        length = SEQ_STEPS_PER_BAR;
    }
    if (!length) {
        return false;
    }
    uint16_t steps = state->steps ? state->steps : length;
    if (!steps) {
        steps = 1;
    }
    uint16_t fills = state->fills > steps ? steps : state->fills;
    if (!fills) {
        return false;
    }

    seq_rhythm_component_t rhythm = {.kind = 2, .enabled = state->enabled, .steps = steps, .fills = fills, .rotation = state->rotate % steps, .template_active = state->template_active, .template_mask = state->template_mask};
    uint16_t hit_index;
    if (!seq_rhythm_hit(&rhythm, step % length, &hit_index)) return false;
    if (hit_index_out) {
        *hit_index_out = hit_index;
    }
    if (pool_value_out) {
        uint8_t pool_count = state->note_count ? state->note_count : 1u;
        uint16_t selection = hit_index % pool_count;
        if (state->palette_pin != 0xFF && state->palette_pin < pool_count) {
            selection = state->palette_pin;
        }
        int8_t pool_value = state->note_count ? state->note_pool[selection] : 0;
        *pool_value_out    = pool_value;
    }
    return true;
}


static bool seq_euclid_step_hit(uint8_t track, uint16_t step, uint16_t *index, int8_t *pool) {
    return seq_euclid_state_hit(track, &euclid_tracks[track % 12], step, index, pool);
}

static bool seq_euclid_any_enabled(void) {
    for (uint8_t t = 0; t < 12; t++) {
        if (seq_modules[t].kind != SEQ_MODULE_EUCLID) continue;
        if (slot_is_melodic(t) && euclid_tracks[t].enabled && euclid_tracks[t].fills) return true;
        for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
            const seq_euclid_track_t *e = seq_drum_euclid_state(t, n);
            if (e->enabled && e->fills) return true;
        }
    }
    return false;
}


static inline bool seq_euclid_editing_enabled(void) {
    return seq_modules[view_slot % 12].kind == SEQ_MODULE_EUCLID && (!seq_modules[view_slot % 12].composed || seq_modules[view_slot % 12].rhythm.kind == 2) && (euclid_mode_active || cmd_state.held);
}

static inline uint8_t seq_euclid_active_track(void) {
    return cmd_state.edit_track % 12;
}

static void seq_euclid_focus_track(uint8_t track) {
    uint8_t normalized     = track % 12;
    cmd_state.edit_track   = normalized;
    euclid_edit.track      = normalized;
    if (euclid_mode_active || cmd_state.held) {
        euclid_edit.active = true;
    }
}

static void seq_euclid_clear_preview(void) {
    euclid_param_preview_kind    = EUCLID_PARAM_KIND_NONE;
    euclid_param_preview_value   = 0;
    euclid_param_preview_expires = 0;
}

static void seq_euclid_capture_preview(uint8_t track, seq_euclid_param_kind_t kind, uint16_t value, uint32_t now) {
    if (kind == EUCLID_PARAM_KIND_NONE) {
        seq_euclid_clear_preview();
        return;
    }
    euclid_param_preview_kind    = kind;
    euclid_param_preview_value   = value;
    euclid_param_preview_expires = now + 700;
}

static seq_euclid_param_kind_t seq_euclid_param_kind_for_mode(seq_euclid_param_mode_t mode) {
    switch (mode) {
        case EUCLID_PARAM_MODE_FILLS:
            return EUCLID_PARAM_KIND_FILLS;
        case EUCLID_PARAM_MODE_ROTATE:
            return EUCLID_PARAM_KIND_ROTATE;
        case EUCLID_PARAM_MODE_STEPS:
            return EUCLID_PARAM_KIND_STEPS;
        case EUCLID_PARAM_MODE_PROBABILITY:
            return EUCLID_PARAM_KIND_PROBABILITY;
        case EUCLID_PARAM_MODE_ACCENT:
            return EUCLID_PARAM_KIND_SWING;
        case EUCLID_PARAM_MODE_RATCHET_THRESHOLD:
            return EUCLID_PARAM_KIND_RATCHET_THRESHOLD;
        default:
            return EUCLID_PARAM_KIND_NONE;
    }
}

static int16_t seq_euclid_param_step_for_mode(seq_euclid_param_mode_t mode) {
    bool fast = seq_shift_active();
    switch (mode) {
        case EUCLID_PARAM_MODE_FILLS:
            return fast ? 4 : 1;
        case EUCLID_PARAM_MODE_ROTATE:
            return fast ? 4 : 1;
        case EUCLID_PARAM_MODE_STEPS:
            return fast ? (int16_t)SEQ_STEPS_PER_BAR : 1;
        case EUCLID_PARAM_MODE_PROBABILITY:
            return fast ? 10 : 1;
        case EUCLID_PARAM_MODE_ACCENT:
            return fast ? 4 : 1;
        case EUCLID_PARAM_MODE_RATCHET_THRESHOLD:
            return fast ? 4 : 1;
        default:
            return 0;
    }
}

static seq_euclid_param_mode_t seq_euclid_param_mode_for_key(uint16_t keycode) {
    switch (keycode) {
        case KC_Q: return EUCLID_PARAM_MODE_STEPS;
        case KC_W: return EUCLID_PARAM_MODE_FILLS;
        case KC_E: return EUCLID_PARAM_MODE_ROTATE;
        case KC_R: return EUCLID_PARAM_MODE_PROBABILITY;
        case KC_T: return EUCLID_PARAM_MODE_ACCENT;
        case KC_F: return EUCLID_PARAM_MODE_RATCHET_THRESHOLD;
        default: return EUCLID_PARAM_MODE_NONE;
    }
}

static uint16_t seq_euclid_param_value_for_kind(uint8_t track, seq_euclid_param_kind_t kind) {
    uint8_t normalized            = track % 12;
    seq_euclid_track_t *state     = seq_euclid_selected(normalized, false);
    switch (kind) {
        case EUCLID_PARAM_KIND_FILLS:
            return state->fills;
        case EUCLID_PARAM_KIND_ROTATE:
            return (uint16_t)(state->rotate >= 0 ? state->rotate : 0);
        case EUCLID_PARAM_KIND_STEPS: {
            if (state->steps) {
                return state->steps;
            }
            uint16_t length = seq_track_length_steps(normalized);
            return length ? length : 1;
        }
        case EUCLID_PARAM_KIND_SWING:
            return state->swing_amount;
        case EUCLID_PARAM_KIND_PROBABILITY:
            return state->probability;
        case EUCLID_PARAM_KIND_RATCHET_THRESHOLD:
            return state->ratchet_threshold;
        default:
            return 0;
    }
}

static bool seq_cmd_register_tap(uint32_t now) {
    if (TIMER_DIFF_32(now, cmd_state.last_tap_ms) <= SEQ_DOUBLE_TAP_MS) {
        cmd_state.tap_count++;
    } else {
        cmd_state.tap_count = 1;
    }
    cmd_state.last_tap_ms = now;
    if (cmd_state.tap_count >= 2) {
        cmd_state.tap_count = 0;
        return true;
    }
    return false;
}

static void seq_euclid_mode_enter(uint32_t now) {
    uint8_t track = view_slot % 12;
    euclid_mode_active = true;
    seq_euclid_focus_track(track);
    seq_euclid_enable(track, true);
    seq_euclid_sync_track(track);
    seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_FILLS, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_FILLS), now);
}

static void seq_euclid_mode_exit(bool reset, uint32_t now) {
    uint8_t track = seq_euclid_active_track();
    if (reset) {
        seq_euclid_reset_selected(track);
        seq_euclid_sync_track(track);
    }
    euclid_mode_active   = false;
    euclid_param_mode    = EUCLID_PARAM_MODE_NONE;
    euclid_param_keycode = KC_NO;
    seq_euclid_capture_preview(track, reset ? EUCLID_PARAM_KIND_FILLS : EUCLID_PARAM_KIND_NONE, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_FILLS), now);
    if (!cmd_state.held) {
        euclid_edit.active = false;
    }
    seq_euclid_palette_capture_reset();
}

static inline bool seq_drum_has_activity(uint8_t channel) {
    uint32_t byte_count = ((uint32_t)seq_track_length_steps(channel) + 7u) >> 3;
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        const seq_drum_lane_storage_t *lane = seq_drum_pool_lane(channel, note);
        const seq_euclid_track_t *e = seq_drum_euclid_state(channel, note);
        if (e->enabled && e->fills) return true;
        if (!lane) {
            continue;
        }
        for (uint32_t b = 0; b < byte_count; b++) {
            if (lane->data[b]) {
                return true;
            }
        }
    }
    if (euclid_tracks[channel].enabled && euclid_tracks[channel].fills > 0) {
        return true;
    }
    return false;
}

static void seq_clamp_view_bar(void);

static void seq_melodic_set_last_mask(uint8_t track, uint16_t mask) {
    melodic_last_mask[track] = mask;
    if (mask) {
#if defined(__GNUC__)
        melodic_last_semitone[track] = (uint8_t)__builtin_ctz(mask);
#else
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            if (mask & (1u << i)) {
                melodic_last_semitone[track] = i;
                break;
            }
        }
#endif
    }
}

static void seq_update_total_steps(void) {
    uint32_t max_steps = SEQ_STEPS_PER_BAR;
    for (uint8_t i = 0; i < 12; i++) {
        if (!slot_has_activity(i)) {
            continue;
        }
        uint32_t length = track_length_steps[i];
        if (!length) {
            length = SEQ_STEPS_PER_BAR;
        }
        if (length > max_steps) {
            max_steps = length;
        }
    }
    seq_total_steps = max_steps;
    playhead_bar    = (uint16_t)(seq_track_step_position[view_slot % 12] / SEQ_STEPS_PER_BAR);
}

static void seq_track_set_bars(uint8_t slot, uint16_t bars) {
    uint8_t track = slot % 12;
    if (bars < 1) {
        bars = 1;
    } else if (bars > SEQ_MAX_BARS) {
        bars = SEQ_MAX_BARS;
    }

    uint16_t old_bars  = track_length_bars[track];
    uint16_t old_steps = seq_track_length_steps(track);
    uint16_t new_steps = bars * SEQ_STEPS_PER_BAR;

    if (old_bars == bars) {
        seq_euclid_sync_track(track);
        return;
    }

    if (old_steps > new_steps) {
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            seq_drum_step_clear_range(track, note, new_steps);
        }
        mel_track_trim(track, new_steps);
        seq_cc_automation_trim(track, new_steps);
    }

    track_length_bars[track]  = bars;
    track_length_steps[track] = new_steps;
    seq_track_sync_position(track);
    seq_ratchet_clear_track(track);
    seq_euclid_sync_lengths(track);
    seq_update_total_steps();
    seq_clamp_view_bar();
    seq_mark_dirty();
}

static void seq_track_set_steps(uint8_t slot, uint16_t steps) {
    uint8_t track = slot % 12;
    if (steps < 1) {
        steps = 1;
    } else if (steps > SEQ_MAX_STEPS) {
        steps = SEQ_MAX_STEPS;
    }

    uint16_t target_bars = (uint16_t)((steps + SEQ_STEPS_PER_BAR - 1u) / SEQ_STEPS_PER_BAR);
    if (target_bars < 1) {
        target_bars = 1;
    } else if (target_bars > SEQ_MAX_BARS) {
        target_bars = SEQ_MAX_BARS;
    }

    if (track_length_bars[track] != target_bars) {
        seq_track_set_bars(track, target_bars);
    }

    uint16_t current_capacity = track_length_bars[track] * SEQ_STEPS_PER_BAR;
    if (steps > current_capacity) {
        steps = current_capacity;
    }

    uint16_t current_steps = seq_track_length_steps(track);
    if (steps < current_steps) {
        uint8_t drum_channel = slot_drum_track(track);
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            seq_drum_step_clear_range(drum_channel, note, steps);
        }
        if (slot_is_melodic(track)) {
            uint8_t melodic_index = slot_melodic_track(track);
            mel_track_trim(melodic_index, steps);
        }
        seq_cc_automation_trim(track, steps);
    }

    track_length_steps[track] = steps;
    seq_track_sync_position(track);
    if (steps != current_steps) seq_ratchet_clear_track(track);
    seq_euclid_sync_lengths(track);
    seq_update_total_steps();
    seq_clamp_view_bar();
    if (steps != current_steps) {
        seq_mark_dirty();
    }
}

static void seq_track_ensure_steps(uint8_t slot, uint16_t steps) {
    uint8_t track = slot % 12;
    uint16_t current = seq_track_length_steps(track);
    if (steps <= current) {
        return;
    }
    uint16_t needed_bars = (steps + SEQ_STEPS_PER_BAR - 1u) / SEQ_STEPS_PER_BAR;
    if (needed_bars > SEQ_MAX_BARS) {
        needed_bars = SEQ_MAX_BARS;
    }
    seq_track_set_bars(track, needed_bars);
}

static void seq_track_double_length(uint8_t slot, bool duplicate_contents) {
    uint8_t track = slot % 12;
    uint16_t old_steps = seq_track_length_steps(track);
    if (old_steps > SEQ_MAX_STEPS / 2u) {
        seq_drum_pool_report_full();
        return;
    }
    uint16_t new_steps = old_steps * 2u;
    uint16_t new_bars  = (new_steps + 15u) / 16u;
    if (duplicate_contents) {
        if (!mel_track_can_duplicate(slot_melodic_track(slot), old_steps, new_steps) || !seq_automation_can_duplicate(track, old_steps)) {
            seq_drum_pool_report_full();
            return;
        }
        for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
            const seq_drum_lane_storage_t *lane = seq_drum_pool_lane(slot_drum_track(slot), n);
            if (!lane) continue;
            uint8_t mods = 0;
            for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++)
                mods += lane->mods[i].step < old_steps;
            if (mods * 2u > SEQ_DRUM_STEP_MOD_CAP) {
                seq_drum_pool_report_full();
                return;
            }
        }
    }
    uint16_t copy_limit = (old_steps > new_steps) ? new_steps : old_steps;

    uint8_t drum_channel = slot_drum_track(slot);
    if (duplicate_contents) {
        uint16_t targets = 0;
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            for (uint16_t step = 0; step < copy_limit; step++) {
                if (seq_drum_pool_step_get(drum_channel, note, step)) {
                    targets |= (uint16_t)(1u << note);
                    break;
                }
            }
        }
        if (!seq_drum_can_reserve(drum_channel, targets)) {
            return;
        }
    }

    seq_track_set_bars(track, new_bars);
    track_length_steps[track] = new_steps;
    seq_track_sync_position(track);
    seq_ratchet_clear_track(track);
    seq_update_total_steps();

    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        for (uint16_t step = old_steps; step < new_steps; step++) {
            seq_drum_step_set(drum_channel, note, step, false);
        }
    }

    if (duplicate_contents) {
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            for (uint16_t step = 0; step < copy_limit; step++) {
                uint16_t target = (uint16_t)(step + old_steps);
                if (target >= new_steps) {
                    break;
                }
                if (seq_drum_pool_step_get(drum_channel, note, step)) {
                    seq_drum_step_set(drum_channel, note, target, true);
                }
            }
        }
    }

    if (duplicate_contents) {
        seq_automation_duplicate(track, old_steps);
        for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
            seq_drum_lane_storage_t *lane = seq_drum_pool_lane(drum_channel, n);
            if (!lane) continue;
            for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
                if (lane->mods[i].step >= old_steps) continue;
                seq_drum_step_mod_t  copy   = lane->mods[i];
                seq_drum_step_mod_t *target = seq_drum_step_mod_ensure(drum_channel, n, copy.step + old_steps);
                if (target) {
                    *target = copy;
                    target->step += old_steps;
                }
            }
        }
    }
    if (slot_is_melodic(slot)) {
        uint8_t melodic_track = slot_melodic_track(slot);
        if (duplicate_contents) {
            mel_track_duplicate_append(melodic_track, old_steps, new_steps);
        } else {
            mel_track_clear_range(melodic_track, old_steps, new_steps);
        }
    }
    seq_mark_dirty();
}

static void seq_record_reset(void) {
    record_state = (seq_record_state_t){0};
}

static void seq_record_add_melodic_segment(uint8_t track_index, uint16_t start_local, uint16_t end_local, uint16_t mask, uint8_t velocity) {
    if (end_local < start_local) {
        return;
    }
    mask &= (uint16_t)((1u << SEQ_MELODIC_KEY_COUNT) - 1u);
    if (!mask) {
        return;
    }
    for (uint8_t semitone = 0; semitone < SEQ_MELODIC_KEY_COUNT; semitone++) {
        if (!(mask & (uint16_t)(1u << semitone))) {
            continue;
        }
        uint8_t pitch = seq_melodic_note_pitch(track_index, semitone);
        mel_track_add_note(track_index, start_local, end_local, pitch, velocity);
    }
}

static void seq_record_try_begin(uint8_t slot, bool is_melodic, uint16_t mask, uint32_t start_step) {
    if (!record_state.armed || !seq_state.running || !mask) {
        return;
    }

    record_state.capturing      = true;
    record_state.is_melodic     = is_melodic;
    record_state.capture_track  = slot % 12;
    record_state.capture_mask   = mask;
    record_state.start_step     = start_step;
    record_state.start_local     = seq_track_step_position[slot % 12];
    record_state.flush_requested = false;
}

static void seq_record_flush(uint32_t end_step) {
    if (!record_state.capturing) {
        record_state.flush_requested = false;
        return;
    }

    if (record_state.is_melodic) {
        uint16_t mask = record_state.capture_mask & ((1u << SEQ_MELODIC_KEY_COUNT) - 1u);
        if (mask) {
            uint8_t slot_index  = record_state.capture_track % 12;
            uint8_t track_index = slot_melodic_track(slot_index);
            uint16_t track_length = seq_track_length_steps(slot_index);
            uint32_t duration     = end_step - record_state.start_step;
            if (duration >= track_length) duration = track_length - 1u;
            uint16_t start_local = record_state.start_local % track_length;
            uint16_t end_local   = (start_local + duration) % track_length;
            uint8_t  velocity    = MEL_DEFAULT_VELOCITY;
            uint16_t needed      = 0;
            for (uint8_t bit = 0; bit < SEQ_MELODIC_KEY_COUNT; bit++)
                if (mask & (1u << bit)) needed++;
            if (end_local < start_local) needed *= 2;
            if (!mel_track_can_add(track_index, needed)) {
                seq_drum_pool_report_full();
                record_state.capturing       = false;
                record_state.capture_mask    = 0;
                record_state.flush_requested = false;
                return;
            }

            if (end_local < start_local) {
                uint16_t first_end = (uint16_t)((track_length > 0) ? (track_length - 1u) : start_local);
                seq_record_add_melodic_segment(track_index, start_local, first_end, mask, velocity);
                seq_record_add_melodic_segment(track_index, 0, end_local, mask, velocity);
            } else {
                seq_record_add_melodic_segment(track_index, start_local, end_local, mask, velocity);
            }
        }
    }

    record_state.capturing       = false;
    record_state.capture_mask    = 0;
    record_state.flush_requested = false;
}

static void seq_record_try_disarm(uint32_t end_step) {
    if (record_state.capturing && record_state.is_melodic) {
        seq_record_flush(end_step);
    }
    record_state.capturing       = false;
    record_state.capture_mask    = 0;
    record_state.flush_requested = false;
    record_state.held_mask       = 0;
    record_state.armed           = false;
}

static void seq_record_on_transport_stop(uint32_t end_step) {
    if (record_state.capturing && record_state.is_melodic) {
        seq_record_flush(end_step);
    }
    record_state.capturing       = false;
    record_state.capture_mask    = 0;
    record_state.flush_requested = false;
    record_state.held_mask       = 0;
}

static void seq_record_update_mask(uint8_t slot, bool is_melodic, uint16_t mask) {
    uint8_t slot_index = slot % 12;
    record_state.held_mask     = mask;

    if (!record_state.armed) {
        return;
    }

    if (!seq_state.running) {
        return;
    }

    if (!mask) {
        if (record_state.capturing && record_state.is_melodic == is_melodic && record_state.capture_track == slot_index) {
            record_state.flush_requested = true;
        } else if (!is_melodic) {
            record_state.capturing       = false;
            record_state.capture_mask    = 0;
            record_state.flush_requested = false;
        }
        return;
    }

    if (!record_state.capturing) {
        seq_record_try_begin(slot_index, is_melodic, mask, seq_step_position);
        return;
    }

    if (record_state.capture_track != slot_index || record_state.is_melodic != is_melodic) {
        record_state.flush_requested = true;
        return;
    }

    if (record_state.capture_mask != mask && is_melodic) {
        record_state.flush_requested = true;
    } else if (!is_melodic) {
        record_state.capture_mask = mask & ((1u << SEQ_DRUM_NOTE_COUNT) - 1u);
    }
}

static void seq_record_step_tick(uint32_t global_previous, uint32_t global_next) {
    if (!record_state.armed || !seq_state.running) {
        return;
    }

    uint8_t focus_slot     = view_slot % 12;
    bool    focus_melodic  = slot_uses_melodic(view_slot);
    bool    flushed        = false;

    if (record_state.capturing && record_state.is_melodic) {
        bool mask_changed = record_state.flush_requested || (record_state.capture_mask != record_state.held_mask);
        bool track_changed = (record_state.capture_track != focus_slot);
        bool released = (record_state.held_mask == 0);
        bool type_changed = !focus_melodic;
        if (mask_changed || track_changed || released || type_changed) {
            seq_record_flush(global_previous);
            flushed = true;
        }
    }

    if (focus_melodic) {
        if (!record_state.capturing && record_state.held_mask) {
            uint32_t start_step = flushed ? global_next : seq_step_position;
            seq_record_try_begin(focus_slot, true, record_state.held_mask, start_step);
        }
        if (record_state.capturing) {
            record_state.capture_mask = record_state.held_mask;
        }
    } else {
        uint16_t mask = record_state.held_mask & ((1u << SEQ_DRUM_NOTE_COUNT) - 1u);
        if (mask) {
            uint8_t drum_track = slot_drum_track(focus_slot);
            if (!seq_drum_can_reserve(drum_track, mask)) {
                return;
            }
            for (uint8_t lane = 0; lane < SEQ_DRUM_NOTE_COUNT; lane++) {
                if (!(mask & (uint16_t)(1u << lane))) {
                    continue;
                }
                seq_drum_step_set(drum_track, lane, seq_track_step_position[focus_slot], true);
            }
            record_state.capturing      = true;
            record_state.is_melodic     = false;
            record_state.capture_track  = focus_slot;
            record_state.capture_mask   = mask;
            record_state.flush_requested = false;
        } else if (!focus_melodic) {
            record_state.capturing       = false;
            record_state.capture_mask    = 0;
            record_state.flush_requested = false;
        }
    }

    record_state.flush_requested = false;
}

static void seq_clamp_view_bar(void) {
    uint16_t max_bars = seq_track_length_bars(view_slot);
    if (!max_bars) {
        max_bars = 1;
    }
    if (view_bar >= max_bars) {
        view_bar = max_bars - 1;
    }
}

static void seq_set_view_bar(uint16_t bar_index) {
    uint16_t max_bars = seq_track_length_bars(view_slot);
    if (!max_bars) {
        max_bars = 1;
    }
    if (bar_index >= max_bars) {
        bar_index = (max_bars > 0) ? (max_bars - 1u) : 0u;
    }

    uint16_t current_steps = seq_track_length_steps(view_slot);
    if (current_steps >= SEQ_STEPS_PER_BAR) {
        uint32_t required = (uint32_t)(bar_index + 1u) * (uint32_t)SEQ_STEPS_PER_BAR;
        if (current_steps < required) {
            seq_track_ensure_steps(view_slot, (uint16_t)required);
        }
    }

    view_bar = bar_index;
    seq_clamp_view_bar();
}

static void seq_bar_nav_reset(void) {
    bar_nav_state.active       = false;
    bar_nav_state.digit        = 0;
    bar_nav_state.taps         = 0;
    bar_nav_state.last_press_ms = 0;
}

static int8_t seq_digit_from_keycode(uint16_t keycode) {
    switch (keycode) {
        case KC_0: return 0;
        case KC_1: return 1;
        case KC_2: return 2;
        case KC_3: return 3;
        case KC_4: return 4;
        case KC_5: return 5;
        case KC_6: return 6;
        case KC_7: return 7;
        case KC_8: return 8;
        case KC_9: return 9;
        default: return -1;
    }
}

static void seq_bar_nav_handle_digit(uint8_t digit, uint32_t now) {
    uint8_t base = (digit == 0) ? 10 : digit;
    if (!bar_nav_state.active || bar_nav_state.digit != digit ||
        TIMER_DIFF_32(now, bar_nav_state.last_press_ms) > SEQ_DOUBLE_TAP_MS) {
        bar_nav_state.active = true;
        bar_nav_state.digit  = digit;
        bar_nav_state.taps   = 1;
    } else if (bar_nav_state.taps < 12) {
        bar_nav_state.taps++;
    }
    bar_nav_state.last_press_ms = now;

    uint32_t value = (uint32_t)base * (uint32_t)bar_nav_state.taps;
    if (seq_shift_active()) {
        value *= 2u;
    }
    if (value == 0) {
        value = 1;
    }
    if (value > SEQ_MAX_BARS) {
        value = SEQ_MAX_BARS;
    }

    seq_set_view_bar((uint16_t)(value - 1u));
    follow_mode = false;
    switch_state.consumed = true;
}

static void seq_track_length_reset(uint8_t track) {
    seq_track_length_input_t *input = &track_length_input[track % 12];
    input->active = false;
    input->step_mode = false;
    input->value  = 0;
    input->digits = 0;
}

static bool seq_track_length_cancel_all(void) {
    bool cancelled = false;
    for (uint8_t i = 0; i < 12; i++) {
        if (!track_length_input[i].active) {
            continue;
        }
        seq_track_length_reset(i);
        cancelled = true;
    }
    return cancelled;
}

static bool seq_track_length_handle_digit(uint8_t digit, bool pressed, uint32_t now) {
    if (!pressed || !seq_state.active) {
        return false;
    }

    bool consumed = false;
    for (uint8_t track = 0; track < 12; track++) {
        if (!track_held[track]) {
            continue;
        }

        seq_track_length_input_t *input = &track_length_input[track];
        if (!input->active) {
            continue;
        }

        uint32_t held_ms = TIMER_DIFF_32(now, track_press_ms[track]);
        if (held_ms < SEQ_TRACK_LENGTH_CAPTURE_DELAY_MS) {
            continue;
        }

        uint8_t max_digits = input->step_mode ? 4 : 3;
        if (input->digits >= max_digits) {
            continue;
        }

        uint32_t limit = input->step_mode ? SEQ_MAX_STEPS : SEQ_MAX_BARS;
        uint32_t next  = (uint32_t)input->value * 10u + digit;
        if (next > limit) {
            next = limit;
        }

        input->value  = (uint16_t)next;
        input->digits++;
        consumed = true;
    }

    return consumed;
}

static int8_t seq_first_held_track(void) {
    for (uint8_t i = 0; i < 12; i++) {
        if (track_held[i]) {
            return (int8_t)i;
        }
    }
    return -1;
}

static bool seq_track_length_start_step_mode(uint32_t now) {
    for (uint8_t track = 0; track < 12; track++) {
        if (!track_held[track]) {
            continue;
        }

        seq_track_length_input_t *input = &track_length_input[track];
        if (!input->active) {
            continue;
        }

        uint32_t held_ms = TIMER_DIFF_32(now, track_press_ms[track]);
        if (held_ms < SEQ_TRACK_LENGTH_CAPTURE_DELAY_MS) {
            continue;
        }

        input->step_mode = true;
        input->value     = 0;
        input->digits    = 0;
        return true;
    }

    return false;
}

static void seq_track_length_finalize(uint8_t track) {
    seq_track_length_input_t *input = &track_length_input[track % 12];
    if (input->digits == 0) {
        seq_track_length_reset(track);
        return;
    }

    if (input->step_mode) {
        uint16_t steps = input->value;
        if (steps < 1) {
            steps = 1;
        } else if (steps > SEQ_MAX_STEPS) {
            steps = SEQ_MAX_STEPS;
        }
        seq_track_set_steps(track, steps);
    } else {
        uint16_t bars = input->value;
        if (bars == 0) {
            bars = 1;
        } else if (bars > SEQ_MAX_BARS) {
            bars = SEQ_MAX_BARS;
        }
        seq_track_set_bars(track, bars);
    }

    seq_track_length_reset(track);
}
typedef struct {
    bool     pressed;
    bool     hold_active;
    bool     negative;
    uint8_t  pending_taps;
    uint32_t press_time;
    uint32_t last_tap_ms;
    int8_t   applied_delta;
    uint8_t  target_track;
} seq_octave_hold_state_t;

static seq_octave_hold_state_t octave_hold_positive = {0};
static seq_octave_hold_state_t octave_hold_negative = {0};

static void seq_midi_stop_melodic_track(uint8_t track_index, uint32_t now);
static void set_view_slot(uint8_t slot);
static void seq_select_root_track(uint8_t track);
static inline bool slot_is_melodic(uint8_t slot);
static inline bool slot_is_wrapper(uint8_t slot);
static inline uint8_t slot_melodic_track(uint8_t slot);
static inline bool slot_viewing_child(uint8_t slot);
static inline bool slot_uses_melodic(uint8_t slot);
static inline bool slot_has_drum_layer(uint8_t slot);
static void seq_slot_mark_drum(uint8_t slot, bool preserve_melodic);
static bool seq_track_register_tap(uint32_t *last_tap_ms, uint8_t *tap_count, uint8_t track, uint32_t now);
static void seq_track_reset_tap(uint8_t *tap_count, uint8_t track);
static bool seq_track_kind_matches(uint8_t track, seq_slot_kind_t target_kind);
static bool seq_track_confirmation_active(void);
static void seq_track_prompt_confirmation(uint8_t track, seq_slot_kind_t target_kind, bool switch_after);
static void seq_track_accept_confirmation(uint32_t now);
static void seq_track_cancel_confirmation(void);
static bool seq_track_request_kind(uint8_t track, seq_slot_kind_t target_kind, bool switch_after, uint32_t now);
static void seq_track_apply_kind(uint8_t track, seq_slot_kind_t target_kind, uint32_t now);
static void seq_track_switch(uint8_t track);
static void seq_apply_layout(seq_layout_mode_t mode);
static bool seq_handle_melodic_keyboard(uint16_t keycode, bool pressed, uint32_t now);
static void seq_octave_hold_tick(uint32_t now);
static void seq_octave_hold_press(bool negative, uint32_t now);
static void seq_octave_hold_release(bool negative, uint32_t now);
static bool seq_track_length_handle_digit(uint8_t digit, bool pressed, uint32_t now);
static void seq_track_length_reset(uint8_t track);
static void seq_track_length_finalize(uint8_t track);
static void seq_track_double_length(uint8_t slot, bool duplicate_contents);
static void seq_record_reset(void);
static void seq_record_flush(uint32_t end_step);
static void seq_record_try_begin(uint8_t slot, bool is_melodic, uint16_t mask, uint32_t start_step);
static void seq_record_update_mask(uint8_t slot, bool is_melodic, uint16_t mask);
static void seq_record_step_tick(uint32_t global_previous, uint32_t global_next);
static void seq_record_on_transport_stop(uint32_t end_step);
static void seq_record_try_disarm(uint32_t end_step);
static inline uint8_t slot_drum_track(uint8_t slot) {
    return seq_slots[slot % 12].drum_track % 12;
}

static inline uint8_t midi_channel_for_track(uint8_t track);
static inline uint8_t drum_note_for_track(uint8_t track);

static void seq_slot_mark_melodic(uint8_t slot, bool preserve_drum) {
    seq_slot_t *s = &seq_slots[slot % 12];
    bool        was_melodic = (s->kind == SEQ_SLOT_KIND_MELODIC);

    s->kind = SEQ_SLOT_KIND_MELODIC;
    if (s->melodic_track >= MEL_TRACK_COUNT) {
        s->melodic_track = slot % MEL_TRACK_COUNT;
    }

    if (!was_melodic) {
        mel_track_reset(s->melodic_track);
        melodic_octave_shift[s->melodic_track]  = 0;
        melodic_hold_offset[s->melodic_track]   = 0;
        melodic_last_semitone[s->melodic_track] = 0;
        melodic_rest_pending[s->melodic_track]  = false;
        melodic_tie_pending[s->melodic_track]   = false;
        melodic_key_mask[s->melodic_track]      = 0;
        melodic_last_mask[s->melodic_track]     = 0;
    }

    seq_midi_stop_melodic_track(s->melodic_track, timer_read32());

    if (!preserve_drum && s->drum_track < 12) {
        seq_ratchet_clear_track(slot % 12);
        seq_midi_stop_drum_track(s->drum_track, timer_read32());
        seq_drum_pool_clear_track(s->drum_track);
        drum_deleted_mod_note[s->drum_track] = 0xFF;
        drum_note_gate_mask[s->drum_track] = 0;
        drum_active_note[s->drum_track]    = 0;
    }
    if (s->drum_track < 12) {
        memset(drum_lane_muted[s->drum_track], 0, sizeof(drum_lane_muted[s->drum_track]));
    }
    if (s->melodic_track < MEL_TRACK_COUNT) {
        melodic_track_muted[s->melodic_track] = false;
    }
    seq_track_sync_position(slot);
    seq_cc_automation_clear_track(slot % 12);
    seq_mark_dirty();
}

static void seq_slot_enable_wrapper(uint8_t slot) {
    seq_slot_t *s = &seq_slots[slot % 12];
    seq_slot_mark_melodic(slot, true);
    s->wraps_drum = true;
    if (s->drum_track >= 12) {
        s->drum_track = slot % 12;
    }
}

static void __attribute__((unused)) seq_slot_enter_wrapper_child(uint8_t slot) {
    uint8_t normalized = slot % 12;
    seq_slot_enable_wrapper(normalized);

    if (view_child_active && view_child_slot == normalized) {
        view_child_active = false;
        return;
    }

    mel_track_cancel_pending(seq_slots[normalized].melodic_track % MEL_TRACK_COUNT);
    set_view_slot(normalized);
    view_child_slot   = normalized;
    view_child_active = true;
}

static inline bool slot_is_melodic(uint8_t slot) {
    return seq_slots[slot % 12].kind == SEQ_SLOT_KIND_MELODIC;
}

static inline bool slot_is_wrapper(uint8_t slot) {
    return seq_slots[slot % 12].wraps_drum;
}

static inline uint8_t slot_melodic_track(uint8_t slot) {
    uint8_t value = seq_slots[slot % 12].melodic_track;
    return (value < MEL_TRACK_COUNT) ? value : (slot % MEL_TRACK_COUNT);
}

static inline bool slot_viewing_child(uint8_t slot) {
    return slot_is_wrapper(slot) && view_child_active && view_child_slot == (slot % 12);
}

static inline bool slot_uses_melodic(uint8_t slot) {
    return slot_is_melodic(slot) && !slot_viewing_child(slot);
}

static inline bool slot_has_drum_layer(uint8_t slot) {
    if (!slot_is_melodic(slot)) {
        return true;
    }
    return slot_is_wrapper(slot);
}

static uint16_t slot_step_mask(uint8_t slot, uint16_t bar) {
    uint16_t length = seq_track_length_steps(slot);
    if (!length) {
        length = SEQ_STEPS_PER_BAR;
    }
    uint16_t window_start = bar * SEQ_STEPS_PER_BAR;
    if (window_start >= length) {
        return 0;
    }

    uint16_t mask;
    if (slot_uses_melodic(slot)) {
        mask = mel_track_window_mask(slot_melodic_track(slot), window_start);
    } else {
        uint8_t  channel   = slot_drum_track(slot);
        uint16_t note_mask = (uint16_t)(1u << drum_active_note[channel]);
        if (slot == (view_slot % 12)) {
            note_mask = note_hold_mask ? note_hold_mask : note_mask;
        }
        mask = 0;
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            if (!(note_mask & (1u << note))) {
                continue;
            }
            mask |= seq_drum_window_mask(channel, note, bar);
        }
    }

    uint16_t remaining = length - window_start;
    if (remaining >= SEQ_STEPS_PER_BAR) {
        return mask;
    }
    uint16_t allowed_mask = (uint16_t)((1u << remaining) - 1u);
    return (uint16_t)(mask & allowed_mask);
}

static uint8_t seq_scale_apply(uint8_t track, uint8_t note) {
    if (seq_chord_mode[track]) return note;
    const seq_scale_track_t *t = &seq_scale_tracks[track];
    if (t->mode == 2 || (t->mode == 0 && !seq_scale_global.enabled)) return note;
    return seq_scale_quantize(note, t->mode == 1 ? t->root : seq_scale_global.root,
                              t->mode == 1 ? t->kind : seq_scale_global.kind, t->direction);
}

#include "seq_chord_core.inc"
#include "seq_palette_chord.inc"

/* Palette mode supplies actual pitches; transpose mode retains picked fallback. */
static uint8_t seq_melodic_euclid_fallback(uint8_t track, uint16_t step, uint8_t *notes) {
    if (!euclid_tracks[track].note_count) return 0;
    if (euclid_tracks[track].pitch_mode == 2) {
        uint16_t hit;
        if (!seq_euclid_step_hit(track, step, &hit, NULL)) return 0;
        uint8_t slot = euclid_tracks[track].palette_pin < euclid_tracks[track].note_count ? euclid_tracks[track].palette_pin : hit % euclid_tracks[track].note_count;
        return seq_palette_chord_notes(track, slot, notes);
    }
    uint16_t keys = euclid_tracks[track].pitch_mode == 0 ? 0 : melodic_key_mask[track] ? melodic_key_mask[track] : melodic_last_mask[track];
    uint8_t count = 0;
    for (uint8_t semitone = 0; semitone < SEQ_MELODIC_KEY_COUNT; semitone++) {
        if (keys & (1u << semitone)) notes[count++] = seq_melodic_note_pitch(track, semitone);
    }
    if (!count) notes[count++] = seq_melodic_note_pitch(track, euclid_tracks[track].palette_root);
    return count;
}

static uint8_t seq_melodic_euclid_pitch(uint8_t track, uint8_t base, int8_t offset, bool stored) {
    int16_t pitch = base + ((euclid_tracks[track].pitch_mode == 1 || (!stored && euclid_tracks[track].pitch_mode == 0)) ? offset : 0);
    return pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
}

static uint16_t seq_melodic_euclid_window_mask(uint8_t track, uint16_t bar) {
    uint16_t mask = 0;
    uint16_t length = seq_track_length_steps(track);
    for (uint8_t i = 0; i < 16; i++) {
        uint32_t step = (uint32_t)bar * 16u + i;
        if (step < length && seq_euclid_step_hit(track, step, NULL, NULL)) mask |= (uint16_t)(1u << i);
    }
    return mask;
}


static uint16_t seq_slot_playback_mask(uint8_t slot, uint16_t bar) {

    uint8_t track = slot_melodic_track(slot);
    if (slot_uses_melodic(slot) && seq_modules[track].kind == SEQ_MODULE_EUCLID && euclid_tracks[track].enabled) return seq_melodic_euclid_window_mask(track, bar);
    return slot_step_mask(slot, bar);
}

static void seq_euclid_capture_window_template(uint32_t now) {
    uint8_t track = seq_euclid_active_track();
    uint8_t slot  = track % 12;

    uint16_t mask = slot_step_mask(slot, view_bar);
    uint16_t steps = SEQ_STEPS_PER_BAR;
    uint16_t length = seq_track_length_steps(slot);
    if (length && length < steps) {
        steps = length;
    }
    if (!steps) {
        steps = 1;
    }

    uint16_t fills = (uint16_t)__builtin_popcount((uint32_t)mask);
    if (fills > steps) {
        fills = steps;
    }

    seq_euclid_enable(track, true);
    seq_euclid_seed_pool(track);
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return;
    state->steps  = steps;
    state->fills  = fills;
    state->rotate             = 0;
    state->template_active    = true;
    state->template_mask      = mask;
    seq_euclid_sync_track(track);
    seq_mark_dirty();
    seq_euclid_focus_track(track);
    seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_FILLS, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_FILLS), now);
}

static bool slot_has_activity(uint8_t slot) {
    uint8_t normalized = slot % 12;
    seq_slot_t *s = &seq_slots[normalized];

    bool drum_active = false;
    if (slot_has_drum_layer(normalized)) {
        uint8_t drum_channel = slot_drum_track(normalized) % 12;
        drum_active = seq_drum_has_activity(drum_channel);
        if (euclid_tracks[drum_channel].enabled && euclid_tracks[drum_channel].fills > 0) {
            drum_active = true;
        }
    }

    bool melodic_active = false;
    if (s->kind == SEQ_SLOT_KIND_MELODIC || s->wraps_drum) {
        melodic_active = mel_track_has_activity(s->melodic_track % MEL_TRACK_COUNT);
    }

    if (slot_is_melodic(normalized) && !s->wraps_drum) {
        melodic_active = mel_track_has_activity(slot_melodic_track(normalized));
    }

    if (!slot_is_melodic(normalized)) {
        return drum_active;
    }
    if (s->wraps_drum) {
        return drum_active || melodic_active;
    }
    return melodic_active;
}

static void seq_mute_pending_reset(void) {
    mute_pending.active       = false;
    mute_pending.track        = 0;
    mute_pending.timestamp_ms = 0;
}

static inline bool melodic_active_contains(uint8_t track, uint8_t note) {
    uint8_t byte = note >> 3;
    uint8_t mask = (uint8_t)(1u << (note & 7));
    return (melodic_active_map[track % MEL_TRACK_COUNT][byte] & mask) != 0;
}

static inline bool melodic_active_any(uint8_t track) {
    track %= MEL_TRACK_COUNT;
    for (uint8_t i = 0; i < sizeof(melodic_active_map[track]); i++) {
        if (melodic_active_map[track][i]) {
            return true;
        }
    }
    return false;
}

static inline void melodic_active_add(uint8_t track, uint8_t note) {
    track %= MEL_TRACK_COUNT;
    uint8_t byte = note >> 3;
    uint8_t mask = (uint8_t)(1u << (note & 7));
    melodic_active_map[track][byte] |= mask;
    melodic_gate_mask |= (uint16_t)(1u << track);
}

static inline bool melodic_active_remove(uint8_t track, uint8_t note) {
    track %= MEL_TRACK_COUNT;
    uint8_t byte = note >> 3;
    uint8_t mask = (uint8_t)(1u << (note & 7));
    uint8_t *cell = &melodic_active_map[track][byte];
    if (!(*cell & mask)) {
        return false;
    }
    *cell &= (uint8_t)~mask;
    if (!melodic_active_any(track)) {
        melodic_gate_mask &= (uint16_t)~(1u << track);
    }
    return true;
}

static inline void melodic_active_clear(uint8_t track) {
    track %= MEL_TRACK_COUNT;
    memset(melodic_active_map[track], 0, sizeof(melodic_active_map[track]));
    melodic_gate_mask &= (uint16_t)~(1u << track);
}

static void melodic_active_note_off_all(uint8_t track, uint32_t now) {
    track %= MEL_TRACK_COUNT;
    uint8_t midi_ch = midi_channel_for_track(track);
    for (uint8_t byte = 0; byte < sizeof(melodic_active_map[track]); byte++) {
        uint8_t bits = melodic_active_map[track][byte];
        if (!bits) {
            continue;
        }
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (bits & (1u << bit)) {
                uint8_t note = (uint8_t)((byte << 3) | bit);
                virtual_midi_note_off(midi_ch, note, now);
            }
        }
    }
    melodic_active_clear(track);
    seq_accumulator_clear_active(track);
}

static void seq_drum_lane_toggle_mute(uint8_t slot, uint8_t lane, uint32_t now) {
    uint8_t channel = slot_drum_track(slot);
    lane %= SEQ_DRUM_NOTE_COUNT;
    bool mute = !drum_lane_muted[channel][lane];
    if (mute) {
        if (drum_note_gate_mask[channel] & (1u << lane)) {
            virtual_midi_note_off(midi_channel_for_track(channel), drum_note_for_track(lane), now);
            drum_note_gate_mask[channel] &= (uint16_t)~(1u << lane);
        }
    }
    drum_lane_muted[channel][lane] = mute;
    seq_mark_dirty();
}

static void seq_melodic_toggle_mute(uint8_t slot, uint32_t now) {
    uint8_t track = slot_melodic_track(slot);
    bool    mute  = !melodic_track_muted[track];
    if (mute) {
        seq_midi_stop_melodic_track(track, now);
    }
    melodic_track_muted[track] = mute;
    seq_mark_dirty();
}

static void seq_mute_clear_all(uint32_t now) {
    (void)now;
    for (uint8_t ch = 0; ch < 12; ch++) {
        for (uint8_t lane = 0; lane < SEQ_DRUM_NOTE_COUNT; lane++) {
            drum_lane_muted[ch][lane] = false;
        }
    }
    for (uint8_t track = 0; track < MEL_TRACK_COUNT; track++) {
        melodic_track_muted[track] = false;
    }
    seq_mark_dirty();
}

/* Persistent storage helpers */
static inline void seq_midi_all_notes_off(uint32_t now);
static void seq_module_export_state(uint8_t track, const seq_euclid_track_t *e, seq_module_t *m) {
    *m = seq_modules[track];
    if (m->kind == SEQ_MODULE_EUCLID) {
        m->config.euclid            = (seq_module_euclid_t){.enabled = e->enabled, .steps = e->steps, .fills = e->fills, .rotation = e->rotate, .probability = e->probability, .accent = e->swing_amount, .ratchet = e->ratchet_threshold, .root = e->palette_root, .count = e->note_count, .pin = e->palette_pin, .pitch_mode = e->pitch_mode, .template_active = e->template_active, .template_mask = e->template_mask};
        memcpy(m->config.euclid.pool, e->note_pool, 8);
        memcpy(m->config.euclid.qualities, seq_modules[track].config.euclid.qualities, 8);
        if (m->composed && m->rhythm.kind == 2) {
            seq_module_t legacy = *m; legacy.composed = false;
            m->rhythm = seq_module_rhythm(&legacy);
        }
    }
}
static void seq_module_export(uint8_t track, seq_module_t *m) {
    seq_module_export_state(track, &euclid_tracks[track], m);
}
static void seq_module_export_selected(uint8_t track, seq_module_t *m) {
    seq_module_export_state(track, seq_euclid_selected(track, false), m);
}
static void seq_module_install(uint8_t track, const seq_module_t *m) {
    seq_palette_cancel();
    seq_module_preview_stop(timer_read32());
    seq_modules[track] = *m;
    if (m->kind == SEQ_MODULE_EUCLID) {
        seq_module_euclid_t    component = m->config.euclid;
        seq_rhythm_component_t r         = seq_module_rhythm(m);
        component.enabled                = r.kind == 2 && r.enabled;
        component.steps                  = r.steps ? r.steps : 16;
        component.fills                  = r.fills;
        component.rotation               = r.rotation;
        component.probability            = r.probability;
        component.accent                 = r.accent;
        component.ratchet                = r.ratchet;
        component.template_active        = r.template_active;
        component.template_mask          = r.template_mask;
        const seq_module_euclid_t *e     = &component;
        euclid_tracks[track]             = (seq_euclid_track_t){.palette_authored = true, .enabled = e->enabled, .steps = e->steps, .fills = e->fills, .rotate = e->rotation, .probability = e->probability, .swing_amount = e->accent, .ratchet_threshold = e->ratchet, .palette_root = e->root, .note_count = e->count, .palette_pin = e->pin, .pitch_mode = e->pitch_mode, .template_active = e->template_active, .template_mask = e->template_mask};
        memcpy(euclid_tracks[track].note_pool, e->pool, 8);
    } else if (!slot_is_melodic(track)) {
        seq_slots[track].kind       = SEQ_SLOT_KIND_MELODIC;
        seq_slots[track].wraps_drum = true;
    }
    if (track == view_slot) {
        euclid_mode_active = false;
        cmd_state.held     = false;
        euclid_edit.active = false;
        euclid_param_mode  = EUCLID_PARAM_MODE_NONE;
        seq_step_edit_reset();
    }
    seq_mark_dirty();
}
#include "seq_storage.inc"

static void seq_cc_automation_clear_track(uint8_t track) {
    seq_automation_clear(track % 12);
    seq_mark_dirty();
}

static void seq_cc_automation_trim(uint8_t track, uint16_t max_step) {
    seq_automation_trim(track % 12, max_step);
    seq_mark_dirty();
}

static void seq_cc_automation_record(uint8_t track, uint8_t index, uint16_t step, uint8_t value) {
    if (!seq_automation_record(track % 12, index, step, value)) {
        seq_drum_pool_report_full();
        return;
    }
    seq_mark_dirty();
}

static void seq_cc_automation_apply(uint8_t track, uint16_t step, uint32_t now) {
    if (cc_automation_manual[track % 12]) return;
    seq_cc_block_record = true;
    for (uint16_t i = 0; i < seq_automation_count(); i++) {
        const seq_cc_event_t *event = seq_automation_event(i);
        if (event->track == track && event->step == step) seq_cc_set_value(track, event->index, event->value, now);
    }
    seq_cc_block_record = false;
}

static void seq_handle_space_track_press(uint8_t track, uint32_t now) {
    track %= 12;
    bool track_is_melodic = slot_is_melodic(track);
    bool track_has_drum   = slot_has_drum_layer(track);

    if (!mute_pending.active || TIMER_DIFF_32(now, mute_pending.timestamp_ms) > SEQ_DOUBLE_TAP_MS) {
        mute_pending.track        = track;
        mute_pending.timestamp_ms = now;
        mute_pending.active       = true;
        return;
    }

    if (mute_pending.track == track) {
        if (track_is_melodic) {
            seq_melodic_toggle_mute(track, now);
        } else if (track_has_drum) {
            uint8_t lane = track % SEQ_DRUM_NOTE_COUNT;
            seq_drum_lane_toggle_mute(track, lane, now);
        }
        seq_mute_pending_reset();
        return;
    }

    if (slot_has_drum_layer(mute_pending.track)) {
        uint8_t lane = track % SEQ_DRUM_NOTE_COUNT;
        seq_drum_lane_toggle_mute(mute_pending.track, lane, now);
        seq_mute_pending_reset();
        return;
    }

    mute_pending.track        = track;
    mute_pending.timestamp_ms = now;
}

static bool seq_is_option_key(uint16_t keycode) {
    switch (keycode) {
        case KC_LALT:
        case KC_RALT:
#ifdef KC_LOPT
        case KC_LOPT:
#endif
#ifdef KC_ROPT
        case KC_ROPT:
#endif
#ifdef KC_LOPTN
        case KC_LOPTN:
#endif
#ifdef KC_ROPTN
        case KC_ROPTN:
#endif
            return true;
        default:
            return false;
    }
}

static bool seq_is_cmd_key(uint16_t keycode) {
    switch (keycode) {
        case KC_LGUI:
        case KC_RGUI:
#ifdef KC_LCMD
        case KC_LCMD:
#endif
#ifdef KC_RCMD
        case KC_RCMD:
#endif
#ifdef KC_LCMMD
        case KC_LCMMD:
#endif
#ifdef KC_RCMMD
        case KC_RCMMD:
#endif
            return true;
        default:
            return false;
    }
}

static bool seq_shift_pressed(void) {
    uint8_t mods = get_mods();
    return (mods & (MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT))) != 0;
}

static inline void seq_cc_load_mapping(uint8_t index, seq_cc_mapping_t *out) {
    memcpy_P(out, &cc_mappings[index], sizeof(seq_cc_mapping_t));
}

static inline uint16_t seq_progmem_u16(const uint16_t *ptr) {
    return pgm_read_word(ptr);
}

static inline uint8_t seq_progmem_u8(const uint8_t *ptr) {
    return pgm_read_byte(ptr);
}

static int8_t seq_cc_find_mapping(uint16_t keycode) {
    for (uint8_t i = 0; i < SEQ_CC_MAPPING_COUNT; i++) {
        seq_cc_mapping_t mapping;
        seq_cc_load_mapping(i, &mapping);
        if (mapping.keycode == keycode) {
            return (int8_t)i;
        }
    }
    return -1;
}

static void seq_cc_end_session(bool revert, uint32_t now);

static uint8_t seq_cc_base_track(void) {
    return cc_context.active ? cc_context.track : (uint8_t)(view_slot % 12);
}

static uint8_t seq_cc_current_track(void) {
    if (cc_context.hold_override_active) {
        return cc_context.hold_track % 12;
    }
    return seq_cc_base_track();
}

static void seq_cc_send_value(uint8_t track, uint8_t index, uint8_t value, uint32_t now) {
    if (index >= SEQ_CC_MAPPING_COUNT) {
        return;
    }
    seq_cc_mapping_t mapping;
    seq_cc_load_mapping(index, &mapping);
    uint8_t channel = mapping.channel;
    if (channel == 0xFF) {
        channel = midi_channel_for_track(track % 12);
    }
    virtual_midi_control_change(channel, mapping.controller, value, now);
}

static void seq_cc_set_value(uint8_t track, uint8_t index, uint8_t value, uint32_t now) {
    if (index >= SEQ_CC_MAPPING_COUNT) {
        return;
    }
    track %= 12;
    if (seq_state.running && !cc_automation_manual[track] && !seq_cc_block_record && !cc_session.temporary && !cc_mode.temporary && !cc_mode.latched_temporary) {
        seq_cc_automation_record(track, index, (uint16_t)seq_track_step_position[track], value);
    }
    if (cc_values[track][index] != value) {
        cc_values[track][index] = value;
        if (!seq_cc_block_record) {
            seq_mark_dirty();
        }
    }
    seq_cc_send_value(track, index, value, now);
    seq_cc_pending[track] &= (uint16_t)~(1u << index);
}

static void seq_cc_start_session(uint8_t track, uint8_t index, bool temporary, uint32_t now) {
    if (index >= SEQ_CC_MAPPING_COUNT) {
        return;
    }
    track %= 12;
    if (cc_session.active) {
        seq_cc_end_session(cc_session.temporary, now);
    }
    cc_session.active        = true;
    cc_session.mapping_index = index;
    cc_session.temporary     = temporary;
    cc_session.prev_value    = cc_values[track][index];
    cc_session.track         = track;
    (void)now;
}

static void seq_cc_end_session(bool revert, uint32_t now) {
    if (!cc_session.active) {
        return;
    }
    if (revert) {
        seq_cc_set_value(cc_session.track, cc_session.mapping_index, cc_session.prev_value, now);
    }
    cc_session.active = false;
}

static void seq_cc_adjust_value(int8_t delta, uint32_t now) {
    if (!cc_session.active) {
        return;
    }
    uint8_t track = cc_session.track;
    uint8_t index = cc_session.mapping_index;

    int16_t step = (int16_t)delta * 3;
    if (seq_shift_pressed()) {
        step *= 5;
    }

    int16_t value = (int16_t)cc_values[track][index] + step;
    if (value < 0) {
        value = 0;
    } else if (value > 127) {
        value = 127;
    }
    if (value == cc_values[track][index]) {
        return;
    }
    seq_cc_set_value(track, index, (uint8_t)value, now);
}

static void seq_cc_revert_snapshot(uint8_t track, uint32_t now) {
    track %= 12;
    for (uint8_t i = 0; i < SEQ_CC_MAPPING_COUNT; i++) {
        if (cc_values[track][i] != cc_snapshot[track][i]) {
            seq_cc_set_value(track, i, cc_snapshot[track][i], now);
        }
    }
}

static void seq_cc_enter_mode(uint8_t track, bool temporary, uint32_t now) {
    track %= 12;
    if (cc_context.active) {
        cc_context.track = track;
        if (temporary) {
            cc_mode.latched_temporary = true;
            cc_mode.snapshot_track    = track;
            memcpy(cc_snapshot[track], cc_values[track], sizeof(cc_snapshot[track]));
        }
        return;
    }
    cc_context.active               = true;
    cc_context.prev_track           = view_slot % 12;
    cc_context.track                = track;
    cc_context.track_override       = (track != cc_context.prev_track);
    cc_context.hold_override_active = false;
    if (cc_context.track_override) {
        seq_select_root_track(track);
    }
    if (temporary) {
        cc_mode.latched_temporary = true;
        cc_mode.snapshot_track    = track;
        memcpy(cc_snapshot[track], cc_values[track], sizeof(cc_snapshot[track]));
    } else {
        cc_mode.latched_temporary = false;
    }
    (void)now;
}

static void seq_cc_exit_mode(uint32_t now) {
    if (!cc_context.active) {
        return;
    }
    if (cc_session.active) {
        seq_cc_end_session(cc_session.temporary, now);
    }
    if (cc_mode.latched_temporary) {
        seq_cc_revert_snapshot(cc_mode.snapshot_track, now);
        cc_mode.latched_temporary = false;
    }
    if (cc_context.track_override) {
        seq_select_root_track(cc_context.prev_track);
    }
    cc_context = (seq_cc_context_t){0};
    cc_mode.latched        = false;
    cc_mode.simple_toggle  = false;
    (void)now;
}

static bool seq_cc_handle_cmd(uint16_t keycode, bool pressed, uint32_t now) {
    if (!seq_is_cmd_key(keycode)) {
        return false;
    }

    cmd_state.tap_count   = 0;
    cmd_state.last_tap_ms = 0;

    if (pressed) {
        cc_mode.held                 = true;
        cc_mode.consumed             = false;
        cc_mode.temporary            = seq_shift_pressed();
        cc_mode.press_time           = now;
        cc_mode.pending_track_valid  = false;
        cc_mode.simple_toggle        = false;

        if (cc_context.hold_override_active) {
            cc_mode.pending_track       = cc_context.hold_track % 12;
            cc_mode.pending_track_valid = true;
        } else {
            for (uint8_t i = 0; i < 12; i++) {
                if (track_held[i]) {
                    cc_mode.pending_track       = i;
                    cc_mode.pending_track_valid = true;
                    break;
                }
            }
        }

        return true;
    }

    bool latched_before = cc_mode.latched;

    if (!cc_mode.consumed) {
        if (TIMER_DIFF_32(now, cc_mode.last_tap_ms) <= SEQ_DOUBLE_TAP_MS) {
            if (cc_mode.tap_count < 255) {
                cc_mode.tap_count++;
            }
        } else {
            cc_mode.tap_count = 1;
        }
        cc_mode.last_tap_ms = now;

        if (cc_mode.tap_count >= 2) {
            cc_mode.latched = !cc_mode.latched;
            if (cc_mode.latched) {
                uint8_t track = seq_cc_base_track();
                if (cc_mode.pending_track_valid) {
                    track = cc_mode.pending_track % 12;
                } else {
                    for (uint8_t i = 0; i < 12; i++) {
                        if (track_held[i]) {
                            track = i;
                            break;
                        }
                    }
                }
                seq_cc_enter_mode(track, cc_mode.temporary, now);
            } else {
                seq_cc_exit_mode(now);
            }
            cc_mode.tap_count  = 0;
            cc_mode.consumed   = true;
            cc_mode.temporary  = false;
            cc_mode.held       = false;
            cc_mode.pending_track_valid = false;
            cc_mode.simple_toggle       = false;
            return true;
        }
    }

    bool revert = cc_session.active && (cc_session.temporary || cc_mode.temporary);
    seq_cc_end_session(revert, now);

    if (!cc_mode.latched && latched_before) {
        seq_cc_exit_mode(now);
    }

    cc_mode.held      = false;
    cc_mode.temporary = false;
    if (cc_mode.consumed) {
        cc_mode.tap_count = 0;
    }
    cc_mode.pending_track_valid = false;
    cc_mode.simple_toggle       = false;
    cc_mode.consumed = false;
    return true;
}

static bool seq_cc_handle_key_event(uint16_t keycode, keyrecord_t *record, uint32_t now) {
    if ((keycode == KC_T || (keycode == KC_PGDN && (seq_shift_active() || seq_shift_pressed()))) && (cc_context.active || cc_mode.held)) {
        if (record->event.pressed) {
            uint8_t track = seq_cc_current_track();
            cc_automation_manual[track] = !cc_automation_manual[track];
            cc_mode.consumed = true;
            seq_mark_dirty();
        }
        return true;
    }
    int8_t mapping_index = seq_cc_find_mapping(keycode);

    if (mapping_index < 0) {
        return false;
    }

    bool cc_active = cc_context.active || cc_mode.held;
    if (!cc_active) {
        return false;
    }

    uint8_t  current_track = seq_cc_current_track();
    uint32_t last_release  = cc_key_last_release[current_track][mapping_index];

    if (record->event.pressed) {
        bool performance = TIMER_DIFF_32(now, last_release) <= SEQ_DOUBLE_TAP_MS;
        bool temporary   = performance || cc_mode.temporary || cc_mode.latched_temporary;
        seq_cc_start_session(current_track, (uint8_t)mapping_index, temporary, now);
        cc_mode.consumed = true;
        cc_key_pressed[current_track][mapping_index] = true;
        return true;
    }

    uint8_t session_track = cc_session.active && cc_session.mapping_index == (uint8_t)mapping_index ? cc_session.track : current_track;
    cc_key_pressed[session_track][mapping_index]    = false;
    cc_key_last_release[session_track][mapping_index] = now;

    if (cc_session.active && cc_session.mapping_index == (uint8_t)mapping_index) {
        seq_cc_end_session(cc_session.temporary, now);
    }

    return true;
}

static bool seq_euclid_print_loop(uint8_t track, uint32_t now);

static bool seq_cmd_handle(uint16_t keycode, keyrecord_t *record, uint32_t now) {
    if (!seq_state.active) {
        return false;
    }

    bool pressed = record->event.pressed;

    if (seq_is_option_key(keycode)) {
        if (pressed) {
            cmd_state.held     = true;
            cmd_state.consumed = seq_shift_active();
            seq_euclid_focus_track(view_slot % 12);
            if (!euclid_mode_active) {
                euclid_edit.active = true;
            }
        } else {
            bool exit_tap = euclid_mode_active && !cmd_state.consumed && !seq_mod_del() && !seq_shift_active();
            bool print_tap = exit_tap && ((seq_ctrl_mask | get_mods()) & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_RCTL)));
            bool double_tap = !exit_tap && !cmd_state.consumed && seq_cmd_register_tap(now);
            if (cmd_state.consumed) cmd_state.tap_count = 0;
            if (euclid_palette_capture.active) {
                if (euclid_palette_capture.primed) {
                    seq_euclid_palette_capture_commit(now);
                } else {
                    seq_euclid_palette_capture_reset();
                }
            }
            cmd_state.held     = false;
            cmd_state.consumed = false;
            if (!euclid_mode_active) {
                euclid_edit.active = false;
            }
            if (exit_tap) {
                cmd_state.tap_count = 0;
                if (!print_tap || seq_euclid_print_loop(view_slot, now)) seq_euclid_mode_exit(false, now);
                return true;
            }
            if (double_tap) {
                if (euclid_mode_active) {
                    bool reset = seq_mod_del();
                    seq_euclid_mode_exit(reset, now);
                } else {
                    seq_euclid_mode_enter(now);
                }
                return true;
            }
        }
        return true;
    }

    if (!seq_euclid_editing_enabled()) {
        return false;
    }

    if (!pressed) {
        return false;
    }

    cmd_state.consumed = true;

    int8_t track_key = track_from_keycode(keycode);
    if (track_key >= 0 && euclid_mode_active && !cmd_state.held) return false;
    if (track_key >= 0) {
        uint8_t track = (uint8_t)(track_key % 12);
        bool enable = !seq_euclid_selected(track, false)->enabled;
        if (euclid_mode_active) {
            enable = true;
        }
        seq_euclid_enable(track, enable);
        seq_euclid_sync_track(track);
        seq_select_root_track(track);
        seq_euclid_focus_track(track);
        euclid_edit.active = seq_euclid_selected(track, false)->enabled || euclid_mode_active;
        seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_FILLS, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_FILLS), now);
        return true;
    }

    return false;
}

static bool seq_euclid_handle_param_key(uint16_t keycode, keyrecord_t *record, uint32_t now) {
    seq_euclid_param_mode_t mode = seq_euclid_param_mode_for_key(keycode);
    if (mode == EUCLID_PARAM_MODE_NONE) {
        return false;
    }

    bool pressed   = record->event.pressed;
    bool consuming = seq_euclid_editing_enabled() || euclid_param_keycode == keycode;
    if (!consuming) {
        return false;
    }

    if (pressed) {
        euclid_param_keycode = keycode;
        euclid_param_mode    = mode;
        uint8_t track        = seq_euclid_active_track();
        seq_euclid_enable(track, true);
        seq_euclid_sync_track(track);
        seq_euclid_capture_preview(track, seq_euclid_param_kind_for_mode(mode), seq_euclid_param_value_for_kind(track, seq_euclid_param_kind_for_mode(mode)), now);
    } else {
        if (euclid_param_keycode == keycode) {
            euclid_param_keycode = KC_NO;
        }
    }
    return true;
}

static bool seq_euclid_apply_encoder(int8_t delta, uint32_t now) {
    if (euclid_param_mode == EUCLID_PARAM_MODE_NONE || delta == 0) {
        return false;
    }

    cmd_state.consumed = true;
    uint8_t track = seq_euclid_active_track();
    seq_euclid_enable(track, true);
    if (euclid_param_mode == EUCLID_PARAM_MODE_FILLS && seq_shift_active()) {
        seq_euclid_adjust_swing(track, delta);
        seq_euclid_focus_track(track);
        seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_SWING, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_SWING), now);
        return true;
    }

    seq_euclid_param_kind_t kind = seq_euclid_param_kind_for_mode(euclid_param_mode);
    if (kind == EUCLID_PARAM_KIND_NONE) {
        return false;
    }

    int16_t step = seq_euclid_param_step_for_mode(euclid_param_mode);
    if (!step) {
        return true;
    }
    int16_t amount = (int16_t)delta * step;

    switch (kind) {
        case EUCLID_PARAM_KIND_FILLS:
            seq_euclid_adjust_fills(track, amount);
            seq_euclid_sync_track(track);
            break;
        case EUCLID_PARAM_KIND_ROTATE:
            seq_euclid_adjust_rotate(track, amount);
            break;
        case EUCLID_PARAM_KIND_STEPS:
            seq_euclid_adjust_steps(track, amount);
            seq_euclid_sync_track(track);
            break;
        case EUCLID_PARAM_KIND_SWING:
            seq_euclid_adjust_swing(track, amount);
            break;
        case EUCLID_PARAM_KIND_PROBABILITY:
            seq_euclid_adjust_probability(track, amount);
            break;
        case EUCLID_PARAM_KIND_RATCHET_THRESHOLD:
            seq_euclid_adjust_threshold(track, amount);
            break;
        default:
            return true;
    }

    seq_euclid_focus_track(track);
    seq_euclid_capture_preview(track, kind, seq_euclid_param_value_for_kind(track, kind), now);
    return true;
}

static bool seq_track_register_tap(uint32_t *last_tap_ms, uint8_t *tap_count, uint8_t track, uint32_t now) {
    if (TIMER_DIFF_32(now, last_tap_ms[track]) <= SEQ_DOUBLE_TAP_MS) {
        tap_count[track]++;
    } else {
        tap_count[track] = 1;
    }
    last_tap_ms[track] = now;
    if (tap_count[track] >= 2) {
        tap_count[track] = 0;
        return true;
    }
    return false;
}

static void seq_track_reset_tap(uint8_t *tap_count, uint8_t track) {
    tap_count[track] = 0;
}

static bool seq_track_kind_matches(uint8_t track, seq_slot_kind_t target_kind) {
    seq_slot_t *slot = &seq_slots[track % 12];
    switch (target_kind) {
        case SEQ_SLOT_KIND_DRUM:
            return slot->kind == SEQ_SLOT_KIND_DRUM && !slot->wraps_drum;
        case SEQ_SLOT_KIND_MELODIC:
            return slot->kind == SEQ_SLOT_KIND_MELODIC && !slot->wraps_drum && !seq_chord_mode[track%12];
        case SEQ_SLOT_KIND_CHORD:
            return slot->kind == SEQ_SLOT_KIND_MELODIC && !slot->wraps_drum && seq_chord_mode[track%12];
        default:
            return false;
    }
}

static bool seq_track_confirmation_active(void) {
    return track_confirm.active;
}

static void seq_track_prompt_confirmation(uint8_t track, seq_slot_kind_t target_kind, bool switch_after) {
    track_confirm.active       = true;
    track_confirm.track        = track % 12;
    track_confirm.target_kind  = target_kind;
    track_confirm.switch_after = switch_after;
    seq_track_reset_tap(track_caps_tap_count, track_confirm.track);
    seq_track_reset_tap(track_tab_tap_count, track_confirm.track);
    seq_track_reset_tap(track_tap_count, track_confirm.track);
    track_caps_last_tap_ms[track_confirm.track] = 0;
    track_tab_last_tap_ms[track_confirm.track]   = 0;
    track_last_tap_ms[track_confirm.track]       = 0;
}

static void seq_track_cancel_confirmation(void) {
    track_confirm.active = false;
}

static void seq_track_switch(uint8_t track) {
    seq_select_root_track(track);
}

static void seq_slot_mark_drum(uint8_t slot, bool preserve_melodic) {
    uint8_t     normalized = slot % 12;
    seq_slot_t *s          = &seq_slots[normalized];
    uint32_t    now        = timer_read32();

    if (s->kind == SEQ_SLOT_KIND_MELODIC && s->melodic_track < MEL_TRACK_COUNT) {
        seq_midi_stop_melodic_track(s->melodic_track, now);
        if (!preserve_melodic) {
            mel_track_reset(s->melodic_track);
            mel_track_cancel_pending(s->melodic_track);
        }
        melodic_track_muted[s->melodic_track] = false;
    }

    s->kind       = SEQ_SLOT_KIND_DRUM;
    s->wraps_drum = false;
    uint8_t drum_index = (s->drum_track < 12) ? s->drum_track : normalized;
    if (drum_index >= 12) {
        drum_index = normalized;
    }
    seq_ratchet_clear_track(normalized);
    seq_midi_stop_drum_track(drum_index, now);
    seq_drum_pool_clear_track(drum_index);
    drum_deleted_mod_note[drum_index] = 0xFF;
    drum_note_gate_mask[drum_index] = 0;
    drum_active_note[drum_index]    = 0;
    memset(drum_lane_muted[drum_index], 0, sizeof(drum_lane_muted[drum_index]));
    s->drum_track = drum_index;
    seq_track_sync_position(normalized);
    seq_cc_automation_clear_track(normalized);
    seq_mark_dirty();
}

static void seq_track_apply_kind(uint8_t track, seq_slot_kind_t target_kind, uint32_t now) {
    if (slot_is_melodic(track) && !slot_is_wrapper(track) && target_kind != SEQ_SLOT_KIND_DRUM) {
        seq_chord_mode_set(track, target_kind == SEQ_SLOT_KIND_CHORD); return;
    }
    uint8_t normalized = track % 12;
    if (target_kind == SEQ_SLOT_KIND_DRUM) {
        seq_slot_mark_drum(normalized, false);
    } else {
        seq_slot_mark_melodic(normalized, false);
        seq_slots[normalized].wraps_drum = false;
    }
    seq_chord_mode_set(normalized, target_kind == SEQ_SLOT_KIND_CHORD);
    (void)now;
}

static void seq_track_accept_confirmation(uint32_t now) {
    if (!track_confirm.active) {
        return;
    }
    uint8_t track = track_confirm.track;
    seq_track_apply_kind(track, track_confirm.target_kind, now);
    if (track_confirm.switch_after) {
        seq_track_switch(track);
    }
    track_confirm.active = false;
}

static bool seq_track_request_kind(uint8_t track, seq_slot_kind_t target_kind, bool switch_after, uint32_t now) {
    uint8_t normalized = track % 12;
    bool    needs_change = !seq_track_kind_matches(normalized, target_kind);

    if (needs_change && normalized == view_slot) {
        seq_track_prompt_confirmation(normalized, target_kind, switch_after);
        return false;
    }

    if (needs_change) {
        seq_track_apply_kind(normalized, target_kind, now);
        if (normalized == (view_slot % 12) && target_kind == SEQ_SLOT_KIND_MELODIC) {
            note_hold_mask = 0;
        }
    }

    if (switch_after) {
        seq_track_switch(normalized);
    }

    return true;
}

static seq_toggle_state_t seq_pgup_toggle  = {0};
static seq_toggle_state_t seq_pgdn_toggle  = {0};
static seq_toggle_state_t seq_esc_toggle   = {0};

static void seq_select_root_track(uint8_t track) {
    seq_chord_cancel();
    uint8_t normalized = track % 12;
    set_view_slot(normalized);
    view_child_active  = false;
    view_child_slot    = normalized;
    note_hold_mask      = 0;
}

static inline uint8_t midi_channel_for_track(uint8_t track) {
    return (uint8_t)(track_midi_channel[track % 12] & 0x0Fu);
}

static inline uint8_t drum_note_for_track(uint8_t note_index) {
    return drum_note_map[note_index % SEQ_DRUM_NOTE_COUNT];
}

static void seq_midi_stop_drum_track(uint8_t track, uint32_t now) {
    uint16_t mask = drum_note_gate_mask[track];
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        if (mask & (1u << note)) virtual_midi_note_off(midi_channel_for_track(track), drum_note_for_track(note), now);
    }
    drum_note_gate_mask[track] = 0;
}

static void seq_midi_stop_drums(uint32_t now) {
    for (uint8_t track = 0; track < 12; track++) seq_midi_stop_drum_track(track, now);
}

static void seq_midi_stop_melodic_track(uint8_t track_index, uint32_t now) {
    if (module_preview.active && module_preview.track == track_index) seq_module_preview_stop(now);
    melodic_active_note_off_all(track_index, now);
}

static void seq_midi_stop_melodic(uint32_t now) {
    for (uint8_t track = 0; track < MEL_TRACK_COUNT; track++) {
        melodic_active_note_off_all(track, now);
    }
}

static inline void seq_midi_all_notes_off(uint32_t now) {
    seq_module_preview_stop(now);
    seq_midi_stop_drums(now);
    seq_midi_stop_melodic(now);
}

static void seq_clear_track(uint8_t track, uint32_t now) {
    seq_pending_erase = false;
    uint8_t normalized = track % 12;
    seq_ratchet_clear_track(normalized);
    seq_slot_t *slot   = &seq_slots[normalized];

    uint8_t drum_index = slot->drum_track % 12;
    uint8_t midi_ch    = midi_channel_for_track(drum_index);
    seq_drum_pool_clear_track(drum_index);
    drum_deleted_mod_note[drum_index] = 0xFF;
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        if (drum_note_gate_mask[drum_index] & (1u << note)) {
            virtual_midi_note_off(midi_ch, drum_note_for_track(note), now);
        }
    }
    drum_note_gate_mask[drum_index] = 0;
    drum_active_note[drum_index]    = 0;

    if (slot_is_melodic(normalized)) {
        uint8_t melodic_index = slot_melodic_track(normalized);
        mel_track_reset(melodic_index);
        mel_track_cancel_pending(melodic_index);
        seq_midi_stop_melodic_track(melodic_index, now);
    }

    mel_last_start_mask &= (uint16_t)~(1u << normalized);
    mel_last_stop_mask  &= (uint16_t)~(1u << normalized);
    seq_track_sync_position(normalized);
    seq_cc_automation_clear_track(normalized);
    seq_mark_dirty();
}

static void set_view_slot(uint8_t slot) {
    if (view_slot != slot % 12) seq_module_preview_stop(timer_read32());
    uint8_t normalized = slot % 12;
    if (view_slot != normalized) {
        seq_slot_t *old_slot = &seq_slots[view_slot];
        if (old_slot->kind == SEQ_SLOT_KIND_MELODIC && !(old_slot->wraps_drum && view_child_active && view_child_slot == view_slot)) {
            if (old_slot->melodic_track < MEL_TRACK_COUNT) {
                mel_track_cancel_pending(old_slot->melodic_track);
            }
        }
    }
    if (view_child_active && view_child_slot != normalized) {
        view_child_active = false;
    }
    view_slot = normalized;
    seq_clamp_view_bar();
    if (euclid_mode_active) {
        seq_euclid_focus_track(normalized);
    }
}

void seq_mode_on_load(void) {
    seq_loop_steps = 0;
    seq_palette_cancel();
    seq_loop_held = false;
    seq_temp_active = seq_temp_enter_held = seq_temp_escape_held = false;
    seq_temp_length = 0;
    seq_temp_restore_pending = false;
    memset(seq_chord_events, 255, sizeof(seq_chord_events));
    memset(seq_chord_mode, 0, sizeof(seq_chord_mode));
    memset(seq_chord_draft, 0, sizeof(seq_chord_draft));
    seq_chord_selected=SEQ_CHORD_NONE;seq_chord_held=seq_chord_note_held=seq_chord_shortcut_keys=0;seq_chord_delete=false;
    seq_chord_parameter=seq_chord_error=seq_chord_edit_track=0;seq_chord_mutating=false;
    for(uint8_t t=0;t<12;t++)seq_chord_draft[t]=(seq_chord_event_t){.track=t,.root=60,.length=4,.velocity=100};
    memset(&seq_step_hold, 0, sizeof(seq_step_hold));
    seq_step_hold_replaying = false;
    seq_scale_global = (seq_scale_context_t){0};
    memset(seq_scale_tracks, 0, sizeof(seq_scale_tracks));
    seq_scale_editing = false;
    seq_palette_cancel();
    seq_palette_digit_held = seq_palette_type_held = 0;
    seq_palette_toggle_held = false;
    seq_scale_revision = 0;
    seq_ctrl_mask = 0;
    memset(seq_modules, 0, sizeof(seq_modules));
    memset(seq_variations, 0, sizeof(seq_variations));
    memset(seq_variation_queued, 0, sizeof(seq_variation_queued));
    seq_variation_pending = seq_variation_revision = seq_variation_playing = seq_variation_rejoin = 0;
    memset(&module_preview, 0, sizeof(module_preview));
    memset(track_last_tap_ms, 0, sizeof(track_last_tap_ms));
    memset(track_press_ms, 0, sizeof(track_press_ms));
    memset(track_tap_count, 0, sizeof(track_tap_count));
    memset(track_tab_last_tap_ms, 0, sizeof(track_tab_last_tap_ms));
    memset(track_tab_tap_count, 0, sizeof(track_tab_tap_count));
    memset(track_caps_last_tap_ms, 0, sizeof(track_caps_last_tap_ms));
    memset(track_caps_tap_count, 0, sizeof(track_caps_tap_count));
    memset(track_held, 0, sizeof(track_held));
    memset(track_length_input, 0, sizeof(track_length_input));
    srand((unsigned)timer_read32());
    seq_automation_init();
    memset(cc_automation_manual, 0, sizeof(cc_automation_manual));
    memset(seq_cc_pending, 0, sizeof(seq_cc_pending));
    seq_transfer_active         = false;
    seq_pending_erase           = false;
    seq_external_clock          = false;
    seq_external_song_position  = 0;
    seq_timing_swing            = 0;
    seq_save_status             = 0;
    note_hold_mask = 0;
    switch_state.held      = false;
    switch_state.stage     = SEQ_SWITCH_STAGE_INACTIVE;
    switch_state.consumed  = false;
    switch_state.synthetic = false;
    bar_nav_state.active       = false;
    bar_nav_state.digit        = 0;
    bar_nav_state.taps         = 0;
    bar_nav_state.last_press_ms = 0;
    seq_modifiers       = 0;
    track_confirm.active = false;
    memset(melodic_octave_shift, 0, sizeof(melodic_octave_shift));
    memset(melodic_hold_offset, 0, sizeof(melodic_hold_offset));
    memset(melodic_rest_pending, 0, sizeof(melodic_rest_pending));
    memset(melodic_key_mask, 0, sizeof(melodic_key_mask));
    memset(melodic_last_mask, 0, sizeof(melodic_last_mask));
    memset(melodic_tie_pending, 0, sizeof(melodic_tie_pending));
    memset(melodic_last_semitone, 0, sizeof(melodic_last_semitone));
    memset(melodic_track_muted, 0, sizeof(melodic_track_muted));
    memset(&octave_hold_positive, 0, sizeof(octave_hold_positive));
    memset(&octave_hold_negative, 0, sizeof(octave_hold_negative));
    seq_euclid_palette_capture_reset();
    seq_chord_capture_clear();
    seq_chord_tap_reset();
    seq_state.active  = false;
    seq_state.running = false;
    seq_running       = false;
    seq_toggle_init(&seq_pgup_toggle);
    seq_toggle_init(&seq_pgdn_toggle);
    seq_toggle_init(&seq_esc_toggle);
    seq_record_reset();
    mel_last_start_mask = 0;
    mel_last_stop_mask  = 0;
    melodic_gate_mask   = 0;
    memset(melodic_active_map, 0, sizeof(melodic_active_map));
    seq_drum_pool_init();
    drum_pool_full_preview = false;
    memset(drum_deleted_mod_note, 0xFF, sizeof(drum_deleted_mod_note));
    memset(drum_note_gate_mask, 0, sizeof(drum_note_gate_mask));
    memset(drum_active_note, 0, sizeof(drum_active_note));
    memset(drum_lane_muted, 0, sizeof(drum_lane_muted));
    seq_drum_pool_reset_mods();
    for (uint8_t track = 0; track < 12; track++) {
        for (uint8_t i = 0; i < SEQ_CC_MAPPING_COUNT; i++) {
            seq_cc_mapping_t mapping;
            seq_cc_load_mapping(i, &mapping);
            cc_values[track][i]          = mapping.default_value;
            cc_snapshot[track][i]        = mapping.default_value;
            cc_key_pressed[track][i]     = false;
            cc_key_last_release[track][i] = 0;
        }
    }
    cc_session = (seq_cc_session_t){0};
    cc_mode    = (seq_cc_mode_t){0};
    cc_context = (seq_cc_context_t){0};
    seq_mute_pending_reset();

    for (uint8_t i = 0; i < 12; i++) {
        track_length_bars[i]  = 1;
        track_length_steps[i] = SEQ_STEPS_PER_BAR;
    }
    memset(seq_track_step_position, 0, sizeof(seq_track_step_position));
    for (uint8_t i = 0; i < 12; i++) {
        drum_track_velocity[i] = SEQ_DRUM_VELOCITY;
    }
    for (uint8_t i = 0; i < MEL_TRACK_COUNT; i++) {
        melodic_track_velocity[i] = 127;
    }
    for (uint8_t i = 0; i < 12; i++) {
        track_midi_channel[i] = (uint8_t)(i % 16u);
    }
    for (uint8_t i = 0; i < SEQ_DRUM_NOTE_COUNT; i++) {
        drum_note_map[i] = (uint8_t)(SEQ_DRUM_NOTE_BASE + i);
    }
    velocity_preview_active = false;
    memset(seq_accumulators, 0, sizeof(seq_accumulators));
    for (uint8_t i = 0; i < 12; i++)
        seq_accumulators[i].direction = 1;
    seq_lane_selection_used = 0;
    memset(euclid_tracks, 0, sizeof(euclid_tracks));
    for (uint8_t i = 0; i < 12; i++) {
        seq_euclid_reset_track(i);
    }
    cmd_state   = (seq_cmd_state_t){0};
    cmd_state.edit_track = seq_config.default_track % 12;
    euclid_edit = (seq_euclid_edit_state_t){0};
    euclid_mode_active   = false;
    euclid_param_mode    = EUCLID_PARAM_MODE_NONE;
    euclid_param_keycode = KC_NO;
    seq_euclid_clear_preview();
    seq_step_edit_reset();
    view_bar          = 0;
    playhead_bar      = 0;
    seq_step_position = 0;
    seq_total_steps   = SEQ_STEPS_PER_BAR;
    follow_mode       = true;

    virtual_midi_init();
    virtual_midi_stop(timer_read32());
    seq_ratchet_clear();

    mel_tracks_init();

    for (uint8_t i = 0; i < 12; i++) {
        seq_slots[i].kind          = SEQ_SLOT_KIND_DRUM;
        seq_slots[i].wraps_drum    = false;
        seq_slots[i].drum_track    = i;
        seq_slots[i].melodic_track = i % MEL_TRACK_COUNT;
    }

    view_child_active = false;
    view_child_slot   = seq_config.default_track % 12;
    set_view_slot(seq_config.default_track);
    seq_storage_load();
    set_view_slot(seq_config.default_track);
    virtual_midi_set_clock_output(!seq_external_clock);
    seq_update_total_steps();
}

void seq_set_default_track(uint8_t track) {
    seq_config.default_track = track % 12;
    if (!seq_state.active) {
        seq_select_root_track(seq_config.default_track);
    }
}

static const uint16_t PROGMEM SEQ_STEP_KEYS_DEFAULT[16] = {
    KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K,
    KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM,
};

static const uint16_t PROGMEM SEQ_STEP_KEYS_GRID_4X4[16] = {
    KC_1, KC_2, KC_3, KC_4,
    KC_Q, KC_W, KC_E, KC_R,
    KC_A, KC_S, KC_D, KC_F,
    KC_Z, KC_X, KC_C, KC_V,
};

static const uint16_t PROGMEM *active_step_keys = SEQ_STEP_KEYS_DEFAULT;

static inline uint16_t seq_step_key(uint8_t index) {
    return pgm_read_word((slot_uses_melodic(view_slot) ? SEQ_STEP_KEYS_DEFAULT : active_step_keys) + index);
}

#define SEQ_SWITCH_GROUP_KEY   KC_S

typedef struct {
    uint16_t keycode;
    int8_t   semitone;
    bool     is_sharp;
} seq_melodic_key_t;

static const seq_melodic_key_t PROGMEM SEQ_MELODIC_KEYS[SEQ_MELODIC_KEY_COUNT] = {
    { KC_Q, 0,  false },
    { KC_2, 1,  true  },
    { KC_W, 2,  false },
    { KC_3, 3,  true  },
    { KC_E, 4,  false },
    { KC_R, 5,  false },
    { KC_5, 6,  true  },
    { KC_T, 7,  false },
    { KC_6, 8,  true  },
    { KC_Y, 9,  false },
    { KC_7, 10, true  },
    { KC_U, 11, false },
};

#define SEQ_MELODIC_KEY(index) ((seq_melodic_key_t){                   \
    .keycode  = seq_progmem_u16(&SEQ_MELODIC_KEYS[index].keycode),     \
    .semitone = (int8_t)pgm_read_byte(&SEQ_MELODIC_KEYS[index].semitone), \
    .is_sharp = (bool)pgm_read_byte(&SEQ_MELODIC_KEYS[index].is_sharp)  \
})

#define SEQ_MELODIC_CONTROL_COUNT 3
static const uint16_t PROGMEM SEQ_MELODIC_CONTROL_KEYS[SEQ_MELODIC_CONTROL_COUNT] = {
    KC_1, /* octave down */
    KC_4, /* rest / tie */
    KC_8, /* octave up */
};

static uint8_t melodic_key_leds[SEQ_MELODIC_KEY_COUNT];
static uint8_t melodic_control_leds[SEQ_MELODIC_CONTROL_COUNT];

static const uint16_t PROGMEM TRACK_KEYS[12] = {
    KC_BRID, KC_BRIU, KC_MCTL, KC_LPAD, RGB_VAD, RGB_VAI,
    KC_MPRV, KC_MPLY, KC_MNXT, KC_MUTE, KC_VOLD, KC_VOLU,
};

static const uint8_t PROGMEM TRACK_HUES[12] = {
    0, 21, 42, 64, 85, 106, 128, 149, 170, 191, 213, 234,
};

#define SEQ_BAR_LED_COUNT 8
static const uint16_t PROGMEM SEQ_BAR_LED_KEYS[SEQ_BAR_LED_COUNT] = {
    KC_9, KC_0, KC_MINUS, KC_EQUAL, KC_O, KC_P, KC_LBRC, KC_RBRC,
};

#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
static uint8_t step_leds[16];
static uint8_t track_leds[12];
static uint8_t bar_leds[SEQ_BAR_LED_COUNT];
static uint8_t number_leds[10];
static uint8_t home_led        = NO_LED;
static uint8_t grave_led       = NO_LED;
static uint8_t backslash_led   = NO_LED;
static uint8_t esc_led         = NO_LED;
static uint8_t equal_led       = NO_LED;
static uint8_t confirm_led_yes = NO_LED;
static uint8_t confirm_led_no  = NO_LED;
static bool    leds_discovered = false;
#else
static bool    leds_discovered = false;
#endif

static bool init_done = false;

static inline uint32_t beat_period_ms(void) {
    return 60000UL / (bpm ? bpm : 1);
}

static inline uint32_t step_period_ms(void) {
    return seq_external_clock ? seq_external_step_interval : 15000UL / (bpm ? bpm : 1);
}

static void seq_ratchet_clear(void) {
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) {
        ratchet_events[i].active     = false;
        ratchet_events[i].channel    = 0;
        ratchet_events[i].note       = 0;
        ratchet_events[i].velocity   = 0;
        ratchet_events[i].trigger_ms = 0;
    }
}

static void seq_ratchet_clear_track(uint8_t track) {
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) {
        if (ratchet_events[i].channel == track) ratchet_events[i].active = false;
    }
}

static void seq_ratchet_process(uint32_t now) {
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) {
        seq_ratchet_event_t *ev = &ratchet_events[i];
        if (!ev->active || !timer_expired32(now, ev->trigger_ms)) continue;
        /* Keep only the most recent overdue repeat. A stalled scan must not
         * turn several subdivisions into a burst of simultaneous notes. */
        do {
            uint32_t offset = (uint32_t)ev->index * ev->span_ms / ev->divisions;
            if (++ev->index == ev->divisions) {
                ev->active = false;
                break;
            }
            ev->trigger_ms += (uint32_t)ev->index * ev->span_ms / ev->divisions - offset;
        } while (timer_expired32(now, ev->trigger_ms));
        uint8_t channel = ev->channel;
        uint8_t note = ev->note;
        if (drum_lane_muted[channel][note]) continue;
        uint8_t midi_ch = midi_channel_for_track(channel);
        uint8_t midi_note = drum_note_for_track(note);
        if (drum_note_gate_mask[channel] & (1u << note)) virtual_midi_note_off(midi_ch, midi_note, now);
        virtual_midi_note_on(midi_ch, midi_note, ev->velocity, now);
        drum_note_gate_mask[channel] |= (uint16_t)(1u << note);
    }
}

static void seq_ratchet_schedule_repeats(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t repeats, uint8_t spacing_units, uint32_t now, uint32_t emitted_ms) {
    if (!repeats) return;
    uint32_t span = seq_step_duration_ms * (spacing_units ? spacing_units : 1u) / SEQ_DRUM_RATCHET_SPACING_STEPS;
    if (span < repeats + 1u) span = repeats + 1u;
    /* One entry per allocated lane, regardless of repeat count. All repeats
     * fit inside this step, and the next boundary clears the previous set. */
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) {
        seq_ratchet_event_t *ev = &ratchet_events[i];
        if (ev->active) continue;
        *ev = (seq_ratchet_event_t){.active = true, .channel = channel, .note = note,
            .velocity = velocity ? velocity : 1u, .trigger_ms = now + span / (repeats + 1u),
            .span_ms = span, .index = 1, .divisions = repeats + 1u};
        while (timer_expired32(emitted_ms, ev->trigger_ms)) {
            if (++ev->index == ev->divisions) {
                ev->active = false;
                break;
            }
            ev->trigger_ms = now + (uint32_t)ev->index * span / ev->divisions;
        }
        return;
    }
}

static inline uint16_t clamp_bpm(int32_t candidate) {
    if (candidate < 10) {
        candidate = 10;
    } else if (candidate > 400) {
        candidate = 400;
    }
    return (uint16_t)candidate;
}

static inline uint8_t seq_u8_sat_add(uint8_t value, uint8_t add) {
    uint16_t sum = (uint16_t)value + (uint16_t)add;
    return sum > 255 ? 255U : (uint8_t)sum;
}

#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
static void seq_clear_number_leds(uint8_t led_min, uint8_t led_max) {
    for (uint8_t d = 0; d < 10; d++) {
        uint8_t led = number_leds[d];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }
        rgb_matrix_set_color(led, 0, 0, 0);
    }
}

static bool seq_render_digits_common(uint16_t value, uint8_t led_min, uint8_t led_max, uint32_t now, bool brighten) {
    if (value == 0) {
        value = 1;
    }

    uint8_t digits[4];
    uint8_t count = 0;
    uint16_t temp = value;
    do {
        if (count >= sizeof(digits) / sizeof(digits[0])) {
            break;
        }
        digits[count++] = (uint8_t)(temp % 10u);
        temp /= 10u;
    } while (temp);
    if (count == 0) {
        return false;
    }

    for (uint8_t i = 0; i < count / 2; i++) {
        uint8_t swap    = digits[i];
        digits[i]       = digits[count - 1u - i];
        digits[count - 1u - i] = swap;
    }

    seq_clear_number_leds(led_min, led_max);

    uint8_t digit_mask[10] = {0};
    for (uint8_t i = 0; i < count; i++) {
        uint8_t digit    = digits[i];
        uint8_t position = (uint8_t)(count - 1u - i);
        if (position < 4) {
            digit_mask[digit] |= (uint8_t)(1u << position);
        }
    }

    for (uint8_t digit = 0; digit < 10; digit++) {
        uint8_t led = number_leds[digit];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }

        uint8_t mask = digit_mask[digit];
        if (!mask) {
            continue;
        }

        uint8_t total = 0;
        for (uint8_t pos = 0; pos < 4; pos++) {
            if (mask & (1u << pos)) {
                total++;
            }
        }

        uint8_t chosen_pos = 0;
        if (total > 1) {
            uint8_t cycle = (uint8_t)((now / 250u) % total);
            for (uint8_t pos = 0; pos < 4; pos++) {
                if (mask & (1u << pos)) {
                    if (cycle == 0) {
                        chosen_pos = pos;
                        break;
                    }
                    cycle--;
                }
            }
        } else {
            for (uint8_t pos = 0; pos < 4; pos++) {
                if (mask & (1u << pos)) {
                    chosen_pos = pos;
                    break;
                }
            }
        }

        RGB color;
        if (chosen_pos == 0) {
            color = (RGB){ 255, 90, 70 };
        } else if (chosen_pos == 1) {
            color = (RGB){ 70, 140, 255 };
        } else {
            color = (RGB){ 60, 220, 130 };
        }

        if (brighten) {
            color.r = seq_u8_sat_add(color.r, 40);
            color.g = seq_u8_sat_add(color.g, 40);
            color.b = seq_u8_sat_add(color.b, 40);
        }

        rgb_matrix_set_color(led, color.r, color.g, color.b);
    }

    return true;
}

static inline void seq_bpm_render_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
    (void)seq_render_digits_common(bpm, led_min, led_max, now, false);
}
#else
static inline void seq_clear_number_leds(uint8_t led_min, uint8_t led_max) {
    (void)led_min;
    (void)led_max;
}

static inline bool seq_render_digits_common(uint16_t value, uint8_t led_min, uint8_t led_max, uint32_t now, bool brighten) {
    (void)value;
    (void)led_min;
    (void)led_max;
    (void)now;
    (void)brighten;
    return false;
}

static inline void seq_bpm_render_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
    (void)led_min;
    (void)led_max;
    (void)now;
}
#endif

static inline int8_t step_from_keycode(uint16_t keycode) {
    for (uint8_t i = 0; i < 16; i++) {
        if (seq_step_key(i) == keycode) {
            return (int8_t)i;
        }
    }
    return -1;
}

static void seq_step_edit_reset(void) {
    step_edit = (seq_step_edit_state_t){0};
}

static void seq_step_edit_cancel(void) {
    if (step_edit.active) {
        step_edit = (seq_step_edit_state_t){0};
    }
}

static bool seq_step_edit_refresh_descriptor(void) {
    if (!step_edit.active) {
        return false;
    }
    if (step_edit.is_drum) {
        uint8_t channel = step_edit.track % 12;
        uint16_t track_length = seq_track_length_steps(channel);
        if (!track_length) {
            track_length = SEQ_STEPS_PER_BAR;
        }
        if (!track_length) {
            track_length = 1;
        }
        uint16_t local_step = step_edit.local_step % track_length;
        seq_drum_step_mod_t *mod = seq_drum_step_mod_lookup(channel, step_edit.note % SEQ_DRUM_NOTE_COUNT, local_step);
        step_edit.velocity_mode = mod ? seq_drum_mod_velocity(mod) : 0;
        step_edit.ratchet       = mod ? seq_drum_mod_ratchet(mod) : 0;
        step_edit.spacing       = seq_drum_mod_spacing_raw(mod);
        return true;
    }
    mel_note_descriptor_t desc;
    if (!mel_track_note_descriptor(step_edit.track, step_edit.local_step, &desc)) {
        seq_step_edit_cancel();
        return false;
    }
    step_edit.event_index = desc.event_index;
    step_edit.pitch       = desc.pitch;
    step_edit.velocity    = desc.velocity;
    step_edit.start_step  = desc.start_step;
    step_edit.end_step    = desc.end_step;
    step_edit.spacing     = SEQ_DRUM_RATCHET_SPACING_DEFAULT;
    seq_step_edit_refresh_highlight();
    return true;
}

static void seq_step_edit_select_param(uint8_t param) {
    if (!step_edit.active) {
        return;
    }
    switch (param) {
        case STEP_EDIT_PARAM_LENGTH:
        case STEP_EDIT_PARAM_PITCH:
        case STEP_EDIT_PARAM_VELOCITY:
        case STEP_EDIT_PARAM_RATCHET:
        case STEP_EDIT_PARAM_RATCHET_SPACING:
            step_edit.param = param;
            break;
        default:
            break;
    }
    seq_step_edit_refresh_descriptor();
}

static bool seq_step_edit_handle_digit(uint8_t digit, bool pressed) {
    if (!step_edit.active || !pressed) {
        return false;
    }
    if (step_edit.is_drum) {
        switch (digit) {
            case 1:
                seq_step_edit_select_param(STEP_EDIT_PARAM_VELOCITY);
                return true;
            case 2:
                seq_step_edit_select_param(STEP_EDIT_PARAM_RATCHET);
                return true;
            case 3:
                seq_step_edit_select_param(STEP_EDIT_PARAM_RATCHET_SPACING);
                return true;
            case 0:
                if (seq_step_edit_refresh_descriptor()) {
                    seq_drum_step_mod_t *mod = seq_drum_step_mod_lookup(step_edit.track, step_edit.note, step_edit.local_step);
                    if (mod) {
                        seq_drum_mod_set_velocity(mod, 0);
                        seq_drum_mod_set_ratchet(mod, 0);
                        seq_drum_mod_reset_spacing(mod);
                        seq_drum_step_mod_prune(step_edit.track, step_edit.note, step_edit.local_step);
                        seq_step_edit_refresh_descriptor();
                    }
                }
                return true;
            default:
                return true;
        }
    } else {
        switch (digit) {
            case 1: seq_step_edit_select_param(STEP_EDIT_PARAM_LENGTH); return true;
            case 2: seq_step_edit_select_param(STEP_EDIT_PARAM_PITCH); return true;
            case 3: seq_step_edit_select_param(STEP_EDIT_PARAM_VELOCITY); return true;
            default: return true;
        }
    }
}

static bool seq_step_edit_begin_melodic(uint8_t slot_index, uint16_t absolute_step, uint32_t now) {
    uint8_t melodic_track = slot_melodic_track(slot_index);
    uint16_t track_length = seq_track_length_steps(slot_index);
    if (!track_length) {
        track_length = SEQ_STEPS_PER_BAR;
    }
    if (!track_length) {
        return false;
    }
    uint16_t track_step = absolute_step % track_length;
    mel_note_descriptor_t desc;
    if (!mel_track_note_descriptor(melodic_track, track_step, &desc)) {
        return false;
    }
    step_edit.active        = true;
    step_edit.is_drum       = false;
    step_edit.track         = melodic_track;
    step_edit.absolute_step = absolute_step;
    step_edit.local_step    = track_step;
    step_edit.param         = STEP_EDIT_PARAM_LENGTH;
    step_edit.event_index   = desc.event_index;
    step_edit.pitch         = desc.pitch;
    step_edit.velocity      = desc.velocity;
    step_edit.start_step    = desc.start_step;
    step_edit.end_step      = desc.end_step;
    step_edit.last_input_ms = now;
    seq_step_edit_refresh_descriptor();
    seq_step_edit_refresh_highlight();
    return true;
}

static bool seq_step_edit_try_begin(uint8_t slot_index, uint16_t absolute_step, bool allow_without_shift, uint32_t now) {
    if (!allow_without_shift && !seq_shift_active()) {
        return false;
    }
    if (slot_uses_melodic(slot_index)) {
        if (seq_step_edit_begin_melodic(slot_index, absolute_step, now)) {
            return true;
        }
    } else {
        uint8_t channel = slot_drum_track(slot_index);
        uint16_t track_length = seq_track_length_steps(slot_index);
        if (!track_length) {
            track_length = SEQ_STEPS_PER_BAR;
        }
        if (!track_length) {
            track_length = 1;
        }
        uint16_t local_step = absolute_step % track_length;

        uint16_t targets = note_hold_mask ? note_hold_mask : (uint16_t)(1u << drum_active_note[channel]);
        targets &= (uint16_t)((1u << SEQ_DRUM_NOTE_COUNT) - 1);
        if (!targets) {
            return false;
        }
        uint8_t note = first_note_from_mask(targets, drum_active_note[channel]);
        bool had_base = seq_drum_step_base_hit(channel, note, absolute_step);
        if (!had_base && allow_without_shift && !seq_step_hold.active) {
            if (!seq_drum_step_set(channel, note, absolute_step, true)) {
                return false;
            }
            if (drum_deleted_mod_note[channel] == note && drum_deleted_mod[channel].step == local_step) {
                seq_drum_step_mod_t *restored = seq_drum_step_mod_ensure(channel, note, local_step);
                if (restored) {
                    *restored = drum_deleted_mod[channel];
                }
            }
            drum_deleted_mod_note[channel] = 0xFF;
            had_base = true;
        }
        if (!had_base && !seq_drum_step_get(channel, note, absolute_step)) {
            return false;
        }
        step_edit.active        = true;
        step_edit.is_drum       = true;
        step_edit.track         = channel;
        step_edit.absolute_step = absolute_step;
        step_edit.local_step    = local_step;
        step_edit.note          = note;
        step_edit.param         = STEP_EDIT_PARAM_VELOCITY;
        seq_drum_step_mod_t *mod = seq_drum_step_mod_lookup(channel, note, local_step);
        if (mod) {
            step_edit.velocity_mode = seq_drum_mod_velocity(mod);
        } else {
            step_edit.velocity_mode = 0;
        }
        step_edit.ratchet = mod ? seq_drum_mod_ratchet(mod) : 0;
        step_edit.last_input_ms = now;
        seq_step_edit_refresh_descriptor();
        return true;
    }
    return false;
}

static bool seq_step_edit_encoder_delta(int8_t delta) {
    if (!step_edit.active || delta == 0) {
        return step_edit.active;
    }

    if (step_edit.is_drum) {
        uint8_t channel = step_edit.track % 12;
        uint16_t track_length = seq_track_length_steps(channel);
        if (!track_length) {
            track_length = SEQ_STEPS_PER_BAR;
        }
        if (!track_length) {
            track_length = 1;
        }
        uint16_t local_step = step_edit.local_step % track_length;
        seq_drum_step_mod_t *mod = seq_drum_step_mod_ensure(channel, step_edit.note, local_step);
        if (!mod) {
            return false;
        }
        bool updated = false;
        switch (step_edit.param) {
            case STEP_EDIT_PARAM_VELOCITY: {
                int8_t next = (int8_t)seq_drum_mod_velocity(mod) + delta;
                if (next < 0) {
                    next = 0;
                } else if (next > 2) {
                    next = 2;
                }
                if (next != (int8_t)seq_drum_mod_velocity(mod)) {
                    seq_drum_mod_set_velocity(mod, (uint8_t)next);
                    updated = true;
                }
            } break;
            case STEP_EDIT_PARAM_RATCHET: {
                int8_t next = (int8_t)seq_drum_mod_ratchet(mod) + delta;
                if (next < 0) {
                    next = 0;
                } else if (next > 3) {
                    next = 3;
                }
                if (next != (int8_t)seq_drum_mod_ratchet(mod)) {
                    seq_drum_mod_set_ratchet(mod, (uint8_t)next);
                    updated = true;
                }
            } break;
            case STEP_EDIT_PARAM_RATCHET_SPACING: {
                int16_t next = (int16_t)step_edit.spacing + delta;
                if (next < 0) {
                    next = 0;
                } else if (next >= SEQ_DRUM_RATCHET_SPACING_STEPS) {
                    next = SEQ_DRUM_RATCHET_SPACING_STEPS - 1;
                }
                if (next != (int16_t)step_edit.spacing) {
                    seq_drum_mod_set_spacing(mod, (uint8_t)next);
                    step_edit.spacing = (uint8_t)next;
                    updated = true;
                }
            } break;
            default:
                break;
        }
        seq_drum_step_mod_prune(channel, step_edit.note, local_step);
        if (updated) {
            seq_mark_dirty();
            seq_step_edit_refresh_descriptor();
        }
        step_edit.last_input_ms = timer_read32();
        return true;
    }

    if (!seq_step_edit_refresh_descriptor()) {
        return false;
    }

    bool updated = false;
    switch (step_edit.param) {
        case STEP_EDIT_PARAM_LENGTH:
            updated = mel_track_offset_note_length(step_edit.track, step_edit.event_index, delta);
            break;
        case STEP_EDIT_PARAM_PITCH: {
            int16_t next = (int16_t)step_edit.pitch + delta;
            if (seq_step_hold.editing) {
                while (next >= 0 && next <= 127 && seq_scale_apply(step_edit.track, (uint8_t)next) != next) next += delta;
                if (next < 0 || next > 127) next = step_edit.pitch;
            }
            if (next < 0) {
                next = 0;
            } else if (next > 127) {
                next = 127;
            }
            if (next != step_edit.pitch) {
                updated = mel_track_set_note_pitch(step_edit.track, step_edit.event_index, (uint8_t)next);
            }
        } break;
        case STEP_EDIT_PARAM_VELOCITY: {
            int16_t next = (int16_t)step_edit.velocity + delta;
            if (next < 1) {
                next = 1;
            } else if (next > 127) {
                next = 127;
            }
            if (next != step_edit.velocity) {
                updated = mel_track_set_note_velocity(step_edit.track, step_edit.event_index, (uint8_t)next);
            }
        } break;
        default:
            break;
    }

    if (updated) {
        seq_step_edit_refresh_descriptor();
        seq_step_edit_refresh_highlight();
        step_edit.last_input_ms = timer_read32();
    }
    return true;
}

static int8_t seq_melodic_key_index_from_keycode(uint16_t keycode) {
    for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
        seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
        if (key.keycode == keycode) {
            return (int8_t)i;
        }
    }
    return -1;
}

static bool seq_step_edit_toggle_melodic_key(uint8_t key_index, uint32_t now) {
    if (!step_edit.active || step_edit.is_drum) {
        return false;
    }

    uint8_t track = step_edit.track % MEL_TRACK_COUNT;
    uint16_t track_length = seq_track_length_steps(track);
    if (!track_length) {
        track_length = SEQ_STEPS_PER_BAR;
    }
    if (!track_length) {
        track_length = 1;
    }

    uint16_t local_step = step_edit.local_step % track_length;
    seq_melodic_key_t key = SEQ_MELODIC_KEY(key_index);
    uint8_t pitch         = seq_melodic_note_pitch(track, (uint8_t)key.semitone);

    bool removed = mel_track_remove_note(track, local_step, pitch);
    if (!removed) {
        uint8_t velocity = step_edit.velocity ? step_edit.velocity : MEL_DEFAULT_VELOCITY;
        uint16_t start = step_edit.start_step;
        uint16_t end   = step_edit.end_step;
        uint16_t track_length = seq_track_length_steps(track);
        if (!track_length) {
            track_length = SEQ_STEPS_PER_BAR ? SEQ_STEPS_PER_BAR : 1;
        }
        if (start >= track_length) {
            start %= track_length;
        }
        if (end >= track_length) {
            end %= track_length;
        }
        if (end < start) {
            end = start;
        }
        mel_track_add_note(track, start, end, pitch, velocity);
    }

    seq_mark_dirty();
    seq_step_edit_refresh_descriptor();
    seq_step_edit_refresh_highlight();
    step_edit.last_input_ms = now;
    return true;
}

static bool seq_step_edit_handle_key(uint16_t keycode, bool pressed, uint32_t now) {
    if (!step_edit.active) {
        return false;
    }

    if (!step_edit.is_drum) {
        if (!pressed) {
            if (seq_melodic_key_index_from_keycode(keycode) >= 0) {
                return true;
            }
            switch (keycode) {
                case KC_I:
                case KC_O:
                case KC_P:
                case KC_LBRC:
                case KC_RBRC:
                case KC_SCLN:
                case KC_QUOT:
                case KC_SLASH:
                    return true;
                default:
                    break;
            }
            return false;
        }

        int8_t melodic_index = seq_melodic_key_index_from_keycode(keycode);
        if (melodic_index >= 0) {
            return seq_step_edit_toggle_melodic_key((uint8_t)melodic_index, now);
        }

        switch (keycode) {
            case KC_I:
                seq_step_edit_select_param(STEP_EDIT_PARAM_LENGTH);
                return true;
            case KC_O:
                seq_step_edit_select_param(STEP_EDIT_PARAM_PITCH);
                return true;
            case KC_P:
                seq_step_edit_select_param(STEP_EDIT_PARAM_VELOCITY);
                return true;
            case KC_LBRC:
                seq_accumulator_set_direction(step_edit.track, -1);
                seq_mark_dirty();
                return true;
            case KC_RBRC:
                seq_accumulator_set_direction(step_edit.track, 1);
                seq_mark_dirty();
                return true;
            case KC_SCLN:
                seq_accumulator_cycle_run_length(step_edit.track);
                seq_mark_dirty();
                return true;
            case KC_QUOT:
                seq_accumulator_toggle_random(step_edit.track);
                seq_mark_dirty();
                return true;
            case KC_SLASH:
                seq_accumulator_toggle(step_edit.track);
                seq_mark_dirty();
                return true;
            default:
                break;
        }
        return false;
    }

    if (!pressed) {
        switch (keycode) {
            case KC_I:
            case KC_O:
            case KC_P:
            case KC_LBRC:
            case KC_RBRC:
            case KC_SCLN:
            case KC_QUOT:
            case KC_SLASH:
                return true;
            default:
                break;
        }
        return false;
    }

    switch (keycode) {
        case KC_I:
            seq_step_edit_select_param(STEP_EDIT_PARAM_VELOCITY);
            return true;
        case KC_O:
            seq_step_edit_select_param(STEP_EDIT_PARAM_RATCHET);
            return true;
        case KC_P:
            seq_step_edit_select_param(STEP_EDIT_PARAM_RATCHET_SPACING);
            return true;
        default:
            break;
    }

    return false;
}

static inline int8_t track_from_keycode(uint16_t keycode) {
    if (keycode >= KC_F1 && keycode <= KC_F12) {
        return (int8_t)(keycode - KC_F1);
    }
    switch (keycode) {
        case KC_BRID: return 0;
        case KC_BRIU: return 1;
        case KC_MCTL:
#ifdef KC_MCTRL
        case KC_MCTRL:
#endif
            return 2;
        case KC_LPAD:
#ifdef KC_LNPAD
        case KC_LNPAD:
#endif
            return 3;
        case RGB_VAD: return 4;
        case RGB_VAI: return 5;
        case KC_MPRV: return 6;
        case KC_MPLY: return 7;
        case KC_MNXT: return 8;
        case KC_MUTE: return 9;
        case KC_VOLD: return 10;
        case KC_VOLU: return 11;
        default: return -1;
    }
}

static inline uint8_t first_track_from_mask(uint16_t mask, uint8_t fallback) {
    for (uint8_t i = 0; i < 12; i++) {
        if (mask & (1u << i)) {
            return i;
        }
    }
    return fallback % 12;
}

static inline uint8_t seq_first_semitone_from_mask(uint16_t mask, uint8_t fallback) {
#if defined(__GNUC__)
    if (mask) {
        return (uint8_t)__builtin_ctz(mask);
    }
#else
    for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
        if (mask & (1u << i)) {
            return i;
        }
    }
#endif
    return fallback % SEQ_MELODIC_KEY_COUNT;
}

static inline uint8_t shift_bit_for(uint16_t keycode) {
    switch (keycode) {
        case KC_LSFT:
#ifdef KC_LSHIFT
        case KC_LSHIFT:
#endif
            return 1u << 0;
        case KC_RSFT:
#ifdef KC_RSHIFT
        case KC_RSHIFT:
#endif
            return 1u << 1;
        default:
            return 0;
    }
}

static inline bool seq_shift_active(void) {
    return seq_shift_mask != 0;
}

#define SEQ_MOD_GRAVE 0x01
#define SEQ_MOD_TAB   0x02
#define SEQ_MOD_DEL   0x04
#define SEQ_MOD_CAPS  0x08

static inline bool seq_mod_grave(void) {
    return (seq_modifiers & SEQ_MOD_GRAVE) != 0;
}

static inline bool seq_mod_tab(void) {
    return (seq_modifiers & SEQ_MOD_TAB) != 0;
}

static inline bool seq_mod_del(void) {
    return (seq_modifiers & SEQ_MOD_DEL) != 0;
}

static inline bool seq_mod_caps(void) {
    return (seq_modifiers & SEQ_MOD_CAPS) != 0;
}

static uint8_t seq_pitch_to_semitone(uint8_t pitch) {
    int16_t relative = (int16_t)pitch - (int16_t)MEL_DEFAULT_BASE_NOTE;
    while (relative < 0) {
        relative += 12;
    }
    return (uint8_t)(relative % SEQ_MELODIC_KEY_COUNT);
}

static uint16_t seq_melodic_step_event_mask(uint8_t track, uint16_t step_local) {
    uint16_t             mask = 0;
    mel_event_snapshot_t ev;
    for (uint8_t i = 0; i < mel_track_event_count(track); i++) {
        mel_track_event_at(track, i, &ev);
        if (ev.start_step <= step_local && ev.end_step >= step_local) mask |= 1u << seq_pitch_to_semitone(ev.pitch);
    }
    return mask;
}

static void seq_step_edit_refresh_highlight(void) {
    if (!step_edit.active || step_edit.is_drum) {
        return;
    }
    uint8_t  track  = step_edit.track % MEL_TRACK_COUNT;
    uint16_t length = seq_track_length_steps(track);
    if (!length) {
        length = SEQ_STEPS_PER_BAR ? SEQ_STEPS_PER_BAR : 1;
    }
    uint16_t local = step_edit.local_step % length;
    uint16_t mask  = seq_melodic_step_event_mask(track, local);
    seq_melodic_set_last_mask(track, mask);
}

#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
static inline RGB track_rgb(uint8_t track, uint8_t value) {
    uint8_t hue = seq_progmem_u8(&TRACK_HUES[track % 12]);
    HSV hsv = { hue, 255, value };
    return hsv_to_rgb(hsv);
}

static uint8_t triangle_wave(uint32_t now, uint32_t period_ms, uint8_t min, uint8_t max) {
    if (period_ms == 0 || max <= min) {
        return max;
    }
    uint32_t period = period_ms;
    uint32_t half   = period / 2;
    if (half == 0) {
        return max;
    }
    uint32_t t    = now % period;
    uint32_t span = (uint32_t)(max - min);
    uint32_t value = (t < half) ? (min + (t * span) / half)
                                : (min + ((period - t) * span) / half);
    if (value > max) {
        value = max;
    }
    return (uint8_t)value;
}

static uint16_t keycode_for_digit(uint8_t digit) {
    switch (digit) {
        case 0: return KC_0;
        case 1: return KC_1;
        case 2: return KC_2;
        case 3: return KC_3;
        case 4: return KC_4;
        case 5: return KC_5;
        case 6: return KC_6;
        case 7: return KC_7;
        case 8: return KC_8;
        case 9: return KC_9;
        default: return KC_NO;
    }
}

static uint8_t led_index_for_keycode(uint16_t keycode) {
    /* Modifier positions swap by OS; track/media aliases use the reference layout. */
    uint8_t layer = (seq_is_option_key(keycode) || seq_is_cmd_key(keycode)) ? get_highest_layer(default_layer_state) : 0;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (pgm_read_word(&keymaps[layer][r][c]) == keycode) {
                uint8_t idx = g_led_config.matrix_co[r][c];
                if (idx != NO_LED) {
                    return idx;
                }
            }
        }
    }
    return NO_LED;
}

static void discover_seq_leds(void) {
    static uint8_t discovered_layer = 255;
    uint8_t layer = get_highest_layer(default_layer_state);
    if (discovered_layer != layer) {
        discovered_layer = layer;
        leds_discovered = false;
    }
    if (leds_discovered) {
        return;
    }
    for (uint8_t i = 0; i < 16; i++) {
        step_leds[i] = led_index_for_keycode(seq_step_key(i));
    }
    for (uint8_t i = 0; i < 12; i++) {
        track_leds[i] = led_index_for_keycode(seq_progmem_u16(&TRACK_KEYS[i]));
    }
    for (uint8_t i = 0; i < SEQ_BAR_LED_COUNT; i++) {
        bar_leds[i] = led_index_for_keycode(seq_progmem_u16(&SEQ_BAR_LED_KEYS[i]));
    }
    for (uint8_t d = 0; d < 10; d++) {
        number_leds[d] = led_index_for_keycode(keycode_for_digit(d));
    }
    for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
        seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
        melodic_key_leds[i]    = led_index_for_keycode(key.keycode);
    }
    for (uint8_t i = 0; i < SEQ_MELODIC_CONTROL_COUNT; i++) {
        melodic_control_leds[i] = led_index_for_keycode(seq_progmem_u16(&SEQ_MELODIC_CONTROL_KEYS[i]));
    }
    home_led        = led_index_for_keycode(KC_HOME);
    grave_led       = led_index_for_keycode(KC_GRV);
    backslash_led   = led_index_for_keycode(KC_BSLS);
    esc_led         = led_index_for_keycode(KC_ESC);
    equal_led       = led_index_for_keycode(KC_EQUAL);
    confirm_led_yes = led_index_for_keycode(KC_Y);
    confirm_led_no  = led_index_for_keycode(KC_N);
    option_leds[0]  = led_index_for_keycode(KC_LALT);
    if (option_leds[0] == NO_LED) {
        option_leds[0] = led_index_for_keycode(KC_LOPT);
    }
#ifdef KC_LOPTN
    if (option_leds[0] == NO_LED) {
        option_leds[0] = led_index_for_keycode(KC_LOPTN);
    }
#endif
    option_leds[1] = led_index_for_keycode(KC_RALT);
    if (option_leds[1] == NO_LED) {
        option_leds[1] = led_index_for_keycode(KC_ROPT);
    }
#ifdef KC_ROPTN
    if (option_leds[1] == NO_LED) {
        option_leds[1] = led_index_for_keycode(KC_ROPTN);
    }
#endif
    cmd_leds[0] = NO_LED;
#ifdef KC_LCMMD
    cmd_leds[0] = led_index_for_keycode(KC_LCMMD);
#endif
#ifdef KC_LCMD
    if (cmd_leds[0] == NO_LED) {
        cmd_leds[0] = led_index_for_keycode(KC_LCMD);
    }
#endif
    if (cmd_leds[0] == NO_LED) {
        cmd_leds[0] = led_index_for_keycode(KC_LGUI);
    }
    cmd_leds[1] = NO_LED;
#ifdef KC_RCMMD
    cmd_leds[1] = led_index_for_keycode(KC_RCMMD);
#endif
#ifdef KC_RCMD
    if (cmd_leds[1] == NO_LED) {
        cmd_leds[1] = led_index_for_keycode(KC_RCMD);
    }
#endif
    if (cmd_leds[1] == NO_LED) {
        cmd_leds[1] = led_index_for_keycode(KC_RGUI);
    }
    record_led = led_index_for_keycode(KC_PGDN);
    leds_discovered = true;
}

static bool step_edit_meter_visible = false;

static inline RGB rgb_from_components(uint8_t r, uint8_t g, uint8_t b) {
    return (RGB){ r, g, b };
}

static RGB seq_step_edit_param_color(bool is_drum, uint8_t param) {
    (void)is_drum;
    (void)param;
    return rgb_from_components(255, 40, 60);
}

static uint8_t seq_step_edit_collect_digits(uint16_t value, uint8_t *out, uint8_t capacity) {
    if (!capacity) {
        return 0;
    }
    uint8_t count = 0;
    do {
        if (count >= capacity) {
            break;
        }
        out[count++] = (uint8_t)(value % 10u);
        value /= 10u;
    } while (value);

    for (uint8_t i = 0; i < count / 2; i++) {
        uint8_t tmp          = out[i];
        out[i]               = out[count - 1u - i];
        out[count - 1u - i] = tmp;
    }
    return count;
}

static const uint8_t STEP_EDIT_METER_ORDER[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };

static void seq_rgb_set_key(uint16_t keycode, RGB color, uint8_t led_min, uint8_t led_max) {
    uint8_t led = led_index_for_keycode(keycode);
    if (led == NO_LED || led < led_min || led >= led_max) {
        return;
    }
    rgb_matrix_set_color(led, color.r, color.g, color.b);
}

static void seq_number_led_set_color(uint8_t digit, RGB color, uint8_t led_min, uint8_t led_max) {
    if (digit >= 10) {
        return;
    }
    uint8_t led = number_leds[digit];
    if (led == NO_LED || led < led_min || led >= led_max) {
        return;
    }
    rgb_matrix_set_color(led, color.r, color.g, color.b);
}

static void seq_step_edit_fill_meter(uint8_t lit, RGB accent, RGB dim, uint8_t led_min, uint8_t led_max) {
    if (lit > 10) {
        lit = 10;
    }
    for (uint8_t i = 0; i < 10; i++) {
        RGB color = (i < lit) ? accent : dim;
        seq_number_led_set_color(STEP_EDIT_METER_ORDER[i], color, led_min, led_max);
    }
}

static bool seq_step_edit_render_meter(uint8_t led_min, uint8_t led_max) {
    if (!step_edit.active) {
        if (step_edit_meter_visible) {
            seq_clear_number_leds(led_min, led_max);
            step_edit_meter_visible = false;
        }
        return false;
    }

    step_edit_meter_visible = true;
    seq_clear_number_leds(led_min, led_max);

    RGB accent = seq_step_edit_param_color(step_edit.is_drum, step_edit.param);
    RGB dim    = rgb_from_components(12, 18, 24);

    if (step_edit.is_drum) {
        switch (step_edit.param) {
            case STEP_EDIT_PARAM_VELOCITY: {
                uint8_t lit = (uint8_t)(((uint32_t)(step_edit.velocity_mode + 1u) * 10u) / 3u);
                if (!lit) {
                    lit = 1u;
                }
                seq_step_edit_fill_meter(lit, accent, dim, led_min, led_max);
            } break;
            case STEP_EDIT_PARAM_RATCHET: {
                uint8_t lit = (uint8_t)(((uint32_t)(step_edit.ratchet + 1u) * 10u) / 4u);
                if (!lit) {
                    lit = 1u;
                }
                seq_step_edit_fill_meter(lit, accent, dim, led_min, led_max);
            } break;
            case STEP_EDIT_PARAM_RATCHET_SPACING: {
                uint8_t max_units = SEQ_DRUM_RATCHET_SPACING_STEPS;
                uint8_t lit       = (uint8_t)(((uint32_t)(step_edit.spacing + 1u) * 10u) / (max_units ? max_units : 1u));
                if (step_edit.spacing == 0 && lit == 0) {
                    lit = 1;
                }
                seq_step_edit_fill_meter(lit, accent, dim, led_min, led_max);
            } break;
            default:
                for (uint8_t digit = 0; digit < 10; digit++) {
                    seq_number_led_set_color(digit, dim, led_min, led_max);
                }
                break;
        }
    } else {
        uint8_t digits[5] = {0};
        uint8_t count     = 0;
        switch (step_edit.param) {
            case STEP_EDIT_PARAM_LENGTH: {
                uint16_t length = (step_edit.end_step >= step_edit.start_step)
                    ? (uint16_t)(step_edit.end_step - step_edit.start_step + 1u)
                    : 1u;
                count = seq_step_edit_collect_digits(length, digits, sizeof(digits));
            } break;
            case STEP_EDIT_PARAM_PITCH:
                count = seq_step_edit_collect_digits(step_edit.pitch, digits, sizeof(digits));
                break;
            case STEP_EDIT_PARAM_VELOCITY:
                count = seq_step_edit_collect_digits(step_edit.velocity, digits, sizeof(digits));
                break;
            default:
                break;
        }

        for (uint8_t digit = 0; digit < 10; digit++) {
            seq_number_led_set_color(digit, dim, led_min, led_max);
        }
        for (uint8_t i = 0; i < count; i++) {
            seq_number_led_set_color(digits[i], accent, led_min, led_max);
        }
    }
    return true;
}

static RGB seq_euclid_tint(uint8_t track, RGB base, bool focus) {
    if (seq_modules[track % 12].kind != SEQ_MODULE_EUCLID) return base;
    seq_euclid_track_t *state = seq_euclid_selected(track, false);
    if (!state->enabled || state->fills == 0) {
        return base;
    }
    if (focus) {
        return (RGB){
            seq_u8_sat_add(base.r, 100),
            seq_u8_sat_add(base.g, 160),
            seq_u8_sat_add(base.b, 200)
        };
    }
    return (RGB){
        seq_u8_sat_add(base.r, 30),
        seq_u8_sat_add(base.g, 90),
        seq_u8_sat_add(base.b, 90)
    };
}

static void seq_set_track_color(uint8_t track, uint8_t led, RGB base, bool focus) {
    RGB tinted = seq_euclid_tint(track, base, focus);
    rgb_matrix_set_color(led, tinted.r, tinted.g, tinted.b);
}
#else
static inline RGB track_rgb(uint8_t track, uint8_t value) {
    (void)track;
    (void)value;
    return (RGB){0, 0, 0};
}

static void discover_seq_leds(void) {}
#endif

static void seq_apply_layout(seq_layout_mode_t mode) {
    current_layout = mode;
    switch (mode) {
        case SEQ_LAYOUT_DEFAULT:
        default:
            active_step_keys = SEQ_STEP_KEYS_DEFAULT;
            break;
        case SEQ_LAYOUT_GRID_4X4:
            active_step_keys = SEQ_STEP_KEYS_GRID_4X4;
            break;
    }

    note_hold_mask = 0;
    for (uint8_t channel = 0; channel < 12; channel++) {
        drum_active_note[channel] = clamp_note_index(drum_active_note[channel]);
    }

    memset(melodic_key_mask, 0, sizeof(melodic_key_mask));

#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
    leds_discovered = false;
#endif
    discover_seq_leds();
}

#define SEQ_OCTAVE_HOLD_THRESHOLD_MS 180

static seq_octave_hold_state_t *seq_octave_state(bool negative) {
    return negative ? &octave_hold_negative : &octave_hold_positive;
}

static void seq_octave_apply_delta(seq_octave_hold_state_t *state, uint8_t track, int8_t delta) {
    if (track >= MEL_TRACK_COUNT) {
        return;
    }
    melodic_hold_offset[track] = delta;
}

static void seq_octave_hold_press(bool negative, uint32_t now) {
    seq_octave_hold_state_t *state = seq_octave_state(negative);
    if (now - state->last_tap_ms > SEQ_DOUBLE_TAP_MS) {
        state->pending_taps = 0;
    }
    state->pressed      = true;
    state->press_time   = now;
    state->hold_active  = false;
    state->negative     = negative;
    state->applied_delta = 0;
    state->target_track  = 0xFF;
}

static void seq_octave_hold_release(bool negative, uint32_t now) {
    seq_octave_hold_state_t *state = seq_octave_state(negative);
    if (!state->pressed) {
        return;
    }

    state->pressed = false;
    if (state->hold_active) {
        if (state->applied_delta != 0 && state->target_track < MEL_TRACK_COUNT) {
            if (melodic_hold_offset[state->target_track] == state->applied_delta) {
                melodic_hold_offset[state->target_track] = 0;
            }
        }
        state->hold_active   = false;
        state->applied_delta = 0;
        state->pending_taps  = 0;
    } else {
        if (now - state->last_tap_ms <= SEQ_DOUBLE_TAP_MS) {
            if (state->pending_taps < 15) {
                state->pending_taps++;
            }
        } else {
            state->pending_taps = 1;
        }
        state->last_tap_ms = now;
    }
}

static void seq_octave_hold_tick(uint32_t now) {
    seq_octave_hold_state_t *states[2] = { &octave_hold_positive, &octave_hold_negative };
    for (uint8_t i = 0; i < 2; i++) {
        seq_octave_hold_state_t *state = states[i];
        if (state->pressed && !state->hold_active) {
            if (TIMER_DIFF_32(now, state->press_time) >= SEQ_OCTAVE_HOLD_THRESHOLD_MS) {
                if (!slot_uses_melodic(view_slot)) {
                    continue;
                }
                uint8_t melodic_track = slot_melodic_track(view_slot);
                int8_t  magnitude     = (int8_t)(state->pending_taps + 1);
                if (magnitude > 5) {
                    magnitude = 5;
                }
                int8_t delta = state->negative ? (int8_t)(-magnitude) : magnitude;
                state->hold_active   = true;
                state->target_track  = melodic_track;
                state->applied_delta = delta;
                seq_octave_apply_delta(state, melodic_track, delta);
            }
        }
    }
}

static uint8_t seq_melodic_clamp_pitch(int16_t pitch) {
    if (pitch < 0) {
        return 0;
    }
    if (pitch > 127) {
        return 127;
    }
    return (uint8_t)pitch;
}

static uint8_t seq_melodic_note_pitch(uint8_t track, uint8_t semitone) {
    int16_t pitch = (int16_t)MEL_DEFAULT_BASE_NOTE
                  + ((int16_t)melodic_octave_shift[track] + (int16_t)melodic_hold_offset[track]) * 12
                  + semitone;
    return seq_melodic_clamp_pitch(pitch);
}

static inline uint8_t seq_velocity_apply_swing(uint8_t velocity, uint8_t swing, uint16_t hit_index) {
    if (!swing) {
        return velocity;
    }
    int16_t delta  = (hit_index & 1u) ? -(int16_t)swing : (int16_t)swing;
    int16_t result = (int16_t)velocity + delta;
    if (result < 1) {
        result = 1;
    } else if (result > 127) {
        result = 127;
    }
    return (uint8_t)result;
}

static bool seq_euclid_print_drum_loop(uint8_t track, uint32_t now) {
    uint8_t note = drum_active_note[track];
    if (!seq_drum_euclid_state(track, note)->enabled) return false;
    uint8_t printed[SEQ_DRUM_STORAGE_BYTES] = {0};
    for (uint16_t step = 0; step < seq_track_length_steps(track); step++) {
        if (seq_drum_step_get(track, note, step)) printed[step >> 3] |= 1u << (step & 7);
    }
    /* Reserve only after evaluating inherited routing; never alter sibling lanes. */
    seq_euclid_track_t *state = seq_euclid_selected(track, true);
    if (!state) return false;
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    memcpy(lane->data, printed, sizeof(printed));
    state->enabled = false;
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        uint16_t step = lane->mods[i].step;
        if (step != UINT16_MAX && (step >= seq_track_length_steps(track) || !(printed[step >> 3] & (1u << (step & 7))))) {
            lane->mods[i] = (seq_drum_step_mod_t){.step = UINT16_MAX, .spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT};
        }
    }
    if (drum_note_gate_mask[track] & (1u << note)) {
        virtual_midi_note_off(midi_channel_for_track(track), drum_note_for_track(note), now);
        drum_note_gate_mask[track] &= ~(1u << note);
    }
    for (uint16_t i = 0; i < SEQ_RATCHET_EVENT_CAP; i++) {
        if (ratchet_events[i].channel == track && ratchet_events[i].note == note) ratchet_events[i].active = false;
    }
    seq_mark_dirty();
    return true;
}

static bool seq_euclid_print_loop(uint8_t track, uint32_t now) {
    if (track >= 12 || !seq_euclid_editing_enabled() || seq_chord_mode[track] || seq_transfer_active || record_state.armed ||
        step_edit.active || chord_capture.active || euclid_palette_capture.active || melodic_key_mask[track] ||
        cc_context.active || cc_mode.held || seq_scale_editing) return false;
    if (!slot_uses_melodic(track)) return seq_euclid_print_drum_loop(track, now);
    if (!euclid_tracks[track].enabled || seq_variations[track].enabled || (seq_variation_pending & (1u << track))) return false;

    mel_event_snapshot_t printed[MEL_TRACK_MAX_EVENTS];
    uint16_t count = 0;
    uint8_t source_count = mel_track_event_count(track);
    uint16_t cycle_length = euclid_tracks[track].steps ? euclid_tracks[track].steps : seq_track_length_steps(track);
    for (uint16_t step = 0; step < seq_track_length_steps(track); step++) {
        uint16_t hit;
        int8_t offset;
        uint16_t source_step = step % cycle_length;
        if (!seq_euclid_step_hit(track, source_step, &hit, &offset)) continue;
        uint8_t seen[16] = {0};
        bool stored = false;
        for (uint8_t i = 0; i < source_count; i++) {
            mel_event_snapshot_t event;
            if (!mel_track_event_at(track, i, &event) || event.start_step > source_step || event.end_step < source_step) continue;
            stored = true;
            uint8_t pitch = seq_scale_apply(track, seq_melodic_euclid_pitch(track, event.pitch, offset, true));
            if (seen[pitch >> 3] & (1u << (pitch & 7))) continue;
            if (count == MEL_TRACK_MAX_EVENTS) goto full;
            seen[pitch >> 3] |= 1u << (pitch & 7);
            printed[count++] = (mel_event_snapshot_t){.pitch = pitch,
                .velocity = seq_velocity_apply_swing(event.velocity, euclid_tracks[track].swing_amount, hit),
                .start_step = step, .end_step = step};
        }
        if (!stored) {
            uint8_t pitches[SEQ_MELODIC_KEY_COUNT];
            uint8_t voices = seq_melodic_euclid_fallback(track, source_step, pitches);
            for (uint8_t i = 0; i < voices; i++) {
                uint8_t pitch = euclid_tracks[track].pitch_mode == 2 ? pitches[i] : seq_scale_apply(track, seq_melodic_euclid_pitch(track, pitches[i], offset, false));
                if (seen[pitch >> 3] & (1u << (pitch & 7))) continue;
                if (count == MEL_TRACK_MAX_EVENTS) goto full;
                seen[pitch >> 3] |= 1u << (pitch & 7);
                printed[count++] = (mel_event_snapshot_t){.pitch = pitch,
                    .velocity = seq_velocity_apply_swing(MEL_DEFAULT_VELOCITY, euclid_tracks[track].swing_amount, hit),
                    .start_step = step, .end_step = step};
            }
        }
    }
    /* Restore validates the complete replacement before touching stored events. */
    if (!mel_track_restore(track, printed, (uint8_t)count)) return false;
    seq_midi_stop_melodic_track(track, now);
    seq_euclid_enable(track, false);
    /* Printing bakes the resolved pitches. Keep fixed chord qualities from being
     * quantized again by ordinary melodic playback after generation stops. */
    if (euclid_tracks[track].pitch_mode == 2) {
        seq_scale_tracks[track].mode = 2;
        seq_scale_revision++;
    }
    seq_accumulators[track].enabled = false;
    seq_accumulator_reset_runtime(track);
    if (seq_pending_erase && seq_pending_erase_track == track) seq_pending_erase = false;
    seq_chord_tap_reset();
    seq_mark_dirty();
    return true;
full:
    seq_melodic_capacity_exceeded();
    return false;
}

static bool seq_handle_melodic_keyboard(uint16_t keycode, bool pressed, uint32_t now) {
    (void)now;
    if (!seq_state.active) {
        return false;
    }
    if (!slot_uses_melodic(view_slot)) {
        return false;
    }

    uint8_t melodic_track     = slot_melodic_track(view_slot);
    bool    handled           = false;
    bool    handled_note_key  = false;
    int8_t  handled_semitone  = 0;

    for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
        seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
        if (key.keycode == keycode) {
            uint16_t bit = (uint16_t)(1u << (uint8_t)key.semitone);
            if (pressed) {
                melodic_key_mask[melodic_track] |= bit;
                melodic_last_mask[melodic_track] = melodic_key_mask[melodic_track];
                melodic_last_semitone[melodic_track] = (uint8_t)key.semitone;
                melodic_rest_pending[melodic_track] = false;
                melodic_tie_pending[melodic_track]  = false;
            } else {
                if (melodic_key_mask[melodic_track] & bit) {
                    melodic_key_mask[melodic_track] &= (uint16_t)~bit;
                    if (melodic_key_mask[melodic_track]) {
                        melodic_last_mask[melodic_track] = melodic_key_mask[melodic_track];
#if defined(__GNUC__)
                        melodic_last_semitone[melodic_track] = (uint8_t)__builtin_ctz(melodic_key_mask[melodic_track]);
#else
                        for (uint8_t s = 0; s < SEQ_MELODIC_KEY_COUNT; s++) {
                            if (melodic_key_mask[melodic_track] & (1u << s)) {
                                melodic_last_semitone[melodic_track] = s;
                                break;
                            }
                        }
#endif
                    }
                }
            }
            handled = true;
            handled_note_key = true;
            handled_semitone = key.semitone;
            break;
        }
    }

    if (!handled) {
        uint16_t key_oct_down = seq_progmem_u16(&SEQ_MELODIC_CONTROL_KEYS[0]);
        uint16_t key_rest_tie = seq_progmem_u16(&SEQ_MELODIC_CONTROL_KEYS[1]);
        uint16_t key_oct_up   = seq_progmem_u16(&SEQ_MELODIC_CONTROL_KEYS[2]);

        if (keycode == key_oct_down) {
            if (pressed && melodic_octave_shift[melodic_track] > -5) {
                melodic_octave_shift[melodic_track]--;
                seq_mark_dirty();
            }
            handled = true;
        } else if (keycode == key_oct_up) {
            if (pressed && melodic_octave_shift[melodic_track] < 5) {
                melodic_octave_shift[melodic_track]++;
                seq_mark_dirty();
            }
            handled = true;
        } else if (keycode == key_rest_tie) {
            if (pressed) {
                if (seq_shift_active()) {
                    melodic_tie_pending[melodic_track]  = true;
                    melodic_rest_pending[melodic_track] = false;
                } else {
                    melodic_rest_pending[melodic_track] = true;
                    melodic_tie_pending[melodic_track]  = false;
                }
            }
            handled = true;
        }
    }

    if (handled && seq_euclid_editing_enabled()) {
        uint8_t slot_index = view_slot % 12;
        if (seq_euclid_active_track() == slot_index) {
            if (cmd_state.held && handled_note_key && pressed) {
                seq_euclid_palette_capture_record(slot_index, handled_semitone);
            } else if (handled_note_key) {
                seq_euclid_learn_melodic_keys(slot_index, melodic_track);
            }
        }
    }

    if (handled_note_key) {
        seq_record_update_mask(view_slot, true, melodic_key_mask[melodic_track]);
    }

    return handled;
}

#include "seq_step_hold.inc"

static void seq_enter(uint32_t now) {
    if (seq_state.active) {
        return;
    }
    SEQ_LOG("[seq] enter now=%lu view=%u\n", (unsigned long)now, view_slot);
    seq_running     = false;
    seq_state.active  = true;
    seq_state.running = false;
    step_idx        = 0;
    step_started_ms = now;
    seq_toggle_reset(&seq_pgup_toggle);
    seq_toggle_reset(&seq_pgdn_toggle);
    seq_toggle_reset(&seq_esc_toggle);
    seq_record_try_disarm(seq_step_position);
    seq_record_reset();
    seq_shift_mask  = 0;
    seq_modifiers    = 0;
    note_hold_mask = 0;
    seq_euclid_palette_capture_reset();
    track_confirm.active = false;
    seq_chord_capture_clear();
    seq_chord_tap_reset();
    memset(step_last_tap_info, 0, sizeof(step_last_tap_info));
    for (uint8_t i = 0; i < 12; i++) {
        seq_accumulator_reset_runtime(i);
        seq_accumulator_clear_active(i);
    }
    bpm_preview_active  = false;
    bpm_preview_expires = 0;
    seq_step_position   = 0;
    playhead_bar        = 0;
    step_idx            = 0;
    memset(seq_track_step_position, 0, sizeof(seq_track_step_position));
    seq_midi_all_notes_off(now);
    virtual_midi_stop(now);
    seq_ratchet_clear();
}

static void seq_exit(void) {
    seq_palette_cancel();
    seq_loop_held = false;
    seq_loop_set(0, timer_read32());
    if (seq_temp_active) seq_temp_restore(timer_read32());
    seq_chord_cancel();
    seq_step_hold_finish(false);
    seq_flush_pending_erase();
    if (!seq_state.active) {
        return;
    }
    SEQ_LOG("[seq] exit now=%lu\n", (unsigned long)timer_read32());
    seq_running     = false;
    seq_state.active  = false;
    seq_state.running = false;
    seq_toggle_reset(&seq_pgup_toggle);
    seq_toggle_reset(&seq_pgdn_toggle);
    seq_toggle_reset(&seq_esc_toggle);
    seq_record_on_transport_stop(seq_step_position);
    seq_record_reset();
    seq_shift_mask  = 0;
    seq_modifiers    = 0;
    note_hold_mask = 0;
    seq_euclid_palette_capture_reset();
    track_confirm.active = false;
    seq_chord_capture_clear();
    seq_chord_tap_reset();
    memset(step_last_tap_info, 0, sizeof(step_last_tap_info));
    for (uint8_t i = 0; i < 12; i++) {
        seq_accumulator_reset_runtime(i);
    }
    bpm_preview_active  = false;
    bpm_preview_expires = 0;
    mel_last_start_mask = 0;
    mel_last_stop_mask  = 0;
    memset(seq_track_step_position, 0, sizeof(seq_track_step_position));
    uint32_t now        = timer_read32();
    seq_midi_all_notes_off(now);
    virtual_midi_stop(now);
    seq_ratchet_clear();
    seq_cc_exit_mode(now);
    switch_state.held      = false;
    switch_state.stage     = SEQ_SWITCH_STAGE_INACTIVE;
    switch_state.consumed  = false;
    switch_state.synthetic = false;
    memset(&octave_hold_positive, 0, sizeof(octave_hold_positive));
   memset(&octave_hold_negative, 0, sizeof(octave_hold_negative));
   memset(melodic_hold_offset, 0, sizeof(melodic_hold_offset));
    memset(melodic_key_mask, 0, sizeof(melodic_key_mask));
    memset(melodic_last_mask, 0, sizeof(melodic_last_mask));
    memset(melodic_tie_pending, 0, sizeof(melodic_tie_pending));
    seq_mute_pending_reset();
    cc_session = (seq_cc_session_t){0};
    cc_mode    = (seq_cc_mode_t){0};
    cc_context = (seq_cc_context_t){0};
    cmd_state   = (seq_cmd_state_t){0};
    euclid_edit = (seq_euclid_edit_state_t){0};
    euclid_mode_active   = false;
    euclid_param_mode    = EUCLID_PARAM_MODE_NONE;
    euclid_param_keycode = KC_NO;
    seq_euclid_clear_preview();
    seq_step_edit_reset();
    seq_storage_save_if_dirty();
}

bool seq_is_active(void) {
    return seq_state.active;
}

void seq_housekeeping(void) {
    if (seq_pending_erase && timer_expired32(timer_read32(), seq_pending_erase_deadline)) seq_flush_pending_erase();
    if (seq_transfer_active && timer_expired32(timer_read32(), seq_transfer_deadline)) {
        seq_transfer_active      = false;
        seq_transfer_length      = 0;
        seq_storage_save_pending = false;
    }
    if (!init_done) {
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
        rgb_matrix_enable_noeeprom();
#endif
        seq_mode_on_load();
        discover_seq_leds();
        step_started_ms = timer_read32();
        init_done       = true;
        seq_ratchet_clear();
        for (uint8_t i = 0; i < 12; i++) {
            seq_accumulator_reset_runtime(i);
            seq_accumulator_clear_active(i);
        }
    }
    virtual_midi_task(timer_read32());
    if (seq_storage_save_pending && !seq_state.running) {
        seq_storage_save_pending = false;
        seq_storage_save_if_dirty();
    }
}

static void seq_melodic_release_base(uint8_t track, uint8_t base, uint32_t now) {
    uint8_t output;
    if (!seq_accumulator_lookup_output(track, base, &output)) return;
    seq_accumulator_release_output(track, base, output);
    for (uint8_t other = 0; other < 128; other++) {
        if (seq_accumulator_active_notes[track][other] == output) return;
    }
    if (melodic_active_remove(track, output) && !seq_module_preview_owns(track, output)) virtual_midi_note_off(midi_channel_for_track(track), output, now);
}

static void seq_melodic_play_note(uint8_t track, uint8_t base, uint8_t velocity, uint16_t step, uint32_t now, bool stored) {
    if (melodic_track_muted[track]) return;
    uint8_t  note   = base;
    uint16_t hit    = 0;
    int8_t   offset = 0;
    if (seq_modules[track].kind == SEQ_MODULE_EUCLID && euclid_tracks[track].enabled) {
        if (!seq_euclid_step_hit(track, step, &hit, &offset)) return;
        note     = seq_melodic_euclid_pitch(track, base, offset, stored);
        velocity = seq_velocity_apply_swing(velocity, euclid_tracks[track].swing_amount, hit);
    }
    uint16_t scaled = (uint16_t)velocity * melodic_track_velocity[track] / 127u;
    velocity        = scaled ? scaled : 1;
    seq_melodic_release_base(track, base, now);
    if (!seq_chord_mode[track]) {
        if (stored || euclid_tracks[track].pitch_mode != 2) note = seq_scale_apply(track, note);
    }
    seq_accumulator_register_output(track, base, note);
    if (!melodic_active_contains(track, note)) {
        if (!seq_module_preview_owns(track, note)) virtual_midi_note_on(midi_channel_for_track(track), note, velocity, now);
        melodic_active_add(track, note);
    }
}

#include "seq_variation.inc"

/* A playback limit never resizes or serializes the underlying project. */
static void seq_loop_set(uint16_t steps, uint32_t now) {
    if (seq_loop_steps == steps) return;
    seq_loop_steps = steps;
    seq_midi_all_notes_off(now);
    seq_ratchet_clear();
    for (uint8_t track = 0; track < 12; track++) seq_track_sync_position(track);
    seq_resume_after_skip = true;
}

static bool seq_advance_step(void) {
    uint32_t previous = seq_step_position;
    bool     first    = seq_first_step;
    if (!first) {
        seq_step_position++;
    }
    /* Restore before recording, interval calculation and note emission so the
     * first pulse of the global bar already belongs to the original pattern. */
    if (seq_temp_restore_pending && seq_step_position % SEQ_STEPS_PER_BAR == 0) seq_temp_restore(timer_read32());
    if (seq_step_position % SEQ_STEPS_PER_BAR == 0) seq_variation_apply_pending(timer_read32());
    for (uint8_t track = 0; track < 12; track++) seq_track_sync_position(track);
    seq_first_step = false;
    seq_record_step_tick(previous, seq_step_position);
    return first;
}

static void seq_skip_step(void) {
    seq_resume_after_skip = true;
    seq_ratchet_clear();
    /* Retain the final controller state without transmitting the discarded
     * intermediate automation points as a burst. */
    for (uint16_t i = 0; i < seq_automation_count(); i++) {
        const seq_cc_event_t *event = seq_automation_event(i);
        if (!cc_automation_manual[event->track] && event->step == seq_track_step_position[event->track]) {
            cc_values[event->track][event->index] = event->value;
            seq_cc_pending[event->track] |= (uint16_t)(1u << event->index);
        }
    }
}

static void seq_emit_step(uint32_t now, bool first) {
    first |= seq_resume_after_skip;
    seq_resume_after_skip = false;
    seq_ratchet_clear();
    uint16_t view_step = seq_track_step_position[view_slot % 12];
    step_idx           = view_step % SEQ_STEPS_PER_BAR;
    playhead_bar       = view_step / SEQ_STEPS_PER_BAR;
    if (follow_mode && !switch_state.held) seq_set_view_bar(playhead_bar);
    for (uint8_t track = 0; track < 12; track++) {
        seq_cc_automation_apply(track, seq_track_step_position[track], now);
        for (uint8_t index = 0; index < SEQ_CC_MAPPING_COUNT; index++)
            if (!cc_automation_manual[track] && (seq_cc_pending[track] & (1u << index))) seq_cc_send_value(track, index, cc_values[track][index], now);
        seq_cc_pending[track] = 0;
    }
    seq_midi_stop_drums(now);
    for (uint8_t channel = 0; channel < 12; channel++) {
        if (!slot_has_drum_layer(channel)) continue;
        uint8_t  midi_ch    = midi_channel_for_track(channel);
        uint16_t local_step = seq_track_step_position[channel];
        for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
            if (!seq_drum_step_get(channel, note, local_step)) {
                continue;
            }
            if (drum_lane_muted[channel][note]) {
                continue;
            }
            uint8_t velocity = drum_track_velocity[channel % 12];
            if (!velocity) {
                velocity = 1;
            }
            seq_drum_step_mod_t *mod = seq_drum_step_mod_lookup(channel, note, local_step);
            if (mod) {
                switch (seq_drum_mod_velocity(mod)) {
                    case 1:
                        velocity = seq_u8_sat_add(velocity, 20);
                        break;
                    case 2:
                        velocity = (velocity > 40) ? (uint8_t)(velocity - 40) : 1;
                        break;
                    default:
                        break;
                }
            }
            uint16_t absolute_step = local_step;
            bool     base_hit      = seq_drum_step_base_hit(channel, note, absolute_step);
            uint16_t hit_index     = 0;
            bool     euclid_hit    = seq_drum_euclid_hit(channel, note, absolute_step, &hit_index);
            seq_euclid_track_t *lane_euclid = seq_drum_euclid_state(channel, note);
            if (euclid_hit && !base_hit && lane_euclid->probability < 100 &&
                (!lane_euclid->probability || (uint8_t)(rand() % 100) >= lane_euclid->probability)) continue;
            if (euclid_hit && !base_hit) {
                uint8_t swing = lane_euclid->swing_amount;
                if (swing) {
                    velocity = seq_velocity_apply_swing(velocity, swing, hit_index);
                }
            }
            virtual_midi_note_on(midi_ch, drum_note_for_track(note), velocity, now);
            drum_note_gate_mask[channel] |= (uint16_t)(1u << note);
            uint8_t ratchet = mod ? seq_drum_mod_ratchet(mod) : 0;
            if (euclid_hit) {
                seq_euclid_track_t *euclid_state = lane_euclid;
                if (euclid_state->ratchet_threshold > 0 && euclid_state->fills >= euclid_state->ratchet_threshold && ratchet == 0) {
                    uint8_t spacing_units = SEQ_DRUM_RATCHET_SPACING_STEPS / 2;
                    if (!spacing_units) {
                        spacing_units = 1;
                    }
                    seq_ratchet_schedule_repeats(channel, note, velocity, 1, spacing_units, step_started_ms, now);
                }
            }
            if (ratchet > 0) {
                uint8_t spacing_units = seq_drum_mod_spacing_units(mod);
                seq_ratchet_schedule_repeats(channel, note, velocity, ratchet, spacing_units, step_started_ms, now);
            }
        }
    }

    mel_last_start_mask = 0;
    mel_last_stop_mask  = 0;
    for (uint8_t track = 0; track < 12; track++) {
        if (!slot_is_melodic(track)) continue;
        bool track_first = first || (seq_variation_rejoin & (1u << track));
        seq_variation_rejoin &= ~(1u << track);
        bool varied = seq_accumulators[track].enabled || seq_variations[track].enabled;
        if (varied != !!(seq_variation_playing & (1u << track))) {
            seq_midi_stop_melodic_track(track, now);
            seq_variation_playing ^= 1u << track;
            track_first = true;
        }
        if (varied) { seq_variation_emit(track, now, track_first); continue; }
        uint16_t            step   = seq_track_step_position[track];

        uint8_t              desired[16] = {0};
        uint8_t              count       = mel_track_event_count(track);
        bool                 euclidean   = seq_modules[track].kind == SEQ_MODULE_EUCLID && euclid_tracks[track].enabled;
        mel_event_snapshot_t event;
        for (uint8_t i = 0; i < count; i++) {
            mel_track_event_at(track, i, &event);
            if (!melodic_track_muted[track] && event.start_step <= step && event.end_step >= step) desired[event.pitch >> 3] |= (uint8_t)(1u << (event.pitch & 7));
        }
        for (uint8_t base = 0; base < 128; base++) {
            if (euclidean || !(desired[base >> 3] & (1u << (base & 7)))) seq_melodic_release_base(track, base, now);
        }
        /* Euclid uses recorded spans or the picked notes. Evaluate a hit once for
         * the whole chord, and release each gate on the next step. */
        if (euclidean && (!seq_euclid_step_hit(track, step, NULL, NULL) || !seq_euclid_probability_allows(track))) continue;
        uint8_t triggered[16] = {0};
        bool has_source = false;
        for (uint8_t i = 0; i < count; i++) {
            mel_track_event_at(track, i, &event);
            bool starts = event.start_step == step || ((track_first || euclidean) && event.start_step <= step && event.end_step >= step);
            if (starts) has_source = true;
            if (starts && !(triggered[event.pitch >> 3] & (1u << (event.pitch & 7)))) {
                triggered[event.pitch >> 3] |= (uint8_t)(1u << (event.pitch & 7));
                seq_melodic_play_note(track, event.pitch, event.velocity, step, now, true);
                mel_last_start_mask |= (uint16_t)(1u << track);
            }
        }
        if (euclidean && !has_source) {
            uint8_t notes[SEQ_MELODIC_KEY_COUNT];
            uint8_t voices = seq_melodic_euclid_fallback(track, step, notes);
            for (uint8_t i = 0; i < voices; i++) seq_melodic_play_note(track, notes[i], MEL_DEFAULT_VELOCITY, step, now, false);
        }
    }
}

static uint32_t seq_next_interval(void) {
    uint32_t denominator = (uint32_t)bpm * 100u;
    if (seq_clock_denominator != denominator) {
        seq_clock_denominator = denominator;
        seq_clock_error       = 0;
    }
    uint32_t weight    = (seq_step_position & 1u) ? (100u - seq_timing_swing) : (100u + seq_timing_swing);
    uint32_t numerator = 15000u * weight + seq_clock_error;
    seq_clock_error    = numerator % denominator;
    return numerator / denominator;
}

/* Also used by host tests that explicitly advance a single step. */
#ifdef SEQ_ENGINE_TEST
static void seq_play_step(uint32_t now) {
    bool first = seq_advance_step();
    step_started_ms = now;
    seq_step_duration_ms = seq_next_interval();
    seq_emit_step(now, first);
}
#endif

static uint32_t seq_external_deadline(uint8_t offset) {
    uint32_t position = seq_step_position + (seq_first_step ? 0u : 1u) + offset;
    uint32_t delay = (position & 1u) ? seq_external_step_interval * seq_timing_swing / 100u : 0;
    return seq_external_due[(seq_external_head + offset) % 8u] + delay;
}

void seq_matrix_scan(void) {
    uint32_t now = timer_read32();
    if (seq_step_hold.active && !seq_step_hold.editing && TIMER_DIFF_32(now, seq_step_hold.started) >= SEQ_STEP_HOLD_MS) seq_step_hold_begin();
    virtual_midi_set_tempo(bpm);
    virtual_midi_task(now);
    if (virtual_midi_transport_fault() && seq_state.running) {
        seq_transport_stop(now);
        seq_drum_pool_report_full();
    }
    if (!seq_state.active || !seq_state.running) return;
    if (seq_external_clock) {
        if (seq_external_have_clock && TIMER_DIFF_32(now, seq_external_last_clock) > 2000u) {
            seq_transport_stop(now);
            return;
        }
        uint32_t measured = TIMER_DIFF_32(seq_external_last_clock, seq_external_measure_ms);
        if (seq_external_measure_pulses >= 6 && measured) {
            uint32_t interval = measured * 6u / seq_external_measure_pulses;
            if (interval && interval <= 6000u) seq_external_step_interval = interval;
            seq_external_measure_ms = seq_external_last_clock;
            seq_external_measure_pulses = 0;
        }
        /* Look through a coalesced arrival batch: a future swung odd step
         * must not hold an already-due even step behind it. */
        int8_t latest = -1;
        for (uint8_t i = 0; i < seq_external_pending; i++)
            if (timer_expired32(now, seq_external_deadline(i))) latest = i;
        while (latest >= 0) {
            step_started_ms = seq_external_deadline(0);
            seq_external_head = (seq_external_head + 1u) % 8u;
            seq_external_pending--;
            bool first = seq_advance_step();
            uint32_t long_step = seq_external_step_interval * (100u + seq_timing_swing) / 100u;
            seq_step_duration_ms = (seq_step_position & 1u) ? 2u * seq_external_step_interval - long_step : long_step;
            if (latest-- > 0) {
                seq_skip_step();
                continue;
            }
            seq_emit_step(now, first);
        }
        if (!timer_expired32(now, step_started_ms + seq_step_duration_ms)) seq_ratchet_process(now);
        virtual_midi_flush();
        return;
    }
    /* Skip expired steps as a group, preserving the common phase and loop
     * positions. Never replay missed notes or repeats in a catch-up burst. */
    for (uint8_t catchup = 0; catchup < 128 && timer_expired32(now, seq_next_step_ms); catchup++) {
        step_started_ms = seq_next_step_ms;
        bool first = seq_advance_step();
        seq_step_duration_ms = seq_next_interval();
        seq_next_step_ms += seq_step_duration_ms;
        if (timer_expired32(now, seq_next_step_ms)) {
            seq_skip_step();
            continue;
        }
        if (!seq_timing_swing || !(seq_step_position & 1u)) virtual_midi_resync_clock(seq_step_position, step_started_ms, now);
        seq_emit_step(now, first);
    }
    if (!timer_expired32(now, seq_next_step_ms)) seq_ratchet_process(now);
    virtual_midi_flush();
}

static void seq_transport_start(uint32_t now, uint32_t position) {
    seq_flush_pending_erase();
    seq_midi_all_notes_off(now);
    seq_ratchet_clear();
    seq_step_position = position;
    for (uint8_t track = 0; track < 12; track++) {
        seq_track_sync_position(track);
        seq_accumulator_reset_runtime(track);
    }
    seq_first_step          = true;
    seq_resume_after_skip   = false;
    memset(seq_cc_pending, 0, sizeof(seq_cc_pending));
    seq_clock_error         = 0;
    seq_clock_denominator   = 0;
    seq_next_step_ms        = now;
    step_started_ms         = now;
    seq_external_pending    = 0;
    seq_external_head       = 0;
    seq_external_measure_ms = now;
    seq_external_measure_pulses = 0;
    seq_external_step_interval = 15000u / bpm;
    seq_external_pulses     = 0;
    seq_external_have_clock = false;
    seq_running = seq_state.running = true;
    virtual_midi_set_clock_output(!seq_external_clock);
    if (!position) {
        virtual_midi_reset(now);
        virtual_midi_start(now);
    } else {
        virtual_midi_song_position(position, now);
        virtual_midi_continue(now);
    }
    if (!seq_external_clock) seq_matrix_scan();
}

static void seq_transport_stop(uint32_t now) {
    seq_palette_cancel();
    seq_loop_held = false;
    seq_loop_set(0, now);
    seq_chord_cancel();
    seq_flush_pending_erase();
    seq_record_on_transport_stop(seq_step_position);
    seq_running = seq_state.running = false;
    if (seq_temp_restore_pending) seq_temp_restore(now);
    seq_variation_apply_pending(now);
    seq_external_pending            = 0;
    seq_midi_all_notes_off(now);
    seq_ratchet_clear();
    virtual_midi_stop(now);
    seq_storage_save_pending = true;
}

void seq_midi_realtime(uint8_t message, uint32_t now) {
    if (seq_transfer_active || !seq_state.active || !seq_external_clock) return;
    if (message == 0xFA) {
        seq_transport_start(now, 0);
        return;
    }
    if (message == 0xFB) {
        seq_transport_start(now, seq_external_song_position);
        return;
    }
    if (message == 0xFC) {
        seq_external_song_position = seq_step_position + 1u;
        seq_transport_stop(now);
        return;
    }
    if (message != 0xF8 || !seq_state.running) return;
    if (!seq_external_have_clock) {
        seq_external_measure_ms = now;
    } else {
        seq_external_measure_pulses++;
    }
    seq_external_last_clock = now;
    seq_external_have_clock = true;
    if (seq_external_pulses == 0) {
        if (seq_external_pending >= 8) {
            seq_transport_stop(now);
            seq_drum_pool_report_full();
            return;
        }
        /* Store arrival time; apply swing after measuring the whole input
         * batch, rather than multiplying the gap of its first pulse by six. */
        seq_external_due[(seq_external_head + seq_external_pending) % 8u] = now;
        seq_external_pending++;
    }
    seq_external_pulses = (seq_external_pulses + 1u) % 6u;
}

void seq_midi_song_position(uint16_t position) {
    if (!seq_transfer_active) seq_external_song_position = position;
}

/* Global tempo takes priority over performance editors, including CC dispatch. */
static bool seq_tempo_encoder_update(uint8_t index, bool clockwise) {
    if (index != 0 || !seq_state.active || !((seq_ctrl_mask | get_mods()) & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_RCTL)))) return false;
    if (seq_transfer_active || seq_external_clock) return true;
    int32_t delta = clockwise ? 1 : -1;
    if (seq_shift_active() || (get_mods() & (MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT)))) delta *= 5;
    uint16_t next = clamp_bpm((int32_t)bpm + delta);
    if (next != bpm) {
        bpm = next;
        seq_mark_dirty();
    }
    seq_bpm_preview_arm(timer_read32());
    return true;
}

static bool seq_loop_encoder_update(uint8_t index, bool clockwise) {
    if (index != 0 || !seq_loop_held || !seq_state.active) return false;
    if (seq_transfer_active || record_state.armed) return true;
    uint16_t maximum = 1;
    for (uint8_t track = 0; track < 12; track++) {
        uint16_t length = seq_track_length_steps(track);
        if (length > maximum) maximum = length;
    }
    int32_t next = seq_loop_steps ? seq_loop_steps : seq_track_length_steps(view_slot);
    next += clockwise ? 1 : -1;
    if (next < 1) next = 1;
    if (next > maximum) next = maximum;
    seq_loop_set((uint16_t)next, timer_read32());
    return true;
}

bool seq_cc_encoder_update(uint8_t index, bool clockwise) {
    if (seq_tempo_encoder_update(index, clockwise)) return true;
    if (seq_loop_encoder_update(index, clockwise)) return true;
    if (seq_palette_encoder(index, clockwise)) return true;
    if (index != 0) {
        return false;
    }
    if (!cc_session.active) {
        return false;
    }
    int8_t delta = clockwise ? 1 : -1;
    seq_cc_adjust_value(delta, timer_read32());
    return true;
}

bool seq_encoder_update(uint8_t index, bool clockwise) {
    if (seq_tempo_encoder_update(index, clockwise)) return false;
    if (seq_loop_encoder_update(index, clockwise)) return false;
    if (seq_palette_encoder(index, clockwise)) return false;
    if (index == 0 && seq_chord_encoder(clockwise ? 1 : -1)) return false;
    if (seq_step_hold.active && index == 0) {
        seq_step_hold_begin();
        if (seq_step_hold.editing) seq_step_edit_encoder_delta(clockwise ? 1 : -1);
        return false;
    }
    if (seq_transfer_active) return false;
    if (!seq_state.active || index != 0) {
        return true;
    }

    if (switch_state.held) {
        int16_t next          = (int16_t)seq_timing_swing + (clockwise ? 1 : -1);
        seq_timing_swing      = next < 0 ? 0 : next > 50 ? 50 : next;
        switch_state.consumed = true;
        seq_track_velocity_preview(view_slot, false, seq_timing_swing, timer_read32());
        seq_mark_dirty();
        return false;
    }

    if (cc_session.active) {
        seq_cc_adjust_value(clockwise ? 1 : -1, timer_read32());
        return false;
    }

    int8_t held_track = seq_first_held_track();
    if (held_track >= 0 && !step_edit.active) {
        seq_track_length_input_t *input = &track_length_input[(uint8_t)held_track];
        if (input->digits == 0) {
            int16_t delta = clockwise ? 1 : -1;
            if (seq_shift_active()) {
                delta *= 5;
            }
            if (delta != 0) {
                uint32_t now = timer_read32();
                if (slot_uses_melodic((uint8_t)held_track)) {
                    uint8_t melodic_index = slot_melodic_track((uint8_t)held_track) % MEL_TRACK_COUNT;
                    int16_t current = melodic_track_velocity[melodic_index];
                    int16_t next    = current + delta;
                    if (next < 1) {
                        next = 1;
                    } else if (next > 127) {
                        next = 127;
                    }
                    if (next != current) {
                        melodic_track_velocity[melodic_index] = (uint8_t)next;
                        seq_mark_dirty();
                    }
                    seq_track_velocity_preview((uint8_t)held_track, true, (uint16_t)next, now);
                } else {
                    uint8_t drum_index = slot_drum_track((uint8_t)held_track);
                    int16_t current = drum_track_velocity[drum_index];
                    int16_t next    = current + delta;
                    if (next < 1) {
                        next = 1;
                    } else if (next > 127) {
                        next = 127;
                    }
                    if (next != current) {
                        drum_track_velocity[drum_index] = (uint8_t)next;
                        seq_mark_dirty();
                    }
                    seq_track_velocity_preview((uint8_t)held_track, false, (uint16_t)next, now);
                }
            }
            return false;
        }
    }

    if (euclid_param_mode != EUCLID_PARAM_MODE_NONE) {
        uint32_t now = timer_read32();
        if (seq_euclid_apply_encoder(clockwise ? 1 : -1, now)) {
            return false;
        }
    }

    if (step_edit.active) {
        seq_step_edit_encoder_delta(clockwise ? 1 : -1);
        return false;
    }

    int32_t delta = clockwise ? 1 : -1;
    if (seq_shift_active()) {
        delta *= 5;
    }
    bpm = clamp_bpm((int32_t)bpm + delta);
    seq_mark_dirty();
    seq_bpm_preview_arm(timer_read32());
    return false;
}

static void seq_handle_melodic_step(uint8_t slot_index, uint16_t absolute_step, uint16_t track_step, uint16_t track_length, uint32_t now) {
    uint8_t  melodic_track = slot_melodic_track(slot_index);
    uint16_t key_mask      = melodic_key_mask[melodic_track];
    uint16_t chord_mask    = key_mask;
    bool     has_chord     = chord_mask != 0;

    if (chord_capture.active) {
        if (seq_chord_capture_commit(slot_index, absolute_step)) {
            melodic_rest_pending[melodic_track] = false;
            melodic_tie_pending[melodic_track]  = false;
        }
        return;
    }

    if (has_chord && key_mask != 0 && seq_chord_is_double_tap(melodic_track, track_step, chord_mask, now)) {
        seq_chord_capture_begin(slot_index, melodic_track, absolute_step, chord_mask, now);
        melodic_rest_pending[melodic_track] = false;
        melodic_tie_pending[melodic_track]  = false;
        seq_chord_record_tap(melodic_track, track_step, chord_mask, now);
        return;
    }

    if (melodic_rest_pending[melodic_track]) {
        mel_track_remove_step_all(melodic_track, track_step);
        seq_melodic_set_last_mask(melodic_track, 0);
        melodic_rest_pending[melodic_track] = false;
        melodic_tie_pending[melodic_track]  = false;
        seq_chord_tap_reset();
        return;
    }

    if (melodic_tie_pending[melodic_track]) {
        bool placed_chord = false;
        if (has_chord) {
            uint16_t prev_step = (track_step == 0) ? (track_length - 1u) : (track_step - 1u);
            for (uint8_t semitone = 0; semitone < SEQ_MELODIC_KEY_COUNT; semitone++) {
                if (!(chord_mask & (uint16_t)(1u << semitone))) {
                    continue;
                }
                uint8_t pitch = seq_melodic_note_pitch(melodic_track, semitone);
                if (track_step > prev_step) {
                    if (!mel_track_extend_note(melodic_track, prev_step, track_step, pitch)) mel_track_add_note(melodic_track, prev_step, track_step, pitch, MEL_DEFAULT_VELOCITY);
                } else {
                    mel_track_add_note(melodic_track, prev_step, prev_step, pitch, MEL_DEFAULT_VELOCITY);
                    mel_track_add_note(melodic_track, track_step, track_step, pitch, MEL_DEFAULT_VELOCITY);
                }
                placed_chord = true;
            }
            seq_melodic_set_last_mask(melodic_track, chord_mask);
        }
        melodic_tie_pending[melodic_track]  = false;
        melodic_rest_pending[melodic_track] = false;
        if (placed_chord) {
            seq_chord_record_tap(melodic_track, track_step, chord_mask, now);
        } else {
            seq_chord_tap_reset();
        }
        return;
    }

    bool placed_chord = false;
    if (!has_chord) {
        seq_pending_erase          = true;
        seq_pending_erase_track    = melodic_track;
        seq_pending_erase_step     = track_step;
        seq_pending_erase_deadline = now + SEQ_DOUBLE_TAP_MS + 1u;
        seq_melodic_set_last_mask(melodic_track, 0);
    } else {
        for (uint8_t semitone = 0; semitone < SEQ_MELODIC_KEY_COUNT; semitone++) {
            if (!(chord_mask & (uint16_t)(1u << semitone))) {
                continue;
            }
            uint8_t              pitch   = seq_melodic_note_pitch(melodic_track, semitone);
            bool                 present = false;
            mel_event_snapshot_t ev;
            for (uint8_t i = 0; i < mel_track_event_count(melodic_track); i++) {
                mel_track_event_at(melodic_track, i, &ev);
                if (ev.pitch == pitch && ev.start_step <= track_step && ev.end_step >= track_step) present = true;
            }
            if (present)
                mel_track_remove_note(melodic_track, track_step, pitch);
            else
                mel_track_add_note(melodic_track, track_step, track_step, pitch, MEL_DEFAULT_VELOCITY);
            placed_chord = true;
        }
        seq_melodic_set_last_mask(melodic_track, chord_mask);
    }
    melodic_rest_pending[melodic_track] = false;
    if (placed_chord) {
        seq_chord_record_tap(melodic_track, track_step, chord_mask, now);
    } else {
        seq_chord_tap_reset();
    }
}

#include "seq_module_integration.inc"
#include "seq_temp.inc"
#include "seq_scale_control.inc"
#include "seq_palette_controls.inc"
#include "seq_chord_controls.inc"

bool seq_pre_process(uint16_t keycode, keyrecord_t *record) {
    uint32_t now    = timer_read32();
    bool     pressed = record->event.pressed;
    /* Reserve Ctrl+Backslash for the desktop visibility shortcut in every editor.
     * Track its release even if Ctrl is released first or sequencer mode changes. */
    static bool desktop_shortcut_held = false;
    if (keycode == KC_BSLS && (desktop_shortcut_held || (seq_state.active && ((seq_ctrl_mask | get_mods()) & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_RCTL)))))) {
        desktop_shortcut_held = pressed;
        return true;
    }
    if (keycode == KC_ENT) {
        seq_temp_enter_held = pressed;
        if (!pressed) {
            seq_palette_cancel();
            seq_loop_held = false;
            seq_loop_set(0, now);
        } else if (!seq_loop_held) {
            seq_loop_held = seq_temp_gesture_ready();
        }
    } else if (pressed && keycode != KC_LSFT && keycode != KC_RSFT && keycode != KC_LCTL && keycode != KC_RCTL) {
        /* Another gesture ends the loop before it can edit or record data. */
        seq_loop_held = false;
        seq_loop_set(0, now);
    }
    if (keycode == KC_ESC && seq_temp_escape_held) {
        if (!pressed) seq_temp_escape_held = false;
        return false;
    }
    if (seq_state.active && keycode == KC_ESC && pressed && (seq_temp_active || seq_temp_enter_held)) {
        seq_temp_escape_held = true;
        if (seq_temp_enter_held) {
            if (seq_temp_active) seq_temp_commit();
            else seq_temp_begin(now);
        } else if (seq_temp_active && seq_state.running && (seq_shift_active() || seq_shift_pressed())) {
            seq_temp_restore_pending = !seq_temp_restore_pending;
        } else seq_temp_restore(now);
        return false;
    }
    if (seq_step_hold.active && !seq_step_hold_replaying) {
        if (!seq_state.active || seq_transfer_active || view_slot != seq_step_hold.slot) seq_step_hold_finish(false);
        else if (keycode == seq_step_hold.key) {
            if (!pressed) {
                if (TIMER_DIFF_32(now, seq_step_hold.started) >= SEQ_STEP_HOLD_MS) seq_step_hold_begin();
                seq_step_hold_finish(true);
            }
            return false;
        } else if (pressed) {
            if (slot_uses_melodic(seq_step_hold.slot) && (keycode == KC_I || keycode == KC_O || keycode == KC_P)) {
                /* A parameter tap adopts the editor even before the hold timeout. */
                seq_step_hold_begin();
            } else seq_step_hold_finish(keycode != KC_ESC);
        }
    }

    if (keycode == KC_LCTL || keycode == KC_RCTL) {
        uint8_t bit = keycode == KC_LCTL ? MOD_BIT(KC_LCTL) : MOD_BIT(KC_RCTL);
        if (pressed) seq_palette_cancel();
        if (pressed) seq_ctrl_mask |= bit; else seq_ctrl_mask &= (uint8_t)~bit;
        return true; /* Preserve QMK modifier registration and release across modes. */
    }
    if (!pressed && keycode == KC_TAB) seq_modifiers &= (uint8_t)~SEQ_MOD_TAB;
    if (!pressed && keycode == KC_CAPS) seq_modifiers &= (uint8_t)~SEQ_MOD_CAPS;
    if (!init_done) seq_housekeeping();
    if (seq_palette_key(keycode, pressed)) return false;
    if (seq_harmony_key(keycode, pressed)) return false;
    if (seq_scale_key(keycode, pressed)) return false;
    if (seq_chord_shortcut(keycode, pressed)) return false;
    if (seq_chord_key(keycode, pressed, now)) return false;
    if (module_preview.active && ((!pressed && (keycode == module_preview.key || seq_is_option_key(keycode))) || (pressed && (keycode == SEQ_SWITCH_KEY || keycode == KC_ESC)))) seq_module_preview_stop(now);
    if (seq_pending_erase && timer_expired32(now, seq_pending_erase_deadline)) seq_flush_pending_erase();
    if (seq_transfer_active) return !seq_state.active;
    if (!seq_state.active && (keycode == SEQ_SWITCH_KEY || keycode == KC_PGDN)) return true;
    if (seq_state.active) {
        uint8_t bit = shift_bit_for(keycode);
        if (bit) {
            if (pressed && cmd_state.held) cmd_state.consumed = true;
            if (pressed)
                seq_shift_mask |= bit;
            else
                seq_shift_mask &= (uint8_t)~bit;
            return false;
        }
        if (keycode == KC_ESC && pressed && seq_handle_escape(now)) return false;
        if (seq_track_confirmation_active()) {
            if (pressed && keycode == KC_Y)
                seq_track_accept_confirmation(now);
            else if (pressed && keycode == KC_N)
                seq_track_cancel_confirmation();
            return false;
        }
    }


    if (seq_state.active && cmd_state.held && !seq_shift_active() && seq_mod_del() &&
        slot_uses_melodic(view_slot) && seq_euclid_editing_enabled()) {
        int8_t digit = seq_digit_from_keycode(keycode);
        if (digit >= 1 && digit <= SEQ_EUCLID_NOTE_POOL_MAX) {
            cmd_state.consumed = true;
            if (pressed && !record_state.armed && !step_edit.active && !chord_capture.active && !euclid_palette_capture.active) {
                seq_euclid_palette_remove(view_slot, (uint8_t)(digit - 1), now);
            }
            return false;
        }
    }

    if (seq_state.active && cmd_state.held && seq_shift_active() && slot_uses_melodic(view_slot)) {
        if (keycode == KC_DEL && seq_euclid_editing_enabled()) {
            cmd_state.consumed = true;
            seq_modifiers &= (uint8_t)~SEQ_MOD_DEL;
            if (pressed && !record_state.armed && !step_edit.active && !chord_capture.active) {
                uint8_t track = view_slot % 12;
                seq_euclid_palette_capture_reset();
                seq_midi_stop_melodic_track(slot_melodic_track(track), now);
                euclid_tracks[track].note_count = 0;
                memset(seq_modules[track].config.euclid.qualities, 0, 8);
                memset(euclid_tracks[track].note_pool, 0, sizeof(euclid_tracks[track].note_pool));
                euclid_tracks[track].palette_pin = 255;
                euclid_tracks[track].palette_authored = true;
                seq_accumulator_reset_runtime(track);
                seq_mark_dirty();
            }
            return false;
        }
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
            if (key.keycode == keycode) {
                cmd_state.consumed = true;
                if (pressed) seq_euclid_palette_capture_record(view_slot, key.semitone);
                return false;
            }
        }
    }
    if (pressed) {
        int8_t held_track = seq_first_held_track();
        if (held_track >= 0) {
            bool shift = seq_shift_active();
            if (keycode == KC_COMMA || keycode == KC_DOT) {
                int8_t  delta       = (keycode == KC_DOT) ? 1 : -1;
                uint8_t track_index = (uint8_t)held_track;
                if (!shift) {
                    uint8_t current = track_midi_channel[track_index];
                    int16_t next    = (int16_t)current + delta;
                    if (next < 0) {
                        next = 0;
                    } else if (next > 15) {
                        next = 15;
                    }
                    if (next != current) {
                        seq_midi_all_notes_off(now);
                        seq_ratchet_clear();
                        track_midi_channel[track_index] = (uint8_t)next;
                        seq_mark_dirty();
                        seq_track_velocity_preview(track_index, slot_uses_melodic(track_index), (uint16_t)next, now);
                    }
                } else if (slot_has_drum_layer(track_index)) {
                    uint8_t drum_channel = slot_drum_track(track_index);
                    uint8_t lane         = drum_active_note[drum_channel];
                    uint8_t current_note = drum_note_map[lane];
                    int16_t next         = (int16_t)current_note + delta;
                    if (next < 0) {
                        next = 0;
                    } else if (next > 127) {
                        next = 127;
                    }
                    if (next != current_note) {
                        seq_midi_stop_drums(now);
                        seq_ratchet_clear();
                        drum_note_map[lane] = (uint8_t)next;
                        seq_mark_dirty();
                        seq_track_velocity_preview(track_index, false, (uint16_t)next, now);
                    }
                }
                return false;
            }
        }
    }

    if (switch_state.synthetic) {
        return true;
    }

    if (keycode == SEQ_SWITCH_KEY) {
        if (pressed) {
            switch_state.held     = true;
            switch_state.stage    = SEQ_SWITCH_STAGE_ROOT;
            switch_state.consumed = false;
            seq_bar_nav_reset();
            seq_mute_pending_reset();
            return false;
        }

        switch_state.held  = false;
        switch_state.stage = SEQ_SWITCH_STAGE_INACTIVE;
        seq_bar_nav_reset();
        seq_mute_pending_reset();

        return false;
    }

    if (switch_state.held) {
        switch (switch_state.stage) {
            case SEQ_SWITCH_STAGE_ROOT:
                if (pressed) {
                    if (keycode == KC_ENTER) {
                        switch_state.consumed = true;
                        if (seq_state.running) seq_transport_stop(now);
                        seq_storage_save_if_dirty();
                        return false;
                    }
                    if (keycode == KC_HOME) {
                        if (seq_state.running) seq_transport_stop(now);
                        seq_external_clock = !seq_external_clock;
                        virtual_midi_set_clock_output(!seq_external_clock);
                        seq_mark_dirty();
                        switch_state.consumed = true;
                        return false;
                    }
                    if (keycode == SEQ_CLEAR_MUTES_KEY) {
                        seq_mute_clear_all(now);
                        seq_mute_pending_reset();
                        switch_state.consumed = true;
                        return false;
                    }
                    int8_t track_key = track_from_keycode(keycode);
                    if (track_key >= 0) {
                        seq_handle_space_track_press((uint8_t)track_key, now);
                        switch_state.consumed = true;
                        return false;
                    }
                    int8_t digit = seq_digit_from_keycode(keycode);
                    if (digit >= 0) {
                        seq_bar_nav_handle_digit((uint8_t)digit, now);
                        return false;
                    }
                    if (keycode == KC_EQUAL) {
                        follow_mode = !follow_mode;
                        seq_mark_dirty();
                        if (follow_mode) {
                            seq_set_view_bar(playhead_bar);
                        }
                        switch_state.consumed = true;
                        return false;
                    }
                    if (keycode == SEQ_SWITCH_GROUP_KEY) {
                        switch_state.consumed = true;
                        switch_state.stage = SEQ_SWITCH_STAGE_SEQUENCER;
                        seq_mute_pending_reset();
                        return false;
                    }
                }
                return false;
            case SEQ_SWITCH_STAGE_SEQUENCER:
                if (pressed) {
                    if (keycode == KC_GRAVE) {
                        switch_state.consumed = true;
                        seq_apply_layout(SEQ_LAYOUT_DEFAULT);
                        switch_state.stage = SEQ_SWITCH_STAGE_COMMITTED;
                    } else if (keycode == KC_1) {
                        switch_state.consumed = true;
                        seq_apply_layout(SEQ_LAYOUT_GRID_4X4);
                        switch_state.stage = SEQ_SWITCH_STAGE_COMMITTED;
                    }
                }
                return false;
            case SEQ_SWITCH_STAGE_COMMITTED:
                return false;
            case SEQ_SWITCH_STAGE_INACTIVE:
            default:
                break;
        }
    }

    if (seq_cc_handle_cmd(keycode, pressed, now)) {
        return false;
    }

    if (seq_cmd_handle(keycode, record, now)) {
        return false;
    }

    if (seq_euclid_handle_param_key(keycode, record, now)) {
        return false;
    }

    if (pressed && cmd_state.held && seq_euclid_editing_enabled()) {
        int8_t palette_digit = seq_digit_from_keycode(keycode);
        if (palette_digit >= 0) {
            uint8_t track = seq_euclid_active_track();
            if (palette_digit == 0) {
                seq_euclid_palette_pin_set(track, -1, now);
            } else {
                seq_euclid_palette_pin_set(track, (int8_t)(palette_digit - 1), now);
            }
            return false;
        }
    }

    if (pressed && seq_euclid_editing_enabled()) {
        if (keycode == KC_LBRC || keycode == KC_RBRC) {
            int16_t delta = (keycode == KC_LBRC) ? -1 : 1;
            uint8_t track = seq_euclid_active_track();
            seq_euclid_enable(track, true);
            seq_euclid_adjust_rotate(track, delta);
            seq_euclid_focus_track(track);
            seq_euclid_capture_preview(track, EUCLID_PARAM_KIND_ROTATE, seq_euclid_param_value_for_kind(track, EUCLID_PARAM_KIND_ROTATE), now);
            return false;
        }
    }

    if (pressed && keycode == KC_TAB && cmd_state.held && seq_euclid_editing_enabled()) {
        seq_euclid_capture_window_template(now);
        return false;
    }

    if (seq_cc_handle_key_event(keycode, record, now)) {
        if (keycode == KC_PGDN) seq_toggle_reset(&seq_pgdn_toggle);
        return false;
    }

    if (!init_done) {
        seq_housekeeping();
    }

    seq_octave_hold_tick(now);

    if (seq_step_edit_handle_key(keycode, pressed, now)) {
        return false;
    }

    int8_t digit_key = seq_digit_from_keycode(keycode);
    if (digit_key >= 0) {
        if (seq_step_edit_handle_digit((uint8_t)digit_key, pressed)) {
            return false;
        }
        if (seq_track_length_handle_digit((uint8_t)digit_key, pressed, now)) {
            return false;
        }
    }

    if (keycode == SEQ_TRACK_STEP_MODE_KEY && pressed) {
        if (seq_track_length_start_step_mode(now)) {
            return false;
        }
    }

    if (keycode == KC_ESC && pressed) {
        if (seq_handle_escape(now)) {
            return false;
        }
    }


    if (seq_handle_melodic_keyboard(keycode, pressed, now)) {
        return false;
    }

    if (pressed && keycode == KC_PGUP) {
        bool toggled = seq_toggle_press(&seq_pgup_toggle, now, SEQ_DOUBLE_TAP_MS);
#if defined(SEQ_DEBUG_LOGGING) && (SEQ_DEBUG_LOGGING) && defined(CONSOLE_ENABLE)
        uint16_t span = seq_toggle_last_interval(&seq_pgup_toggle);
        SEQ_LOG("[seq] pgup press interval=%u active=%d toggled=%d\n", span, seq_state.active, toggled);
#else
        (void)seq_toggle_last_interval(&seq_pgup_toggle);
#endif
        if (toggled) {
            if (seq_state.active) {
                seq_exit();
            } else {
                seq_enter(now);
            }
            return false;
        }
        return seq_state.active ? false : true;
    }

    if (pressed && keycode == KC_PGDN) {
        bool toggled = seq_toggle_press(&seq_pgdn_toggle, now, SEQ_DOUBLE_TAP_MS);
        if (toggled) {
            if (record_state.armed) {
                seq_record_try_disarm(seq_step_position);
                seq_record_reset();
            } else {
                record_state = (seq_record_state_t){
                    .armed         = true,
                    .capturing     = false,
                    .is_melodic    = slot_uses_melodic(view_slot),
                    .capture_track = view_slot % 12,
                    .start_step    = seq_step_position,
                };
            }
        }
        return false;
    }

    if (!seq_state.active) {
        int8_t track = track_from_keycode(keycode);
        if (track >= 0 && !pressed) {
            if (TIMER_DIFF_32(now, track_last_tap_ms[track]) <= SEQ_DOUBLE_TAP_MS) {
                track_tap_count[track]++;
            } else {
                track_tap_count[track] = 1;
            }
            track_last_tap_ms[track] = now;
            if (track_tap_count[track] >= 2) {
                if (slot_has_drum_layer((uint8_t)track)) {
                    seq_set_default_track((uint8_t)track);
                    seq_select_root_track((uint8_t)track);
                }
                track_tap_count[track] = 0;
            }
        }
        return true;
    }

    if (keycode == KC_GRAVE) {
        if (pressed) {
            seq_modifiers |= SEQ_MOD_GRAVE;
            bool negative = seq_shift_active();
            seq_octave_hold_press(negative, now);
        } else {
            seq_modifiers &= (uint8_t)~SEQ_MOD_GRAVE;
            if (octave_hold_negative.pressed) {
                seq_octave_hold_release(true, now);
            } else if (octave_hold_positive.pressed) {
                seq_octave_hold_release(false, now);
            }
        }
        return false;
    }

    if (keycode == KC_TAB) {
        if (pressed) {
            seq_modifiers |= SEQ_MOD_TAB;
        } else {
            seq_modifiers &= (uint8_t)~SEQ_MOD_TAB;
        }
        return false;
    }

    if (keycode == KC_CAPS || keycode == KC_CAPS_LOCK) {
        if (pressed) {
            seq_modifiers |= SEQ_MOD_CAPS;
        } else {
            seq_modifiers &= (uint8_t)~SEQ_MOD_CAPS;
        }
        return false;
    }

    if (keycode == SEQ_CC_MODE_KEY && pressed) {
        if (cc_context.active) {
            seq_cc_exit_mode(now);
            cc_mode.latched       = false;
            cc_mode.simple_toggle = false;
        } else {
            uint8_t track = seq_cc_base_track();
            cc_mode.latched              = true;
            cc_mode.simple_toggle        = true;
            cc_mode.held                 = false;
            cc_mode.consumed             = true;
            cc_mode.temporary            = false;
            cc_mode.latched_temporary    = false;
            cc_mode.pending_track_valid  = false;
            cc_mode.tap_count            = 0;
            cc_mode.press_time           = now;
            cc_mode.last_tap_ms          = now;
            seq_cc_enter_mode(track, false, now);
        }
        return false;
    }

    if (keycode == KC_DEL
#ifdef KC_DELETE
        || keycode == KC_DELETE
#endif
    ) {
        if (pressed) {
            seq_modifiers |= SEQ_MOD_DEL;
        } else {
            seq_modifiers &= (uint8_t)~SEQ_MOD_DEL;
        }
        return false;
    }

    int8_t track = track_from_keycode(keycode);
    if (track >= 0) {
        uint8_t slot_index = (uint8_t)(track % 12);
        if (cc_context.active || cc_mode.held) {
            if (pressed) {
                track_press_ms[slot_index] = now;
                track_held[slot_index]     = true;
                cc_context.hold_override_active = true;
                cc_context.hold_track          = slot_index;
                if (cc_mode.held) {
                    cc_mode.pending_track       = slot_index;
                    cc_mode.pending_track_valid = true;
                }
                if (cc_mode.simple_toggle && cc_context.active) {
                    cc_context.track          = slot_index;
                    cc_context.track_override = (slot_index != cc_context.prev_track);
                    if (cc_context.track_override) {
                        seq_select_root_track(slot_index);
                    }
                }
            } else {
                track_held[slot_index] = false;
                cc_context.hold_override_active = false;
                if (cc_mode.simple_toggle && cc_context.active) {
                    cc_context.track          = slot_index;
                    cc_context.track_override = (slot_index != cc_context.prev_track);
                    if (cc_context.track_override) {
                        seq_select_root_track(slot_index);
                    }
                    track_tap_count[slot_index] = 0;
                    return false;
                }
                if (TIMER_DIFF_32(now, track_last_tap_ms[slot_index]) <= SEQ_DOUBLE_TAP_MS) {
                    track_tap_count[slot_index]++;
                } else {
                    track_tap_count[slot_index] = 1;
                }
                track_last_tap_ms[slot_index] = now;
                if (track_tap_count[slot_index] >= 2) {
                    cc_context.track = slot_index;
                    if (cc_mode.held) {
                        cc_mode.pending_track       = slot_index;
                        cc_mode.pending_track_valid = true;
                    }
                    track_tap_count[slot_index] = 0;
                }
            }
            return false;
        }

        if (seq_track_confirmation_active()) {
            return false;
        }
        uint16_t note_bit   = (uint16_t)(1u << (slot_index % SEQ_DRUM_NOTE_COUNT));
        bool     grave      = seq_mod_grave();
        bool     tab        = seq_mod_tab();
        bool     caps       = seq_mod_caps();
        bool     del        = seq_mod_del();

        uint8_t active_slot    = view_slot % 12;
        uint8_t active_channel = slot_drum_track(active_slot);
        bool     active_is_drum = !slot_uses_melodic(active_slot);

        if (pressed) {
            track_press_ms[track] = now;
            track_held[slot_index] = true;
            seq_track_length_reset(slot_index);
            track_length_input[slot_index].active = true;
            if (del) {
                if (slot_uses_melodic(slot_index) || seq_shift_mask == 3u) {
                    seq_clear_track(slot_index, now);
                    seq_euclid_reset_track(slot_index);
                    seq_euclid_sync_track(slot_index);
                } else {
                    uint8_t t = slot_drum_track(slot_index), n = drum_active_note[t];
                    seq_midi_stop_drums(now);
                    seq_ratchet_clear();
                    seq_euclid_reset_selected(slot_index);
                    seq_drum_pool_trim(t, n, 0);
                    drum_deleted_mod_note[t] = 0xFF;
                    seq_mark_dirty();
                }
                seq_track_reset_tap(track_tap_count, slot_index);
                seq_track_reset_tap(track_tab_tap_count, slot_index);
                seq_track_reset_tap(track_caps_tap_count, slot_index);
                return false;
            }
            if (!grave && !tab && !caps && active_is_drum) {
                note_hold_mask |= note_bit;
                if (track_held[active_slot] && slot_index != active_slot) {
                    uint8_t lane = clamp_note_index((uint8_t)slot_index);
                    drum_active_note[active_channel] = lane;
                    seq_lane_selection_used |= 1u << active_slot;
                }
                seq_record_update_mask(view_slot, false, note_hold_mask);
            }
        } else {
            uint32_t held_ms = TIMER_DIFF_32(now, track_press_ms[track]);
            track_held[slot_index] = false;

            if (!grave && !tab && !caps && active_is_drum) {
                note_hold_mask &= (uint16_t)~note_bit;
                seq_record_update_mask(view_slot, false, note_hold_mask);
            }

            if (del) {
                seq_track_reset_tap(track_tap_count, slot_index);
                seq_track_reset_tap(track_tab_tap_count, slot_index);
                seq_track_reset_tap(track_caps_tap_count, slot_index);
                track_last_tap_ms[track]     = 0;
                track_tab_last_tap_ms[track] = 0;
                track_caps_last_tap_ms[track] = 0;
                seq_track_length_finalize(slot_index);
                return false;
            }

            if (caps) {
                seq_track_reset_tap(track_tap_count, slot_index);
                seq_track_reset_tap(track_tab_tap_count, slot_index);
                if (held_ms < SEQ_TRACK_HOLD_MS) {
                    bool switch_after = seq_track_register_tap(track_caps_last_tap_ms, track_caps_tap_count, slot_index, now);
                    seq_track_request_kind(slot_index, SEQ_SLOT_KIND_DRUM, switch_after, now);
                } else {
                    seq_track_reset_tap(track_caps_tap_count, slot_index);
                    track_caps_last_tap_ms[slot_index] = 0;
                }
                seq_track_length_finalize(slot_index);
                return false;
            }

            if (tab) {
                seq_track_reset_tap(track_tap_count, slot_index);
                if (held_ms < SEQ_TRACK_HOLD_MS) {
                    bool switch_after = seq_track_register_tap(track_tab_last_tap_ms, track_tab_tap_count, slot_index, now);
                    seq_track_request_kind(slot_index, SEQ_SLOT_KIND_MELODIC, switch_after, now);
                } else {
                    seq_track_reset_tap(track_tab_tap_count, slot_index);
                    track_tab_last_tap_ms[slot_index] = 0;
                }
                seq_track_length_finalize(slot_index);
                return false;
            }

            if (seq_lane_selection_used & (1u << slot_index)) {
                seq_lane_selection_used &= ~(1u << slot_index);
                seq_track_length_finalize(slot_index);
                return false;
            }
            if (held_ms < SEQ_TRACK_HOLD_MS) {
                bool active_track_held = track_held[active_slot];
                if (active_track_held && active_is_drum && slot_index != active_slot) {
                    uint8_t drum_channel = active_channel;
                    drum_active_note[drum_channel] = clamp_note_index((uint8_t)slot_index);
                } else {
                    seq_track_switch(slot_index);
                    if (!euclid_mode_active && !slot_uses_melodic(slot_index)) {
                        uint8_t drum_channel = slot_drum_track(slot_index);
                        drum_active_note[drum_channel] = clamp_note_index((uint8_t)slot_index);
                    }
                }
            } else {
                seq_track_reset_tap(track_tap_count, slot_index);
                track_last_tap_ms[track] = 0;
            }

            seq_track_length_finalize(slot_index);
        }
        return false;
    }

    if (seq_track_confirmation_active()) {
        if (pressed) {
            if (keycode == KC_Y) {
                seq_track_accept_confirmation(now);
            } else if (keycode == KC_N) {
                seq_track_cancel_confirmation();
            }
        }
        return false;
    }

    if (!pressed) {
        return false;
    }

    if (seq_mod_tab() && pressed && (keycode == KC_EQUAL
#ifdef KC_KP_PLUS
        || keycode == KC_KP_PLUS
#endif
        )) {
        bool duplicate_contents = true;
        if ((seq_shift_mask & 0x03u) == 0x03u) {
            duplicate_contents = false;
        }
        seq_track_double_length(view_slot, duplicate_contents);
        return false;
    }

    if (keycode == KC_ESC) {
        if (!seq_state.active) {
            seq_toggle_reset(&seq_esc_toggle);
            return false;
        }
        bool toggled = seq_toggle_press(&seq_esc_toggle, now, SEQ_DOUBLE_TAP_MS);
#if defined(SEQ_DEBUG_LOGGING) && (SEQ_DEBUG_LOGGING) && defined(CONSOLE_ENABLE)
        uint16_t span = seq_toggle_last_interval(&seq_esc_toggle);
        SEQ_LOG("[seq] esc press interval=%u toggled=%d\n", span, toggled);
#else
        (void)seq_toggle_last_interval(&seq_esc_toggle);
#endif
        if (toggled) {
            seq_exit();
        }
        return false;
    }

    switch (keycode) {
        case KC_HOME:
            if (seq_state.running)
                seq_transport_stop(now);
            else
                seq_transport_start(now, 0);
            return false;
        case KC_BPM_UP: {
            int32_t delta = seq_shift_active() ? 5 : 1;
            bpm = clamp_bpm((int32_t)bpm + delta);
            seq_mark_dirty();
            seq_bpm_preview_arm(now);
            return false;
        }
        case KC_BPM_DN: {
            int32_t delta = seq_shift_active() ? 5 : 1;
            bpm = clamp_bpm((int32_t)bpm - delta);
            seq_mark_dirty();
            seq_bpm_preview_arm(now);
            return false;
        }
        default:
            break;
    }

    int8_t step = step_from_keycode(keycode);
    if (step >= 0) {
        uint16_t absolute_step = (uint16_t)view_bar * SEQ_STEPS_PER_BAR + (uint16_t)step;
        uint8_t  slot_index    = view_slot % 12;

        if (pressed) {
            if (!slot_uses_melodic(slot_index)) {
                uint8_t  channel = slot_drum_track(slot_index);
                uint16_t targets = note_hold_mask ? note_hold_mask : (uint16_t)(1u << drum_active_note[channel]);
                if (!seq_drum_can_reserve(channel, targets)) {
                    return false;
                }
            }
            seq_track_ensure_steps(slot_index, absolute_step + 1u);
        }

        uint16_t track_length = seq_track_length_steps(slot_index);
        if (!track_length) {
            track_length = SEQ_STEPS_PER_BAR;
        }
        if (!track_length) {
            track_length = 1;
        }
        uint16_t track_step = (uint16_t)(absolute_step % track_length);

        bool double_tap_for_edit = false;
        if (pressed && !seq_step_hold_replaying) {
            uint32_t info     = step_last_tap_info[slot_index];
            uint16_t prev_step = (uint16_t)(info >> 16);
            uint16_t prev_time = (uint16_t)info;
            uint16_t now16     = (uint16_t)now;
            if (info != 0) {
                uint16_t diff = (uint16_t)(now16 - prev_time);
                if (diff <= SEQ_DOUBLE_TAP_MS && prev_step == track_step) {
                    double_tap_for_edit = true;
                }
            }
            step_last_tap_info[slot_index] = ((uint32_t)track_step << 16) | now16;
            if (!double_tap_for_edit) {
                drum_deleted_mod_note[slot_drum_track(slot_index)] = 0xFF;
            }
        }

        if (pressed) {
            if (slot_uses_melodic(slot_index)) {
                uint8_t  melodic_track = slot_melodic_track(slot_index);
                uint16_t key_mask = melodic_key_mask[melodic_track];
                bool allow_without_shift = double_tap_for_edit && !chord_capture.active && key_mask == 0;
                if (seq_pending_erase) {
                    if (allow_without_shift && seq_pending_erase_track == melodic_track && seq_pending_erase_step == track_step)
                        seq_pending_erase = false;
                    else
                        seq_flush_pending_erase();
                }
                if ((allow_without_shift || seq_shift_active())
                    && seq_step_edit_try_begin(slot_index, absolute_step, allow_without_shift, now)) {
                    if (allow_without_shift) {
                        step_last_tap_info[slot_index] = 0;
                    }
                    return false;
                }
            } else {
                bool allow_without_shift = double_tap_for_edit;
                if ((double_tap_for_edit || seq_shift_active())
                    && seq_step_edit_try_begin(slot_index, absolute_step, allow_without_shift, now)) {
                    if (allow_without_shift) {
                        step_last_tap_info[slot_index] = 0;
                    }
                    return false;
                }
            }
        }

        if (pressed && seq_step_hold_arm(keycode, slot_index, absolute_step, true, now)) return false;

        if (pressed) {
            if (slot_uses_melodic(slot_index)) {
                uint16_t previous_length = track_length_steps[slot_index];
                uint16_t previous_bars   = track_length_bars[slot_index];
                mel_track_edit_begin(slot_melodic_track(slot_index));
                seq_handle_melodic_step(slot_index, absolute_step, track_step, track_length, now);
                if (!mel_track_edit_end()) {
                    track_length_steps[slot_index] = previous_length;
                    track_length_bars[slot_index]  = previous_bars;
                    seq_track_sync_position(slot_index);
                    seq_update_total_steps();
                }
            } else {
                uint8_t  drum_channel = slot_drum_track(slot_index);
                uint16_t targets      = note_hold_mask ? note_hold_mask : (uint16_t)(1u << drum_active_note[drum_channel]);
                targets &= (uint16_t)((1u << SEQ_DRUM_NOTE_COUNT) - 1);
                uint8_t              first_note     = first_note_from_mask(targets, drum_active_note[drum_channel]);
                seq_drum_step_mod_t *deleted        = seq_drum_step_mod_lookup(drum_channel, first_note, track_step);
                drum_deleted_mod_note[drum_channel] = 0xFF;
                if (deleted && seq_drum_step_base_hit(drum_channel, first_note, absolute_step)) {
                    drum_deleted_mod[drum_channel]      = *deleted;
                    drum_deleted_mod_note[drum_channel] = first_note;
                }
                for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
                    if (!(targets & (1u << note))) {
                        continue;
                    }
                    seq_drum_step_toggle(drum_channel, note, absolute_step);
                }
            }
            seq_step_hold_arm(keycode, slot_index, absolute_step, false, now);
        }
        return false;
    }

    return false;
}

static bool seq_track_length_render_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
    if (step_edit.active) {
        return false;
    }

    uint8_t active_track = 0xFF;
    seq_track_length_input_t *input = NULL;
    for (uint8_t i = 0; i < 12; i++) {
        if (!track_length_input[i].active) {
            continue;
        }
        active_track = i;
        input        = &track_length_input[i];
        break;
    }

    if (!input) {
        return false;
    }

    uint16_t display_value;
    if (input->digits > 0) {
        display_value = input->value;
    } else {
        display_value = input->step_mode ? seq_track_length_steps(active_track)
                                         : seq_track_length_bars(active_track);
    }
    if (!seq_render_digits_common(display_value, led_min, led_max, now, input->step_mode)) {
        return false;
    }

    uint8_t track_led = track_leds[active_track];
    if (track_led != NO_LED && track_led >= led_min && track_led < led_max) {
        RGB base = input->step_mode ? (RGB){ 120, 240, 140 } : (RGB){ 80, 150, 255 };
        rgb_matrix_set_color(track_led, base.r, base.g, base.b);
    }

    return true;
#else
    (void)led_min;
    (void)led_max;
    (void)now;
    return false;
#endif
}

static inline void seq_bpm_preview_arm(uint32_t now) {
    bpm_preview_active  = true;
    bpm_preview_expires = now + 1200U;
}

static inline void seq_track_velocity_preview(uint8_t track, bool is_melodic, uint16_t value, uint32_t now) {
    velocity_preview_active     = true;
    velocity_preview_track      = track % 12;
    velocity_preview_is_melodic = is_melodic;
    velocity_preview_value      = value;
    velocity_preview_expires    = now + 800U;
}

static bool seq_track_velocity_render_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
    if (step_edit.active) {
        return false;
    }
    if (!velocity_preview_active) {
        return false;
    }
    if ((int32_t)TIMER_DIFF_32(now, velocity_preview_expires) >= 0) {
        velocity_preview_active = false;
        return false;
    }

    if (!seq_render_digits_common(velocity_preview_value, led_min, led_max, now, true)) {
        return false;
    }

    uint8_t led = track_leds[velocity_preview_track];
    if (led != NO_LED && led >= led_min && led < led_max) {
        RGB color = velocity_preview_is_melodic ? (RGB){ 120, 240, 140 } : (RGB){ 255, 200, 70 };
        rgb_matrix_set_color(led, color.r, color.g, color.b);
    }
    return true;
#else
    (void)led_min;
    (void)led_max;
    (void)now;
    return false;
#endif
}

static bool seq_bpm_render_preview_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
    if (step_edit.active) {
        return false;
    }
    if (!bpm_preview_active) {
        return false;
    }
    if ((int32_t)TIMER_DIFF_32(now, bpm_preview_expires) >= 0) {
        bpm_preview_active = false;
        return false;
    }
    seq_bpm_render_digits(led_min, led_max, now);
    return true;
#else
    (void)led_min;
    (void)led_max;
    (void)now;
    return false;
#endif
}

static bool seq_euclid_render_value_digits(uint8_t led_min, uint8_t led_max, uint32_t now) {
#if defined(RGB_MATRIX_ENABLE) && SEQ_FEATURE_RGB
    if (step_edit.active) {
        return false;
    }
    seq_euclid_param_kind_t kind  = EUCLID_PARAM_KIND_NONE;
    uint16_t                value = 0;
    bool                    show  = false;

    if (euclid_param_mode != EUCLID_PARAM_MODE_NONE) {
        kind  = seq_euclid_param_kind_for_mode(euclid_param_mode);
        value = seq_euclid_param_value_for_kind(seq_euclid_active_track(), kind);
        seq_euclid_capture_preview(seq_euclid_active_track(), kind, value, now);
        show = true;
    } else if (euclid_param_preview_kind != EUCLID_PARAM_KIND_NONE) {
        if ((int32_t)TIMER_DIFF_32(now, euclid_param_preview_expires) >= 0) {
            seq_euclid_clear_preview();
        } else {
            kind  = euclid_param_preview_kind;
            value = euclid_param_preview_value;
            show  = true;
        }
    }

    if (!show) {
        return false;
    }

    return seq_render_digits_common(value, led_min, led_max, now, false);
#else
    (void)led_min;
    (void)led_max;
    (void)now;
    return false;
#endif
}

bool seq_rgb_matrix_indicators(uint8_t led_min, uint8_t led_max) {
#ifdef RGB_MATRIX_ENABLE
    static bool previous_melodic_view;
    if (previous_melodic_view != slot_uses_melodic(view_slot)) {
        previous_melodic_view = slot_uses_melodic(view_slot);
        leds_discovered       = false;
    }
    discover_seq_leds();

    if (!seq_state.active || !leds_discovered) {
        return false;
    }

    uint32_t now = timer_read32();

    if (seq_loop_steps) {
        seq_render_digits_common(seq_loop_steps, led_min, led_max, now, true);
        seq_rgb_set_key(KC_ENT, (RGB){255, 90, 20}, led_min, led_max);
        return true;
    }

    if (seq_palette_edit_digit != 255) {
        for (uint8_t led = led_min; led < led_max; led++) rgb_matrix_set_color(led, 0, 0, 0);
        uint8_t slot = seq_palette_edit_digit ? seq_palette_edit_digit - 1 : 0;
        uint8_t notes[8], count = seq_palette_chord_notes(view_slot, slot, notes);
        uint8_t type = seq_modules[view_slot].config.euclid.qualities[slot];
        uint8_t resolved = seq_palette_resolved_type(notes, count);
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
            bool tone = false;
            for (uint8_t n = 0; n < count; n++) tone |= notes[n] % 12 == key.semitone;
            seq_rgb_set_key(key.keycode, count && notes[0] % 12 == key.semitone ? (RGB){255,70,20} : tone ? (RGB){45,110,70} : (RGB){8,8,8}, led_min, led_max);
        }
        for (uint8_t i = 0; i < sizeof(seq_palette_types); i++) {
            bool available = i < 11 || seq_chord_scale_enabled(view_slot);
            bool selected = seq_palette_types[i] == type || ((type == 10 || type == 11) && i == resolved);
            seq_rgb_set_key(seq_palette_type_keys[i], selected ? (RGB){170,70,255} : available ? (RGB){25,20,35} : (RGB){0,0,0}, led_min, led_max);
        }
        return true;
    }
    if (seq_scale_editing) {
        for (uint8_t led = led_min; led < led_max; led++) rgb_matrix_set_color(led, 0, 0, 0);
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
            RGB color = key.semitone == seq_scale_pending.root ? (RGB){255,70,20} : seq_scale_contains(key.semitone,seq_scale_pending.root,seq_scale_pending.kind) ? (RGB){45,110,70} : (RGB){8,8,8};
            seq_rgb_set_key(key.keycode, color, led_min, led_max);
        }
        for (uint8_t i = 0; i < SEQ_SCALE_COUNT; i++) seq_rgb_set_key(seq_scale_mode_keys[i], i == seq_scale_pending.kind ? (RGB){170,70,255} : (RGB){25,20,35}, led_min, led_max);
        seq_rgb_set_key(KC_ENT, (RGB){0,100,30}, led_min, led_max);
        seq_rgb_set_key(KC_ESC, (RGB){120,20,0}, led_min, led_max);
        return true;
    }
    if(seq_chord_mode[view_slot] && !slot_viewing_child(view_slot) && !cc_context.active && !cc_mode.held) {
        const seq_chord_event_t *d=&seq_chord_draft[view_slot];uint8_t pitches[8],count=seq_chord_render(d,pitches);
        for(uint8_t i=0;i<SEQ_MELODIC_KEY_COUNT;i++) {
            seq_melodic_key_t key=SEQ_MELODIC_KEY(i);bool tone=false;for(uint8_t n=0;n<count;n++)if(pitches[n]%12==key.semitone)tone=true;
            seq_rgb_set_key(key.keycode,key.semitone==d->root%12?(RGB){255,85,20}:tone?(RGB){70,150,100}:(RGB){8,8,8},led_min,led_max);
        }
        for (uint8_t i = 0; i < 16; i++) {
            uint16_t step = view_bar * 16u + i;
            int16_t c = seq_chord_find(view_slot, step);
            RGB color = c < 0 ? (RGB){8, 8, 8} : seq_chord_events[c].start == step ? (RGB){100, 140, 60} : (RGB){25, 40, 20};
            if (c >= 0 && seq_chord_events[c].start == seq_chord_selected) color = (RGB){110, 55, 170};
            if (seq_state.running && seq_track_step_position[view_slot] == step) color = (RGB){255, 85, 20};
            seq_rgb_set_key(SEQ_STEP_KEYS_DEFAULT[i], color, led_min, led_max);
        }
        const uint16_t params[]={KC_O,KC_RBRC,KC_I,KC_LBRC,KC_P};for(uint8_t i=0;i<5;i++)seq_rgb_set_key(params[i],seq_chord_parameter==i?(RGB){255,130,30}:(RGB){30,25,20},led_min,led_max);
        return true;
    }
    bool meter_rendered = seq_step_edit_render_meter(led_min, led_max);

    if (step_edit.active) {
        bool is_drum        = step_edit.is_drum;
        RGB  velocity_color = seq_step_edit_param_color(is_drum, STEP_EDIT_PARAM_VELOCITY);
        RGB  ratchet_color  = seq_step_edit_param_color(true, STEP_EDIT_PARAM_RATCHET);
        RGB  spacing_color  = seq_step_edit_param_color(true, STEP_EDIT_PARAM_RATCHET_SPACING);
        RGB  length_color   = seq_step_edit_param_color(false, STEP_EDIT_PARAM_LENGTH);
        RGB  pitch_color    = seq_step_edit_param_color(false, STEP_EDIT_PARAM_PITCH);

        RGB dim_velocity = (RGB){ velocity_color.r / 6u, velocity_color.g / 6u, velocity_color.b / 6u };
        RGB dim_ratchet  = (RGB){ ratchet_color.r / 6u, ratchet_color.g / 6u, ratchet_color.b / 6u };
        RGB dim_spacing  = (RGB){ spacing_color.r / 6u, spacing_color.g / 6u, spacing_color.b / 6u };
        RGB dim_length   = (RGB){ length_color.r / 6u, length_color.g / 6u, length_color.b / 6u };
        RGB dim_pitch    = (RGB){ pitch_color.r / 6u, pitch_color.g / 6u, pitch_color.b / 6u };

        if (is_drum) {
            RGB vel = (step_edit.param == STEP_EDIT_PARAM_VELOCITY) ? velocity_color : dim_velocity;
            RGB rat = (step_edit.param == STEP_EDIT_PARAM_RATCHET) ? ratchet_color : dim_ratchet;
            RGB spa = (step_edit.param == STEP_EDIT_PARAM_RATCHET_SPACING) ? spacing_color : dim_spacing;

            seq_rgb_set_key(KC_I, vel, led_min, led_max);
            seq_rgb_set_key(KC_O, rat, led_min, led_max);
            seq_rgb_set_key(KC_P, spa, led_min, led_max);
            seq_rgb_set_key(KC_1, vel, led_min, led_max);
            seq_rgb_set_key(KC_2, rat, led_min, led_max);
            seq_rgb_set_key(KC_3, spa, led_min, led_max);
        } else {
            RGB len = (step_edit.param == STEP_EDIT_PARAM_LENGTH) ? length_color : dim_length;
            RGB pit = (step_edit.param == STEP_EDIT_PARAM_PITCH) ? pitch_color : dim_pitch;
            RGB vel = (step_edit.param == STEP_EDIT_PARAM_VELOCITY) ? velocity_color : dim_velocity;

            seq_rgb_set_key(KC_I, len, led_min, led_max);
            seq_rgb_set_key(KC_O, pit, led_min, led_max);
            seq_rgb_set_key(KC_P, vel, led_min, led_max);
            seq_rgb_set_key(KC_LBRC, pitch_color, led_min, led_max);
            seq_rgb_set_key(KC_RBRC, pitch_color, led_min, led_max);
            seq_rgb_set_key(KC_SCLN, velocity_color, led_min, led_max);
            seq_rgb_set_key(KC_QUOT, velocity_color, led_min, led_max);
            seq_rgb_set_key(KC_SLASH, velocity_color, led_min, led_max);
        }
    } else {
        if (meter_rendered) {
            seq_clear_number_leds(led_min, led_max);
        }
        RGB off = {0, 0, 0};
        seq_rgb_set_key(KC_I, off, led_min, led_max);
        seq_rgb_set_key(KC_O, off, led_min, led_max);
        seq_rgb_set_key(KC_P, off, led_min, led_max);
        seq_rgb_set_key(KC_1, off, led_min, led_max);
        seq_rgb_set_key(KC_2, off, led_min, led_max);
        seq_rgb_set_key(KC_3, off, led_min, led_max);
        seq_rgb_set_key(KC_LBRC, off, led_min, led_max);
        seq_rgb_set_key(KC_RBRC, off, led_min, led_max);
        seq_rgb_set_key(KC_SCLN, off, led_min, led_max);
        seq_rgb_set_key(KC_QUOT, off, led_min, led_max);
        seq_rgb_set_key(KC_SLASH, off, led_min, led_max);
    }

    if (record_led != NO_LED && record_led >= led_min && record_led < led_max) {
        if (record_state.armed) {
            bool solid   = seq_state.running;
            bool blink_on = ((now / 200U) & 1u) == 0;
            if (solid || blink_on) {
                rgb_matrix_set_color(record_led, 255, 40, 40);
            } else {
                rgb_matrix_set_color(record_led, 0, 0, 0);
            }
        } else {
            rgb_matrix_set_color(record_led, 0, 0, 0);
        }
    }

    uint32_t beat_period = beat_period_ms();
    if (beat_period < 200U) beat_period = 200U;
    bool running = seq_state.running;

    bool     view_is_melodic  = slot_uses_melodic(view_slot);
    uint8_t  active_channel   = slot_drum_track(view_slot);
    uint8_t  view_melodic_track = slot_melodic_track(view_slot);
    uint16_t active_note_mask = view_is_melodic ? 0 : (note_hold_mask ? note_hold_mask : (uint16_t)(1u << drum_active_note[active_channel]));
    uint16_t melodic_highlight_mask = 0;
    uint16_t euclid_palette_mask    = 0;
    bool     euclid_palette_active  = false;
    uint16_t pitch_focus_mask       = 0;
    if (!view_is_melodic) {
        melodic_highlight_mask = melodic_key_mask[view_melodic_track];
        if (!melodic_highlight_mask) {
            melodic_highlight_mask = melodic_last_mask[view_melodic_track];
        }
    }
    uint32_t drum_period = beat_period * 2;
    if (drum_period < 200U) drum_period = 200U;
    bool euclid_any = seq_euclid_any_enabled();

    if (view_is_melodic) {
        uint8_t track_index = view_melodic_track % MEL_TRACK_COUNT;
        uint16_t key_mask   = melodic_key_mask[track_index];
        uint8_t base_semitone = seq_first_semitone_from_mask(key_mask, melodic_last_semitone[track_index]);

        if (cmd_state.held && (cmd_state.edit_track % 12u) == view_slot) {
            euclid_palette_active = true;
            if (euclid_palette_capture.active && euclid_palette_capture.primed
                && (euclid_palette_capture.track % 12u) == view_slot && euclid_palette_capture.count) {
                int8_t origin = euclid_palette_capture.origin;
                for (uint8_t i = 0; i < euclid_palette_capture.count; i++) {
                    int16_t semitone = (int16_t)origin + (int16_t)euclid_palette_capture.values[i];
                    while (semitone < 0) {
                        semitone += SEQ_MELODIC_KEY_COUNT;
                    }
                    semitone %= SEQ_MELODIC_KEY_COUNT;
                    euclid_palette_mask |= (uint16_t)(1u << semitone);
                }
            } else {
                seq_euclid_track_t *euclid = &euclid_tracks[view_slot % 12];
                if (euclid->note_count) {
                    for (uint8_t i = 0; i < euclid->note_count; i++) {
                        int16_t semitone = (int16_t)base_semitone + (int16_t)euclid->note_pool[i];
                        while (semitone < 0) {
                            semitone += SEQ_MELODIC_KEY_COUNT;
                        }
                        semitone %= SEQ_MELODIC_KEY_COUNT;
                        euclid_palette_mask |= (uint16_t)(1u << semitone);
                    }
                }
            }
        }

        if (step_edit.active && !step_edit.is_drum
            && (step_edit.track % MEL_TRACK_COUNT) == view_melodic_track
            && step_edit.param == STEP_EDIT_PARAM_PITCH) {
            uint8_t semitone = seq_pitch_to_semitone(step_edit.pitch);
            pitch_focus_mask = (uint16_t)(1u << (semitone % SEQ_MELODIC_KEY_COUNT));
        }
    }

    for (uint8_t i = 0; i < 12; i++) {
        uint8_t led = track_leds[i];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }

        bool has_steps      = slot_has_activity(i);
        bool is_view        = (i == view_slot);
        bool note_selected  = (!view_is_melodic) && ((melodic_highlight_mask & (1u << (i % SEQ_DRUM_NOTE_COUNT))) != 0);
        bool viewing_child  = slot_viewing_child(i);
        bool slot_melodic   = slot_is_melodic(i);
        bool uses_melodic   = slot_uses_melodic(i);
        bool start_flash    = (mel_last_start_mask & (1u << i)) != 0;
        bool stop_flash     = (mel_last_stop_mask & (1u << i)) != 0;
        const seq_euclid_track_t *e = seq_euclid_selected(i, false);
        bool euclid_active = e->enabled && e->fills > 0;
        bool euclid_focus  = euclid_edit.active && euclid_edit.track == i;
        bool chord_capture_track = chord_capture.active && chord_capture.slot == i;

        if (euclid_active) {
            has_steps = true;
        }

        if (is_view) {
            if (uses_melodic) {
                uint8_t v = triangle_wave(now, beat_period, 60, 255);
                RGB c     = track_rgb(i, v);
                seq_set_track_color(i, led, c, euclid_focus || is_view);
            } else {
                uint8_t level = has_steps ? 255 : 80;
                RGB c         = track_rgb(i, level);
                seq_set_track_color(i, led, c, euclid_focus || is_view);
            }
            if (chord_capture_track) {
                RGB highlight = { 255, 150, 255 };
                seq_set_track_color(i, led, highlight, true);
            }
            continue;
        }

        if (chord_capture_track) {
            RGB highlight = { 255, 150, 255 };
            seq_set_track_color(i, led, highlight, true);
            continue;
        }

        if (note_selected) {
            uint8_t level = has_steps ? 255 : 80;
            RGB c         = track_rgb(i, level);
            seq_set_track_color(i, led, c, euclid_focus);
            continue;
        }

        if (slot_melodic && !viewing_child) {
            if (start_flash) {
                seq_set_track_color(i, led, (RGB){ 255, 255, 255 }, euclid_focus);
                continue;
            }
            if (stop_flash) {
                RGB c = track_rgb(i, 90);
                seq_set_track_color(i, led, c, euclid_focus);
                continue;
            }
            uint8_t v = triangle_wave(now, beat_period, 30, 200);
            RGB c     = track_rgb(i, v);
            seq_set_track_color(i, led, c, euclid_focus);
            continue;
        }

        if (!view_is_melodic && !slot_melodic) {
            if (has_steps) {
                RGB c = track_rgb(i, 255);
                seq_set_track_color(i, led, c, euclid_focus);
            } else {
                seq_set_track_color(i, led, (RGB){ 0, 0, 0 }, euclid_focus);
            }
            continue;
        }

        if (slot_melodic && viewing_child && has_steps) {
            RGB c = track_rgb(i, 200);
            seq_set_track_color(i, led, c, euclid_focus);
            continue;
        }

        if (!slot_melodic) {
            if (has_steps) {
                uint8_t v = triangle_wave(now, drum_period, 0, 200);
                RGB c     = track_rgb(i, v);
                seq_set_track_color(i, led, c, euclid_focus);
            } else {
                seq_set_track_color(i, led, (RGB){ 0, 0, 0 }, euclid_focus);
            }
            continue;
        }

        seq_set_track_color(i, led, (RGB){ 12, 12, 12 }, euclid_focus);
    }

    uint16_t view_value = seq_state.active ? (uint16_t)(view_bar + 1u) : 0u;
    uint16_t play_value = seq_state.active ? (uint16_t)(playhead_bar + 1u) : 0u;
    bool     show_play  = seq_state.active && (!follow_mode || playhead_bar != view_bar);

    for (uint8_t i = 0; i < SEQ_BAR_LED_COUNT; i++) {
        uint8_t led = bar_leds[i];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }

        uint16_t bit = (uint16_t)(1u << i);
        bool     view_bit = (view_value & bit) != 0;
        bool     play_bit = show_play && (play_value & bit) != 0;

        RGB color = { 0, 0, 0 };
        if (view_bit && play_bit) {
            color = (RGB){ 255, 255, 255 };
        } else if (view_bit) {
            color = follow_mode ? (RGB){ 40, 150, 255 } : (RGB){ 60, 200, 255 };
        } else if (play_bit) {
            color = (RGB){ 255, 90, 40 };
        }

        rgb_matrix_set_color(led, color.r, color.g, color.b);
    }

    bool digits_displayed = seq_bpm_render_preview_digits(led_min, led_max, now);
    if (!digits_displayed) {
        bool track_length_displayed = seq_track_length_render_digits(led_min, led_max, now);
        if (!track_length_displayed) {
            bool velocity_displayed = seq_track_velocity_render_digits(led_min, led_max, now);
            if (!velocity_displayed) {
                bool euclid_displayed = seq_euclid_render_value_digits(led_min, led_max, now);
                if (!euclid_displayed) {
                    seq_bpm_render_digits(led_min, led_max, now);
                }
            }
        }
    }

    if (slot_uses_melodic(view_slot)) {
        uint8_t melodic_track   = view_melodic_track;
        uint16_t key_mask       = melodic_key_mask[melodic_track];
        uint16_t last_mask      = melodic_last_mask[melodic_track];
        bool     rest_pending   = melodic_rest_pending[melodic_track];
        bool     tie_pending    = melodic_tie_pending[melodic_track];

        uint16_t highlight_mask = key_mask ? key_mask : last_mask;
        bool highlight_override = false;
        if (step_edit.active && !step_edit.is_drum
            && (step_edit.track % MEL_TRACK_COUNT) == view_melodic_track) {
            uint16_t length = seq_track_length_steps(view_slot);
            if (!length) {
                length = SEQ_STEPS_PER_BAR ? SEQ_STEPS_PER_BAR : 1;
            }
            uint16_t local = step_edit.local_step % length;
            highlight_mask = seq_melodic_step_event_mask(view_melodic_track, local);
            highlight_override = true;
        }
        if (!highlight_mask && !highlight_override) {
            highlight_mask = (uint16_t)(1u << melodic_last_semitone[melodic_track]);
        }

        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            uint8_t led = melodic_key_leds[i];
            if (led == NO_LED || led < led_min || led >= led_max) {
                continue;
            }

            seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
            uint16_t bit = (uint16_t)(1u << (uint8_t)key.semitone);
            bool chord_active = (highlight_mask & bit) != 0;
            bool euclid_active = euclid_palette_active && (euclid_palette_mask & bit) != 0;
            bool pitch_focus = (pitch_focus_mask & bit) != 0;
            RGB  c = key.is_sharp ? (RGB){ 20, 40, 150 } : (RGB){ 220, 210, 110 };
            if (euclid_active && !chord_active) {
                c = (RGB){ 90, 220, 255 };
            }
            if (chord_active) {
                c = key.is_sharp ? (RGB){ 60, 110, 255 } : (RGB){ 255, 245, 150 };
                if (euclid_active) {
                    c.r = seq_u8_sat_add(c.r, 40);
                    c.g = seq_u8_sat_add(c.g, 40);
                    c.b = seq_u8_sat_add(c.b, 40);
                }
            }
            if (pitch_focus) {
                RGB pitch_color = seq_step_edit_param_color(false, STEP_EDIT_PARAM_PITCH);
                c = pitch_color;
            }
            rgb_matrix_set_color(led, c.r, c.g, c.b);
        }

        for (uint8_t i = 0; i < SEQ_MELODIC_CONTROL_COUNT; i++) {
            uint8_t led = melodic_control_leds[i];
            if (led == NO_LED || led < led_min || led >= led_max) {
                continue;
            }

            RGB c = { 100, 100, 100 };
            int8_t shift = melodic_octave_shift[melodic_track];
            uint32_t base_period = beat_period;
            if (base_period < 200U) base_period = 200U;

            if (i == 0) { /* octave down */
                RGB base_color = (RGB){ 80, 180, 200 };
                if (shift < 0) {
                    uint8_t magnitude = (uint8_t)(-shift);
                    uint32_t period = base_period;
                    for (uint8_t step = 1; step < magnitude; step++) {
                        period = (period > 1) ? (period / 2u) : 1u;
                    }
                    uint8_t wave = triangle_wave(now, period, 20, 255);
                    c.r = (uint8_t)((base_color.r * wave) / 255u);
                    c.g = (uint8_t)((base_color.g * wave) / 255u);
                    c.b = (uint8_t)((base_color.b * wave) / 255u);
                } else {
                    c = (shift == 0) ? (RGB){ base_color.r / 3u, base_color.g / 3u, base_color.b / 3u }
                                     : base_color;
                }
            } else if (i == 2) { /* octave up */
                RGB base_color = (RGB){ 100, 220, 160 };
                if (shift > 0) {
                    uint8_t magnitude = (uint8_t)shift;
                    uint32_t period = base_period;
                    for (uint8_t step = 1; step < magnitude; step++) {
                        period = (period > 1) ? (period / 2u) : 1u;
                    }
                    uint8_t wave = triangle_wave(now, period, 20, 255);
                    c.r = (uint8_t)((base_color.r * wave) / 255u);
                    c.g = (uint8_t)((base_color.g * wave) / 255u);
                    c.b = (uint8_t)((base_color.b * wave) / 255u);
                } else {
                    c = (shift == 0) ? (RGB){ base_color.r / 3u, base_color.g / 3u, base_color.b / 3u }
                                     : base_color;
                }
            }

            if (i == 1) { /* rest/tie */
                if (rest_pending) {
                    c = (RGB){ 220, 220, 220 };
                } else if (tie_pending) {
                    c = (RGB){ 160, 200, 255 };
                } else {
                    c = (RGB){ 120, 120, 120 };
                }
            }

            rgb_matrix_set_color(led, c.r, c.g, c.b);
        }
    }

    mel_last_start_mask = 0;
    mel_last_stop_mask  = 0;

    for (uint8_t i = 0; i < 16; i++) {
        uint8_t led = step_leds[i];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }
        if (view_is_melodic) {
            rgb_matrix_set_color(led, 25, 25, 25);
        } else {
            rgb_matrix_set_color(led, 25, 25, 25);
        }
    }

    uint8_t  primary_note  = view_is_melodic ? 0 : first_note_from_mask(active_note_mask, drum_active_note[active_channel]);
    uint16_t overlay_mask  = view_is_melodic ? 0 : (active_note_mask & (uint16_t)~(1u << primary_note));

    if (overlay_mask && !view_is_melodic) {
        for (uint8_t n = 0; n < SEQ_DRUM_NOTE_COUNT; n++) {
            if (!(overlay_mask & (1u << n))) {
                continue;
            }
            uint16_t step_mask = seq_drum_window_mask(active_channel, n, view_bar);
            if (!step_mask) {
                continue;
            }
            RGB hr = track_rgb(view_slot, 120);
            for (uint8_t s = 0; s < 16; s++) {
                if (!(step_mask & (1u << s))) {
                    continue;
                }
                uint8_t led = step_leds[s];
                if (led == NO_LED || led < led_min || led >= led_max) {
                    continue;
                }
                rgb_matrix_set_color(led, hr.r, hr.g, hr.b);
            }
        }
    }

    uint16_t primary_steps = view_is_melodic ? seq_slot_playback_mask(view_slot, view_bar)
                                             : seq_drum_window_mask(active_channel, primary_note, view_bar);
    uint16_t euclid_steps  = 0;
    uint8_t  euclid_track  = 0;
    bool     euclid_active = false;
    bool     euclid_focus  = false;

    if (view_is_melodic) {
        euclid_track = slot_melodic_track(view_slot);
        seq_euclid_track_t *state = seq_euclid_selected(euclid_track, false);
        if (seq_modules[euclid_track].kind == SEQ_MODULE_EUCLID && state->enabled && state->fills > 0) {
            euclid_active = true;
            euclid_steps  = seq_melodic_euclid_window_mask(euclid_track, view_bar);
            euclid_focus  = euclid_edit.active && euclid_edit.track == (euclid_track % 12);
        }
    }

    RGB base_track_color    = track_rgb(view_slot, 180);
    RGB primary_step_color  = base_track_color;
    RGB euclid_only_color   = track_rgb(view_slot, 110);

    if (view_is_melodic && euclid_active) {
        primary_step_color = seq_euclid_tint(euclid_track, base_track_color, euclid_focus);
        euclid_only_color  = seq_euclid_tint(euclid_track, euclid_only_color, euclid_focus);
    }

    for (uint8_t i = 0; i < 16; i++) {
        bool has_primary = (primary_steps & (1u << i)) != 0;
        bool has_euclid  = view_is_melodic && ((euclid_steps & (1u << i)) != 0);
        if (!has_primary && !has_euclid) {
            continue;
        }

        uint8_t led = step_leds[i];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }

        RGB color = has_primary ? primary_step_color : euclid_only_color;
        if (has_primary && has_euclid && view_is_melodic && euclid_active) {
            color.r = seq_u8_sat_add(color.r, 25);
            color.g = seq_u8_sat_add(color.g, 25);
            color.b = seq_u8_sat_add(color.b, 25);
        } else if (has_euclid && view_is_melodic && euclid_active && !has_primary) {
            /* brighten Euclid-only hits slightly for visibility */
            color.r = seq_u8_sat_add(color.r, 15);
            color.g = seq_u8_sat_add(color.g, 15);
            color.b = seq_u8_sat_add(color.b, 15);
        }

        rgb_matrix_set_color(led, color.r, color.g, color.b);
    }

    if (chord_capture.active && slot_uses_melodic(view_slot) && chord_capture.slot == (view_slot % 12)) {
        uint16_t capture_bar = chord_capture.start_absolute / SEQ_STEPS_PER_BAR;
        if (capture_bar == view_bar) {
            uint16_t capture_step = chord_capture.start_absolute % SEQ_STEPS_PER_BAR;
            uint8_t led = step_leds[capture_step];
            if (led != NO_LED && led >= led_min && led < led_max) {
                RGB highlight = { 255, 150, 255 };
                rgb_matrix_set_color(led, highlight.r, highlight.g, highlight.b);
            }
        }
    }

    uint16_t view_length = seq_track_length_steps(view_slot);
    if (!view_length) {
        view_length = SEQ_STEPS_PER_BAR;
    }
    uint32_t local_play_index = seq_track_step_position[view_slot % 12];
    if (view_length) {
        local_play_index %= view_length;
    }
    uint8_t play_led_index = (view_length < SEQ_STEPS_PER_BAR) ? (uint8_t)local_play_index : step_idx;
    uint8_t play_led = step_leds[play_led_index % SEQ_STEPS_PER_BAR];
    if (play_led != NO_LED && play_led >= led_min && play_led < led_max) {
        if (running) {
            RGB playhead = hsv_to_rgb((HSV){ 85, 255, 255 });
            rgb_matrix_set_color(play_led, playhead.r, playhead.g, playhead.b);
        } else {
            uint32_t blink_period = beat_period * 4U;
            if (blink_period < 400U) blink_period = 400U;
            bool blink_on = ((now / blink_period) & 1u) == 0;
            if (blink_on) {
                RGB playhead = hsv_to_rgb((HSV){ 85, 255, 255 });
                rgb_matrix_set_color(play_led, playhead.r, playhead.g, playhead.b);
            }
        }
    }

    if (step_edit.active) {
        uint8_t led = step_leds[step_edit.absolute_step % SEQ_STEPS_PER_BAR];
        if (led != NO_LED && led >= led_min && led < led_max) {
            RGB c = (RGB){ 255, 160, 40 };
            rgb_matrix_set_color(led, c.r, c.g, c.b);
        }
    }

    if (seq_step_hold.editing && step_edit.active && !step_edit.is_drum) {
        for (uint8_t i = 0; i < SEQ_MELODIC_KEY_COUNT; i++) {
            seq_melodic_key_t key = SEQ_MELODIC_KEY(i);
            seq_rgb_set_key(key.keycode, key.semitone == step_edit.pitch % 12 ? (RGB){255,100,25} : (RGB){8,8,8}, led_min, led_max);
        }
    }


    if (home_led != NO_LED && home_led >= led_min && home_led < led_max) {
        if (switch_state.held) {
            if (seq_external_clock) rgb_matrix_set_color(home_led, 0, 180, 255);
            else rgb_matrix_set_color(home_led, 255, 140, 0);
        } else if (running) {
            uint32_t period = beat_period;
            uint8_t low  = 40;
            uint8_t high = 255;
            uint8_t v    = triangle_wave(now, period, low, high);
            rgb_matrix_set_color(home_led, v, v, v);
        } else {
            uint32_t blink_period = beat_period * 4U;
            if (blink_period < 400U) blink_period = 400U;
            bool blink_on = ((now / blink_period) & 1u) == 0;
            uint8_t v = blink_on ? 200 : 40;
            rgb_matrix_set_color(home_led, v, v, v);
        }
    }

    bool cc_active = cc_context.active || cc_mode.held;
    if (cc_active) {
        uint8_t cc_track = seq_cc_current_track();
        if (cc_track < 12) {
            uint8_t led = track_leds[cc_track];
            if (led != NO_LED && led >= led_min && led < led_max) {
                RGB c = cc_session.active && cc_session.temporary ? (RGB){ 40, 200, 180 }
                                                                  : (RGB){ 180, 60, 220 };
                rgb_matrix_set_color(led, c.r, c.g, c.b);
            }
        }

        if (cc_context.hold_override_active) {
            uint8_t led = track_leds[cc_context.hold_track % 12];
            if (led != NO_LED && led >= led_min && led < led_max) {
                rgb_matrix_set_color(led, 255, 140, 40);
            }
        }

        for (uint8_t i = 0; i < 2; i++) {
            uint8_t led = cmd_leds[i];
            if (led == NO_LED || led < led_min || led >= led_max) {
                continue;
            }
            RGB c = cc_context.active ? (RGB){ 110, 40, 220 } : (RGB){ 60, 120, 220 };
            if (cc_mode.latched_temporary || cc_mode.temporary) {
                c = (RGB){ 40, 200, 180 };
            }
            rgb_matrix_set_color(led, c.r, c.g, c.b);
        }

        if (cc_session.active) {
            seq_cc_mapping_t mapping;
            seq_cc_load_mapping(cc_session.mapping_index, &mapping);
            uint16_t cc_keycode = mapping.keycode;
            uint8_t  led        = led_index_for_keycode(cc_keycode);
            if (led != NO_LED && led >= led_min && led < led_max) {
                RGB c = cc_session.temporary ? (RGB){ 40, 220, 160 } : (RGB){ 220, 120, 40 };
                rgb_matrix_set_color(led, c.r, c.g, c.b);
            }
        }

        if (backslash_led != NO_LED && backslash_led >= led_min && backslash_led < led_max) {
            RGB c = cc_context.active ? (RGB){ 110, 40, 220 }
                                      : (cc_mode.held ? (RGB){ 60, 120, 220 } : (RGB){ 90, 60, 180 });
            rgb_matrix_set_color(backslash_led, c.r, c.g, c.b);
        }
    }

    bool option_led_present = false;
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t led = option_leds[i];
        if (led == NO_LED || led < led_min || led >= led_max) {
            continue;
        }
        option_led_present = true;
        RGB color = euclid_mode_active ? (RGB){ 255, 200, 70 }
                                       : (euclid_any ? (RGB){ 60, 160, 220 } : (RGB){ 18, 32, 42 });
        if (cmd_state.held) {
            color = (RGB){ 90, 220, 255 };
        }
        if (!euclid_mode_active && !cmd_state.held && !euclid_any) {
            color = (RGB){ 10, 20, 28 };
        }
        rgb_matrix_set_color(led, color.r, color.g, color.b);
    }

    if (!option_led_present) {
        uint8_t track_led = track_leds[seq_euclid_active_track() % 12];
        if (track_led != NO_LED && track_led >= led_min && track_led < led_max) {
            RGB color = euclid_mode_active ? (RGB){ 255, 200, 70 }
                                           : (euclid_any ? (RGB){ 60, 160, 220 } : (RGB){ 18, 32, 42 });
            if (cmd_state.held) {
                color = (RGB){ 90, 220, 255 };
            }
            rgb_matrix_set_color(track_led, color.r, color.g, color.b);
        }
    }

    if (!cc_active) {
        for (uint8_t i = 0; i < 2; i++) {
            uint8_t led = cmd_leds[i];
            if (led == NO_LED || led < led_min || led >= led_max) {
                continue;
            }
            rgb_matrix_set_color(led, 12, 24, 32);
        }
        if (backslash_led != NO_LED && backslash_led >= led_min && backslash_led < led_max) {
            rgb_matrix_set_color(backslash_led, 0, 0, 0);
        }
    }

    if (track_confirm.active) {
        if (confirm_led_yes != NO_LED && confirm_led_yes >= led_min && confirm_led_yes < led_max) {
            rgb_matrix_set_color(confirm_led_yes, 0, 200, 0);
        }
        if (confirm_led_no != NO_LED && confirm_led_no >= led_min && confirm_led_no < led_max) {
            rgb_matrix_set_color(confirm_led_no, 200, 0, 0);
        }
    }

    if (equal_led != NO_LED && equal_led >= led_min && equal_led < led_max) {
        if (follow_mode) {
            uint8_t v = triangle_wave(now, 360U, 40, 255);
            rgb_matrix_set_color(equal_led, 40, v, 255);
        } else if (seq_state.active) {
            uint8_t v = triangle_wave(now, 480U, 0, 255);
            rgb_matrix_set_color(equal_led, v, (uint8_t)(v / 4), (uint8_t)(v / 4));
        } else {
            rgb_matrix_set_color(equal_led, 0, 0, 0);
        }
    }

    if (grave_led != NO_LED && grave_led >= led_min && grave_led < led_max) {
        if (euclid_mode_active) {
            RGB c = (RGB){ 255, 200, 70 };
            rgb_matrix_set_color(grave_led, c.r, c.g, c.b);
        } else if (slot_uses_melodic(view_slot)) {
            uint8_t v = triangle_wave(now, beat_period, 30, 200);
            RGB c     = track_rgb(view_slot, v);
            rgb_matrix_set_color(grave_led, c.r, c.g, c.b);
        } else {
            rgb_matrix_set_color(grave_led, 0, 0, 0);
        }
    }

    if (esc_led != NO_LED && esc_led >= led_min && esc_led < led_max) {
        if (!seq_state.active) {
            rgb_matrix_set_color(esc_led, 0, 0, 0);
        } else if (seq_temp_active) {
            uint8_t v = seq_temp_restore_pending && ((now / 240u) & 1u) ? 0 : 255;
            rgb_matrix_set_color(esc_led, (uint16_t)v * 150u / 255u, 0, v);
        } else if (drum_pool_full_preview && TIMER_DIFF_32(now, drum_pool_full_ms) < 1200u) {
            uint8_t v = ((now / 120u) & 1u) ? 255 : 0;
            rgb_matrix_set_color(esc_led, v, 0, v);
        } else if (seq_save_status >= 2) {
            rgb_matrix_set_color(esc_led, 255, (now / 300u) & 1u ? 90 : 0, 0);
        } else if (!follow_mode && playhead_bar != view_bar) {
            uint8_t v = triangle_wave(now, 480U, 0, 255);
            rgb_matrix_set_color(esc_led, v, (uint8_t)(v / 6), (uint8_t)(v / 6));
        } else if (follow_mode) {
            rgb_matrix_set_color(esc_led, 20, 90, 20);
        } else {
            rgb_matrix_set_color(esc_led, 120, 60, 0);
        }
    }

    return false;
#else
    (void)led_min;
    (void)led_max;
    return false;
#endif
}
