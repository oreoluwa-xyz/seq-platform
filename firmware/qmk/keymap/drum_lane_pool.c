#include "drum_lane_pool.h"

#include <stddef.h>
#include <string.h>

_Static_assert(SEQ_DRUM_LANE_POOL_CAPACITY > 0 && SEQ_DRUM_LANE_POOL_CAPACITY <= SEQ_TRACK_COUNT * SEQ_DRUM_NOTE_COUNT, "Invalid drum lane pool capacity");
_Static_assert(SEQ_TRACK_COUNT *SEQ_DRUM_NOTE_COUNT < UINT8_MAX, "Lane owners must fit in a byte");
_Static_assert(SEQ_DRUM_NOTE_COUNT <= 16, "Lane mask must fit in 16 bits");

static seq_drum_lane_storage_t lanes[SEQ_DRUM_LANE_POOL_CAPACITY];
/* Zero means unallocated; both owners and slot indices are stored plus one. */
static uint8_t lane_slots[SEQ_TRACK_COUNT][SEQ_DRUM_NOTE_COUNT];
static uint8_t lane_owners[SEQ_DRUM_LANE_POOL_CAPACITY];

static void reset_mods(seq_drum_lane_storage_t *lane) {
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        lane->mods[i] = (seq_drum_step_mod_t){.step = UINT16_MAX, .spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT};
    }
}

void seq_drum_pool_init(void) {
    memset(lanes, 0, sizeof(lanes));
    memset(lane_slots, 0, sizeof(lane_slots));
    memset(lane_owners, 0, sizeof(lane_owners));
}

uint8_t seq_drum_pool_used(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < SEQ_DRUM_LANE_POOL_CAPACITY; i++) {
        count += lane_owners[i] != 0;
    }
    return count;
}

seq_drum_lane_storage_t *seq_drum_pool_lane(uint8_t track, uint8_t note) {
    if (track >= SEQ_TRACK_COUNT || note >= SEQ_DRUM_NOTE_COUNT) {
        return NULL;
    }
    uint8_t slot = lane_slots[track][note];
    return slot ? &lanes[slot - 1u] : NULL;
}

bool seq_drum_pool_can_reserve(uint8_t track, uint16_t notes) {
    if (track >= SEQ_TRACK_COUNT || (notes >> SEQ_DRUM_NOTE_COUNT)) {
        return false;
    }
    uint8_t needed = 0;
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        if ((notes & (1u << note)) && !lane_slots[track][note]) {
            needed++;
        }
    }
    return !needed || needed <= SEQ_DRUM_LANE_POOL_CAPACITY - seq_drum_pool_used();
}

bool seq_drum_pool_reserve(uint8_t track, uint16_t notes) {
    if (!seq_drum_pool_can_reserve(track, notes)) {
        return false;
    }
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        if (!(notes & (1u << note)) || lane_slots[track][note]) {
            continue;
        }
        for (uint8_t slot = 0; slot < SEQ_DRUM_LANE_POOL_CAPACITY; slot++) {
            if (!lane_owners[slot]) {
                memset(&lanes[slot], 0, sizeof(lanes[slot]));
                reset_mods(&lanes[slot]);
                lane_owners[slot]       = track * SEQ_DRUM_NOTE_COUNT + note + 1u;
                lane_slots[track][note] = slot + 1u;
                break;
            }
        }
    }
    return true;
}

bool seq_drum_pool_step_get(uint8_t track, uint8_t note, uint16_t step) {
    const seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    return lane && step < SEQ_MAX_STEPS && (lane->data[step >> 3] & (1u << (step & 7)));
}

void seq_drum_pool_prune(uint8_t track, uint8_t note) {
    const seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    if (!lane || lane->euclid.steps) {
        return;
    }
    for (uint16_t i = 0; i < SEQ_DRUM_STORAGE_BYTES; i++) {
        if (lane->data[i]) {
            return;
        }
    }
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        if (lane->mods[i].step != UINT16_MAX) {
            return;
        }
    }
    lane_owners[lane_slots[track][note] - 1u] = 0;
    lane_slots[track][note]                   = 0;
}

bool seq_drum_pool_step_set(uint8_t track, uint8_t note, uint16_t step, bool value) {
    if (track >= SEQ_TRACK_COUNT || note >= SEQ_DRUM_NOTE_COUNT || step >= SEQ_MAX_STEPS) {
        return false;
    }
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    if (!lane) {
        if (!value) {
            return true;
        }
        if (!seq_drum_pool_reserve(track, (uint16_t)(1u << note))) {
            return false;
        }
        lane = seq_drum_pool_lane(track, note);
    }
    uint8_t mask = (uint8_t)(1u << (step & 7));
    if (value) {
        lane->data[step >> 3] |= mask;
    } else {
        bool changed = (lane->data[step >> 3] & mask) != 0;
        lane->data[step >> 3] &= (uint8_t)~mask;
        for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
            if (lane->mods[i].step == step) {
                lane->mods[i] = (seq_drum_step_mod_t){.step = UINT16_MAX, .spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT};
                changed       = true;
            }
        }
        if (changed) {
            seq_drum_pool_prune(track, note);
        }
    }
    return true;
}

void seq_drum_pool_trim(uint8_t track, uint8_t note, uint16_t from_step) {
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(track, note);
    if (!lane || from_step >= SEQ_MAX_STEPS) {
        return;
    }
    uint16_t index = from_step >> 3;
    lane->data[index] &= (uint8_t)((1u << (from_step & 7)) - 1u);
    memset(&lane->data[index + 1u], 0, SEQ_DRUM_STORAGE_BYTES - index - 1u);
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        if (lane->mods[i].step >= from_step) {
            lane->mods[i] = (seq_drum_step_mod_t){.step = UINT16_MAX, .spacing = SEQ_DRUM_RATCHET_SPACING_DEFAULT};
        }
    }
    seq_drum_pool_prune(track, note);
}

void seq_drum_pool_clear_track(uint8_t track) {
    if (track >= SEQ_TRACK_COUNT) {
        return;
    }
    for (uint8_t note = 0; note < SEQ_DRUM_NOTE_COUNT; note++) {
        uint8_t slot = lane_slots[track][note];
        if (slot) {
            lane_owners[slot - 1u]  = 0;
            lane_slots[track][note] = 0;
        }
    }
}

void seq_drum_pool_reset_mods(void) {
    for (uint8_t slot = 0; slot < SEQ_DRUM_LANE_POOL_CAPACITY; slot++) {
        if (lane_owners[slot]) {
            uint8_t owner = lane_owners[slot] - 1u;
            reset_mods(&lanes[slot]);
            seq_drum_pool_prune(owner / SEQ_DRUM_NOTE_COUNT, owner % SEQ_DRUM_NOTE_COUNT);
        }
    }
}
