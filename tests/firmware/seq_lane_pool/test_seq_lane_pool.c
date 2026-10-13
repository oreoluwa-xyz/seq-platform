#include <assert.h>
#include <stdio.h>

#include "drum_lane_pool.h"

static void fill_pool(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        assert(seq_drum_pool_step_set(i % SEQ_TRACK_COUNT, i / SEQ_TRACK_COUNT, SEQ_MAX_STEPS - 1u, true));
    }
    assert(seq_drum_pool_used() == count);
}

static void test_sparse_tracks_and_independent_lanes(void) {
    seq_drum_pool_init();
    assert(!seq_drum_pool_lane(0, 0));
    assert(!seq_drum_pool_step_get(0, 0, 0));
    assert(seq_drum_pool_step_set(0, 0, 0, false));
    assert(seq_drum_pool_used() == 0);
    fill_pool(SEQ_DRUM_LANE_POOL_CAPACITY);
    for (uint8_t i = 0; i < SEQ_DRUM_LANE_POOL_CAPACITY; i++) {
        uint8_t track = i % SEQ_TRACK_COUNT;
        uint8_t note  = i / SEQ_TRACK_COUNT;
        assert(seq_drum_pool_step_get(track, note, SEQ_MAX_STEPS - 1u));
        assert(!seq_drum_pool_step_get(track, note, 0));
        assert(seq_drum_pool_step_set(track, note, i, true));
    }
    for (uint8_t i = 0; i < SEQ_DRUM_LANE_POOL_CAPACITY; i++) {
        for (uint8_t j = 0; j < SEQ_DRUM_LANE_POOL_CAPACITY; j++) {
            assert(seq_drum_pool_step_get(i % SEQ_TRACK_COUNT, i / SEQ_TRACK_COUNT, j) == (i == j));
        }
    }
}

static void test_full_pool_and_reuse(void) {
    seq_drum_pool_init();
    fill_pool(SEQ_DRUM_LANE_POOL_CAPACITY);
#if SEQ_DRUM_LANE_POOL_CAPACITY < SEQ_TRACK_COUNT * SEQ_DRUM_NOTE_COUNT
    uint8_t track = SEQ_DRUM_LANE_POOL_CAPACITY % SEQ_TRACK_COUNT;
    uint8_t note  = SEQ_DRUM_LANE_POOL_CAPACITY / SEQ_TRACK_COUNT;
    assert(!seq_drum_pool_step_set(track, note, 0, true));
    assert(!seq_drum_pool_lane(track, note));
    assert(seq_drum_pool_used() == SEQ_DRUM_LANE_POOL_CAPACITY);
    assert(seq_drum_pool_step_set(0, 0, SEQ_MAX_STEPS - 1u, false));
    assert(seq_drum_pool_used() == SEQ_DRUM_LANE_POOL_CAPACITY - 1u);
    assert(seq_drum_pool_step_set(track, note, 0, true));
    assert(!seq_drum_pool_step_get(track, note, SEQ_MAX_STEPS - 1u));
    assert(!seq_drum_pool_step_get(0, 0, SEQ_MAX_STEPS - 1u));
    assert(seq_drum_pool_used() == SEQ_DRUM_LANE_POOL_CAPACITY);
#endif
}

static void test_group_reservation_is_atomic(void) {
#if SEQ_DRUM_LANE_POOL_CAPACITY < SEQ_TRACK_COUNT * SEQ_DRUM_NOTE_COUNT
    seq_drum_pool_init();
    fill_pool(SEQ_DRUM_LANE_POOL_CAPACITY - 1u);
    uint8_t  track = (SEQ_DRUM_LANE_POOL_CAPACITY - 1u) % SEQ_TRACK_COUNT;
    uint8_t  note  = (SEQ_DRUM_LANE_POOL_CAPACITY - 1u) / SEQ_TRACK_COUNT;
    uint16_t mask  = (uint16_t)((1u << note) | (1u << (note + 1u)));
    assert(!seq_drum_pool_can_reserve(track, mask));
    assert(!seq_drum_pool_reserve(track, mask));
    assert(!seq_drum_pool_lane(track, note));
    assert(!seq_drum_pool_lane(track, note + 1u));
    assert(seq_drum_pool_used() == SEQ_DRUM_LANE_POOL_CAPACITY - 1u);
    seq_drum_pool_clear_track(0);
    assert(seq_drum_pool_reserve(track, mask));
    assert(seq_drum_pool_step_set(track, note, 0, true));
    assert(seq_drum_pool_step_set(track, note + 1u, 0, true));
#endif
}

static void test_modifiers_follow_ownership(void) {
    seq_drum_pool_init();
    assert(seq_drum_pool_reserve(0, 1));
    seq_drum_lane_storage_t *lane = seq_drum_pool_lane(0, 0);
    assert(lane);
    for (uint8_t i = 0; i < SEQ_DRUM_STEP_MOD_CAP; i++) {
        assert(lane->mods[i].step == UINT16_MAX);
        assert(lane->mods[i].spacing == SEQ_DRUM_RATCHET_SPACING_DEFAULT);
    }
    lane->mods[0] = (seq_drum_step_mod_t){.step = 8, .config = 5, .spacing = 3};
    seq_drum_pool_prune(0, 0);
    assert(seq_drum_pool_used() == 1); /* A Euclid-only modifier still owns storage. */
    assert(seq_drum_pool_step_set(0, 0, 8, false));
    assert(seq_drum_pool_used() == 0);
    assert(seq_drum_pool_step_set(11, 11, 4, true));
    lane = seq_drum_pool_lane(11, 11);
    assert(lane->mods[0].step == UINT16_MAX);
    assert(lane->mods[0].config == 0);
    assert(lane->mods[0].spacing == SEQ_DRUM_RATCHET_SPACING_DEFAULT);
    seq_drum_pool_clear_track(11);
    assert(seq_drum_pool_used() == 0);
}

