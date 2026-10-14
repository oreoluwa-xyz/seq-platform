#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sequencer_config.h"

#define SEQ_MODULE_VERSION 1
#define SEQ_CHORD_SLOTS 12
#define SEQ_CHORD_VOICES 8
#define SEQ_MODULE_WIRE_MAX 192

/* Versioned, ordered contracts: rhythm emits pulses; pitch emits bounded voices.
 * IDs 1/2 are stored/Euclidean rhythm; ID 3 is the track palette.
 * ID 4 and module kind 1 are reserved for read-only legacy migration. */
typedef struct {
    uint16_t steps, fills, rotation, template_mask;
    uint8_t  kind, enabled, probability, accent, ratchet, template_active;
} seq_rhythm_component_t;

typedef enum { SEQ_MODULE_EUCLID = 0, SEQ_MODULE_CHORD_BANK = 1 } seq_module_kind_t;
typedef struct {
    uint16_t steps, fills, rotation, template_mask;
    uint8_t  enabled, probability, accent, ratchet, root, count, pin, template_active;
    uint8_t  pitch_mode; /* 0: palette pitches with step overrides; 1: transpose phrase. */
    int8_t   pool[8];
} seq_module_euclid_t;
typedef struct {
    uint8_t                kind;
    bool                   composed;
    seq_rhythm_component_t rhythm;
    union {
        seq_module_euclid_t euclid;
    } config;
} seq_module_t;

void seq_module_default(seq_module_t *module, uint8_t kind);
bool seq_module_valid(const seq_module_t *module);
/* Decode validates the entire object before assigning the destination. */
bool                   seq_module_decode(seq_module_t *module, const uint8_t *data, uint16_t size);
bool                   seq_module_encode(const seq_module_t *module, uint8_t *data, uint16_t *size);
/* Read-only migration for saved tracks; new bank installations remain rejected. */
bool seq_module_migrate_bank(seq_module_t *module, const uint8_t *data, uint16_t size);
seq_rhythm_component_t seq_module_rhythm(const seq_module_t *module);
bool                   seq_rhythm_valid(const seq_rhythm_component_t *rhythm);
bool                   seq_rhythm_hit(const seq_rhythm_component_t *rhythm, uint16_t step, uint16_t *index);
