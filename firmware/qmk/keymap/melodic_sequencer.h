#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sequencer_config.h"

#define MEL_TRACK_COUNT 12
#ifndef MEL_TRACK_MAX_EVENTS
#    define MEL_TRACK_MAX_EVENTS 128
#endif
#define MEL_TRACK_STORAGE_MAX MEL_TRACK_MAX_EVENTS

#ifndef SEQ_MAX_STEPS
#    define SEQ_MAX_STEPS (16 * 128)
#endif

#ifndef MEL_DEFAULT_PITCH
#    define MEL_DEFAULT_PITCH 60
#endif

#ifndef MEL_DEFAULT_VELOCITY
#    define MEL_DEFAULT_VELOCITY 100
#endif

typedef struct {
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
} mel_note_event_t;

typedef struct {
    uint8_t  pitch;
    uint8_t  velocity;
    uint16_t start_step;
    uint16_t end_step;
    uint8_t  event_index;
} mel_note_descriptor_t;

typedef struct {
    uint8_t  pitch;
    uint8_t  velocity;
    uint16_t start_step;
    uint16_t end_step;
} mel_event_snapshot_t;

void seq_melodic_notify_changed(void);

void mel_tracks_init(void);
void mel_track_reset(uint8_t track_index);

typedef enum {
    MEL_TRACK_STEP_RESULT_NONE = 0,
    MEL_TRACK_STEP_RESULT_EVENT_ADDED,
    MEL_TRACK_STEP_RESULT_EVENT_REMOVED,
    MEL_TRACK_STEP_RESULT_SELECTION_STARTED,
} mel_track_step_result_t;

mel_track_step_result_t mel_track_handle_step_press(uint8_t track_index, uint16_t step, uint8_t pitch);
bool                    mel_track_add_note(uint8_t track_index, uint16_t start_step, uint16_t end_step, uint8_t pitch, uint8_t velocity);
bool                    mel_track_extend_note(uint8_t track_index, uint16_t prev_step, uint16_t new_step, uint8_t pitch);
bool                    mel_track_remove_note(uint8_t track_index, uint16_t step, uint8_t pitch);
bool                    mel_track_remove_step_all(uint8_t track_index, uint16_t step);
void                    mel_track_trim(uint8_t track_index, uint16_t max_step);
bool                    mel_track_clear_range(uint8_t track_index, uint16_t from_step, uint16_t to_step);
bool                    mel_track_duplicate_append(uint8_t track_index, uint16_t source_length, uint16_t new_length);
uint16_t                mel_track_window_mask(uint8_t track_index, uint16_t window_start);
bool                    mel_track_has_activity(uint8_t track_index);
void                    mel_track_cancel_pending(uint8_t track_index);
bool                    mel_track_remove_event(uint8_t track_index, uint8_t event_index);
bool                    mel_track_event_at(uint8_t track, uint8_t index, mel_event_snapshot_t *out);
uint8_t                 mel_track_event_count(uint8_t track);
bool                    mel_track_can_add(uint8_t track, uint16_t count);
bool                    mel_track_can_duplicate(uint8_t track, uint16_t source_length, uint16_t new_length);
void                    seq_melodic_capacity_exceeded(void);
bool                    mel_track_note_descriptor(uint8_t track_index, uint16_t step, mel_note_descriptor_t *out);
bool                    mel_track_set_note_pitch(uint8_t track_index, uint8_t event_index, uint8_t pitch);
bool                    mel_track_set_note_velocity(uint8_t track_index, uint8_t event_index, uint8_t velocity);
bool                    mel_track_offset_note_length(uint8_t track_index, uint8_t event_index, int16_t delta);
uint8_t                 mel_track_snapshot(uint8_t track_index, mel_event_snapshot_t *out, uint8_t max_events);
bool                    mel_track_restore(uint8_t track_index, const mel_event_snapshot_t *events, uint8_t count);

void mel_track_edit_begin(uint8_t track);
bool mel_track_edit_end(void);