static void test_deleted_step_restore_after_reuse(void) {
    seq_drum_pool_init();
    assert(seq_drum_pool_step_set(0, 0, 8, true));
    seq_drum_pool_lane(0, 0)->mods[0] = (seq_drum_step_mod_t){.step = 8, .config = 5, .spacing = 3};
    seq_drum_step_mod_t saved         = seq_drum_pool_lane(0, 0)->mods[0];
    assert(seq_drum_pool_step_set(0, 0, 8, false));
    assert(seq_drum_pool_used() == 0);
    /* Another lane can reuse the slot before the double-tap restores the step. */
    assert(seq_drum_pool_step_set(11, 11, 4, true));
    assert(seq_drum_pool_step_set(0, 0, 8, true));
    seq_drum_pool_lane(0, 0)->mods[0] = saved;
    assert(seq_drum_pool_lane(0, 0)->mods[0].config == 5);
    assert(seq_drum_pool_lane(0, 0)->mods[0].spacing == 3);
    assert(seq_drum_pool_lane(11, 11)->mods[0].step == UINT16_MAX);
    assert(seq_drum_pool_step_get(11, 11, 4));
    assert(seq_drum_pool_step_get(0, 0, 8));
    assert(seq_drum_pool_used() == 2);
}

static void test_trim_and_reset(void) {
    seq_drum_pool_init();
    assert(seq_drum_pool_step_set(7, 10, 7, true));
    assert(seq_drum_pool_step_set(7, 10, 8, true));
    assert(seq_drum_pool_step_set(7, 10, SEQ_MAX_STEPS - 1u, true));
    seq_drum_pool_lane(7, 10)->mods[0] = (seq_drum_step_mod_t){.step = 8, .config = 4};
    seq_drum_pool_trim(7, 10, 8);
    assert(seq_drum_pool_step_get(7, 10, 7));
    assert(!seq_drum_pool_step_get(7, 10, 8));
    assert(!seq_drum_pool_step_get(7, 10, SEQ_MAX_STEPS - 1u));
    assert(seq_drum_pool_lane(7, 10)->mods[0].step == UINT16_MAX);
    seq_drum_pool_trim(7, 10, 7);
    assert(seq_drum_pool_used() == 0);
    assert(seq_drum_pool_step_set(7, 10, 0, true));
    seq_drum_pool_trim(7, 10, 0);
    assert(seq_drum_pool_used() == 0);
    assert(seq_drum_pool_reserve(0, 3));
    seq_drum_pool_lane(0, 0)->mods[0].step = 3;
    assert(seq_drum_pool_step_set(0, 1, 3, true));
    seq_drum_pool_reset_mods();
    assert(!seq_drum_pool_lane(0, 0));
    assert(seq_drum_pool_step_get(0, 1, 3));
    assert(seq_drum_pool_used() == 1);
    seq_drum_pool_init();
    assert(seq_drum_pool_used() == 0);
    assert(!seq_drum_pool_step_get(0, 1, 3));
}

static void test_clear_preserves_other_tracks(void) {
    seq_drum_pool_init();
    fill_pool(SEQ_DRUM_LANE_POOL_CAPACITY);
    seq_drum_pool_clear_track(0);
    for (uint8_t i = 0; i < SEQ_DRUM_LANE_POOL_CAPACITY; i++) {
        assert(seq_drum_pool_step_get(i % SEQ_TRACK_COUNT, i / SEQ_TRACK_COUNT, SEQ_MAX_STEPS - 1u) == (i % SEQ_TRACK_COUNT != 0));
    }
}

static void test_invalid_inputs(void) {
    seq_drum_pool_init();
    assert(!seq_drum_pool_reserve(SEQ_TRACK_COUNT, 1));
    assert(!seq_drum_pool_reserve(0, (uint16_t)(1u << SEQ_DRUM_NOTE_COUNT)));
    assert(!seq_drum_pool_step_set(0, SEQ_DRUM_NOTE_COUNT, 0, true));
    assert(!seq_drum_pool_step_set(0, 0, SEQ_MAX_STEPS, true));
    assert(!seq_drum_pool_step_get(0, 0, SEQ_MAX_STEPS));
    seq_drum_pool_clear_track(SEQ_TRACK_COUNT);
    seq_drum_pool_trim(0, SEQ_DRUM_NOTE_COUNT, 0);
    assert(seq_drum_pool_used() == 0);
}

int main(void) {
    test_sparse_tracks_and_independent_lanes();
    test_full_pool_and_reuse();
    test_group_reservation_is_atomic();
    test_modifiers_follow_ownership();
    test_deleted_step_restore_after_reuse();
    test_trim_and_reset();
    test_clear_preserves_other_tracks();
    test_invalid_inputs();
    printf("seq_lane_pool tests passed (capacity %u)\n", SEQ_DRUM_LANE_POOL_CAPACITY);
    return 0;
}
