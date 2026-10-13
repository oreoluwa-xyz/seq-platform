#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { SEQ_SCALE_COUNT = 11 };
/* scale.quantizer@1: pure bounded pitch transform; no MIDI or allocation. */
typedef struct { uint8_t enabled, root, kind; } seq_scale_context_t;
typedef struct { uint8_t mode, root, kind, direction; } seq_scale_track_t; /* Follow=0, Local=1, Off=2; nearest/down/up. */
static inline uint16_t seq_scale_mask(uint8_t kind) {
    const uint16_t masks[] = {0xAB5,0x6AD,0x5AB,0xAD5,0x6B5,0x5AD,0x56B,0x9AD,0x9B5,0x295,0x4A9};
    return kind < SEQ_SCALE_COUNT ? masks[kind] : 0;
}
static inline bool seq_scale_contains(uint8_t note, uint8_t root, uint8_t kind) {
    return (seq_scale_mask(kind) & (1u << ((note + 12u - root) % 12u))) != 0;
}
static inline uint8_t seq_scale_quantize(uint8_t note, uint8_t root, uint8_t kind, uint8_t direction) {
    if (note > 127 || root > 11 || kind >= SEQ_SCALE_COUNT || direction > 2) return note;
    for (uint8_t distance = 0; distance < 12; distance++) {
        if (direction != 2 && note >= distance && seq_scale_contains(note-distance,root,kind)) return note-distance;
        if (direction != 1 && note+distance <= 127 && seq_scale_contains(note+distance,root,kind)) return note+distance;
    }
    /* At the MIDI boundary, use the nearest in-range scale tone. Ties go down. */
    return direction ? seq_scale_quantize(note,root,kind,0) : note;
}
