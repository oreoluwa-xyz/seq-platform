#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sequencer_config.h"

#define SEQ_DRUM_STORAGE_BYTES ((SEQ_MAX_STEPS + 7) / 8)
#ifndef SEQ_DRUM_STEP_MOD_CAP
#    define SEQ_DRUM_STEP_MOD_CAP 16
#endif

#ifndef SEQ_DRUM_RATCHET_SPACING_STEPS
#    define SEQ_DRUM_RATCHET_SPACING_STEPS 16
#endif
#define SEQ_DRUM_RATCHET_SPACING_DEFAULT (SEQ_DRUM_RATCHET_SPACING_STEPS - 1)

#ifndef SEQ_EUCLID_NOTE_POOL_MAX
#    define SEQ_EUCLID_NOTE_POOL_MAX 8
#endif

typedef struct {
    bool    enabled;
    bool     template_active;
    uint16_t template_mask;
    uint8_t  palette_root;
    uint16_t steps;
    uint16_t fills;
    int16_t  rotate;
    uint8_t  note_count;
    int8_t   note_pool[SEQ_EUCLID_NOTE_POOL_MAX];
    uint8_t  swing_amount;
    uint8_t  probability;
    uint8_t  ratchet_threshold;
    uint8_t  palette_pin;
    uint8_t  pitch_mode;
    bool     palette_authored; /* False only for an untouched starter palette. */
} seq_euclid_track_t;

typedef struct __attribute__((packed)) {
    uint16_t step;   /* 0xFFFF => unused */
    uint8_t  config; /* bits 0-1 velocity, bits 2-3 ratchet */
    uint8_t  spacing;
} seq_drum_step_mod_t;

typedef struct {
    /* steps == 0 inherits the legacy track generator (old project compatibility). */
    seq_euclid_track_t euclid;
    uint8_t             data[SEQ_DRUM_STORAGE_BYTES];
    seq_drum_step_mod_t mods[SEQ_DRUM_STEP_MOD_CAP];
} seq_drum_lane_storage_t;

void                     seq_drum_pool_init(void);
uint8_t                  seq_drum_pool_used(void);
seq_drum_lane_storage_t *seq_drum_pool_lane(uint8_t track, uint8_t note);
bool                     seq_drum_pool_can_reserve(uint8_t track, uint16_t notes);
/* Reserve a whole multi-lane edit, or fail without changing any allocations. */
bool seq_drum_pool_reserve(uint8_t track, uint16_t notes);
bool seq_drum_pool_step_get(uint8_t track, uint8_t note, uint16_t step);
bool seq_drum_pool_step_set(uint8_t track, uint8_t note, uint16_t step, bool value);
void seq_drum_pool_trim(uint8_t track, uint8_t note, uint16_t from_step);
void seq_drum_pool_clear_track(uint8_t track);
void seq_drum_pool_prune(uint8_t track, uint8_t note);
void seq_drum_pool_reset_mods(void);
