#include "melodic_sequencer.h"

#include <string.h>

_Static_assert(MEL_TRACK_MAX_EVENTS > 0 && MEL_TRACK_MAX_EVENTS < UINT8_MAX, "Melodic event indices must fit in a byte");

__attribute__((weak)) void seq_melodic_capacity_exceeded(void) {}

__attribute__((weak)) void seq_melodic_notify_changed(void) {}

typedef mel_event_snapshot_t mel_event_t;

typedef struct {
    mel_event_t events[MEL_TRACK_MAX_EVENTS];
    uint8_t     event_count;
    bool        selection_active;
    uint16_t    selection_start;
} mel_track_t;

static mel_track_t mel_tracks[MEL_TRACK_COUNT];
static mel_track_t edit_backup;
static uint8_t     edit_track;
static bool        edit_active, edit_failed;
mel_event_snapshot_t *mel_track_snapshot_scratch(void) {
    return edit_active ? NULL : edit_backup.events;
}

void               mel_track_edit_begin(uint8_t track) {
    edit_track  = track % MEL_TRACK_COUNT;
    edit_backup = mel_tracks[edit_track];
    edit_active = true;
    edit_failed = false;
}
bool mel_track_edit_end(void) {
    bool ok = !edit_failed;
    if (!ok) mel_tracks[edit_track] = edit_backup;
    edit_active = false;
    return ok;
}
static void mel_capacity_failure(void) {
    if (edit_active) edit_failed = true;
    seq_melodic_capacity_exceeded();
}

static bool mel_track_find_event(const mel_track_t *track, uint16_t step, uint8_t *index_out) {
    for (uint8_t i = 0; i < track->event_count; i++) {
        const mel_event_t *ev = &track->events[i];
        if (ev->start_step <= step && ev->end_step >= step) {
            if (index_out) {
                *index_out = i;
            }
            return true;
        }
    }
    return false;
}

static bool mel_track_add_event(mel_track_t *track, uint16_t start, uint16_t end, uint8_t pitch, uint8_t velocity) {
    if (track->event_count >= MEL_TRACK_MAX_EVENTS) {
        mel_capacity_failure();
        return false;
    }

    if (end < start) {
        uint16_t tmp = start;
        start        = end;
        end          = tmp;
    }

    if (start >= SEQ_MAX_STEPS) {
        start = SEQ_MAX_STEPS - 1;
    }
    if (end >= SEQ_MAX_STEPS) {
        end = SEQ_MAX_STEPS - 1;
    }

    uint8_t insert_at = track->event_count;
    for (uint8_t i = 0; i < track->event_count; i++) {
        if (start < track->events[i].start_step) {
            insert_at = i;
            break;
        }
    }

    if (insert_at < track->event_count) {
        memmove(&track->events[insert_at + 1], &track->events[insert_at], (track->event_count - insert_at) * sizeof(mel_event_t));
    }

    mel_event_t *ev = &track->events[insert_at];
    ev->pitch       = pitch;
    ev->velocity    = velocity;
    ev->start_step  = start;
    ev->end_step    = end;
    track->event_count++;
    seq_melodic_notify_changed();
    return true;
}

static void mel_track_split_event(mel_track_t *track, uint8_t index, uint16_t split_step) {
    if (track->event_count >= MEL_TRACK_MAX_EVENTS) {
        return;
    }

    mel_event_t *ev = &track->events[index];
    if (split_step <= ev->start_step || split_step >= ev->end_step) {
        return;
    }

    uint16_t tail_start = split_step + 1;
    uint16_t tail_end   = ev->end_step;

    if (tail_start > tail_end) {
        return;
    }

    uint8_t insert_at = index + 1;
    if (track->event_count < MEL_TRACK_MAX_EVENTS) {
        memmove(&track->events[insert_at + 1], &track->events[insert_at], (track->event_count - insert_at) * sizeof(mel_event_t));
        mel_event_t *tail = &track->events[insert_at];
        tail->pitch       = ev->pitch;
        tail->velocity    = ev->velocity;
        tail->start_step  = tail_start;
        tail->end_step    = tail_end;
        track->event_count++;
        ev->end_step = split_step - 1;
        seq_melodic_notify_changed();
    }
}

bool mel_track_add_note(uint8_t track_index, uint16_t start_step, uint16_t end_step, uint8_t pitch, uint8_t velocity) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    return mel_track_add_event(track, start_step, end_step, pitch, velocity);
}

bool mel_track_extend_note(uint8_t track_index, uint16_t prev_step, uint16_t new_step, uint8_t pitch) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    for (uint8_t i = 0; i < track->event_count; i++) {
        mel_event_t *ev = &track->events[i];
        if (ev->pitch == pitch && ev->end_step == prev_step) {
            ev->end_step = new_step;
            seq_melodic_notify_changed();
            return true;
        }
    }
    return false;
}

bool mel_track_remove_note(uint8_t track_index, uint16_t step, uint8_t pitch) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];

    for (uint8_t i = 0; i < track->event_count; i++) {
        mel_event_t *ev = &track->events[i];
        if (ev->pitch != pitch) {
            continue;
        }
        if (ev->start_step <= step && ev->end_step >= step) {
            if (ev->start_step == ev->end_step) {
                memmove(&track->events[i], &track->events[i + 1], (track->event_count - i - 1) * sizeof(mel_event_t));
                track->event_count--;
                seq_melodic_notify_changed();
                return true;
            }

            if (step == ev->start_step) {
                ev->start_step++;
                seq_melodic_notify_changed();
                return true;
            }

            if (step == ev->end_step) {
                ev->end_step--;
                seq_melodic_notify_changed();
                return true;
            }

            if (track->event_count < MEL_TRACK_MAX_EVENTS) {
                mel_track_split_event(track, i, step);
            } else {
                mel_capacity_failure();
                return false;
            }
            seq_melodic_notify_changed();
            return true;
        }
    }
    return false;
}

bool mel_track_clear_range(uint8_t track_index, uint16_t from_step, uint16_t to_step) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (to_step <= from_step) return true;
    uint16_t end          = to_step - 1u;
    int16_t  result_count = track->event_count;
    for (uint8_t i = 0; i < track->event_count; i++) {
        mel_event_t *ev = &track->events[i];
        if (ev->start_step >= from_step && ev->end_step <= end)
            result_count--;
        else if (ev->start_step < from_step && ev->end_step > end)
            result_count++;
    }
    if (result_count > MEL_TRACK_MAX_EVENTS) {
        mel_capacity_failure();
        return false;
    }
    /* Remove complete events before splitting spans, so released slots are usable. */
    for (uint8_t i = 0; i < track->event_count;) {
        mel_event_t *ev = &track->events[i];
        if (ev->start_step >= from_step && ev->end_step <= end)
            mel_track_remove_event(track_index, i);
        else
            i++;
    }
    uint8_t original = track->event_count;
    for (uint8_t i = 0; i < original; i++) {
        mel_event_t *ev = &track->events[i];
        if (ev->end_step < from_step || ev->start_step > end) continue;
        if (ev->start_step < from_step && ev->end_step > end) {
            mel_event_t tail                    = *ev;
            ev->end_step                        = from_step - 1u;
            tail.start_step                     = to_step;
            track->events[track->event_count++] = tail;
        } else if (ev->start_step < from_step)
            ev->end_step = from_step - 1u;
        else
            ev->start_step = to_step;
    }
    seq_melodic_notify_changed();
    return true;
}

bool mel_track_remove_step_all(uint8_t track_index, uint16_t step) {
    return mel_track_clear_range(track_index, step, step + 1u);
}

void mel_track_trim(uint8_t track_index, uint16_t max_step) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (!max_step) {
        track->event_count      = 0;
        track->selection_active = false;
        track->selection_start  = 0;
        seq_melodic_notify_changed();
        return;
    }

    bool changed = false;
    for (uint8_t i = 0; i < track->event_count;) {
        mel_event_t *ev = &track->events[i];
        if (ev->start_step >= max_step) {
            memmove(&track->events[i], &track->events[i + 1], (track->event_count - i - 1) * sizeof(mel_event_t));
            track->event_count--;
            changed = true;
            continue;
        }
        if (ev->end_step >= max_step) {
            if (ev->end_step != max_step - 1) {
                ev->end_step = max_step - 1;
                changed      = true;
            }
        }
        i++;
    }
    if (changed) {
        seq_melodic_notify_changed();
    }
}

bool mel_track_can_add(uint8_t track, uint16_t count) {
    return count + mel_tracks[track % MEL_TRACK_COUNT].event_count <= MEL_TRACK_MAX_EVENTS;
}

bool mel_track_can_duplicate(uint8_t track_index, uint16_t source_length, uint16_t new_length) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    uint16_t     count = 0;
    for (uint8_t i = 0; i < track->event_count; i++) {
        if (track->events[i].start_step < source_length && track->events[i].start_step + source_length < new_length) count++;
    }
    return mel_track_can_add(track_index, count);
}

bool mel_track_duplicate_append(uint8_t track_index, uint16_t source_length, uint16_t new_length) {
    if (!source_length || new_length <= source_length) return true;
    if (!mel_track_can_duplicate(track_index, source_length, new_length)) {
        mel_capacity_failure();
        return false;
    }
    mel_track_t *track    = &mel_tracks[track_index % MEL_TRACK_COUNT];
    uint8_t      original = track->event_count;
    for (uint8_t i = 0; i < original; i++) {
        mel_event_t ev = track->events[i];
        if (ev.start_step >= source_length || ev.start_step + source_length >= new_length) continue;
        ev.start_step += source_length;
        ev.end_step += source_length;
        if (ev.end_step >= new_length) ev.end_step = new_length - 1u;
        /* Append without shifting the source events while copying. */
        track->events[track->event_count++] = ev;
    }
    seq_melodic_notify_changed();
    return true;
}

uint16_t mel_track_window_mask(uint8_t track_index, uint16_t window_start) {
    mel_track_t *track      = &mel_tracks[track_index % MEL_TRACK_COUNT];
    uint16_t     mask       = 0;
    uint16_t     window_end = window_start + 15;
    for (uint8_t i = 0; i < track->event_count; i++) {
        mel_event_t *ev = &track->events[i];
        if (ev->end_step < window_start || ev->start_step > window_end) {
            continue;
        }
        uint16_t start = ev->start_step < window_start ? window_start : ev->start_step;
        uint16_t end   = ev->end_step > window_end ? window_end : ev->end_step;
        for (uint16_t step = start; step <= end; step++) {
            uint16_t local = step - window_start;
            if (local < 16) {
                mask |= (uint16_t)(1u << local);
            }
        }
    }
    if (track->selection_active && track->selection_start >= window_start && track->selection_start <= window_end) {
        uint16_t local = track->selection_start - window_start;
        if (local < 16) {
            mask |= (uint16_t)(1u << local);
        }
    }
    return mask;
}

void mel_tracks_init(void) {
    for (uint8_t i = 0; i < MEL_TRACK_COUNT; i++) {
        mel_track_reset(i);
    }
}

void mel_track_reset(uint8_t track_index) {
    mel_track_t *track      = &mel_tracks[track_index % MEL_TRACK_COUNT];
    track->event_count      = 0;
    track->selection_active = false;
    track->selection_start  = 0;
}

void mel_track_cancel_pending(uint8_t track_index) {
    mel_tracks[track_index % MEL_TRACK_COUNT].selection_active = false;
}

bool mel_track_remove_event(uint8_t track_index, uint8_t event_index) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (event_index >= track->event_count) {
        return false;
    }
    if (event_index + 1u < track->event_count) {
        memmove(&track->events[event_index], &track->events[event_index + 1u], (track->event_count - event_index - 1u) * sizeof(mel_event_t));
    }
    track->event_count--;
    seq_melodic_notify_changed();
    return true;
}

mel_track_step_result_t mel_track_handle_step_press(uint8_t track_index, uint16_t step, uint8_t pitch) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];

    if (track->selection_active) {
        uint16_t start          = track->selection_start;
        uint16_t s              = (start < step) ? start : step;
        uint16_t e              = (start < step) ? step : start;
        bool     added          = mel_track_add_note(track_index, s, e, pitch, MEL_DEFAULT_VELOCITY);
        track->selection_active = false;
        return added ? MEL_TRACK_STEP_RESULT_EVENT_ADDED : MEL_TRACK_STEP_RESULT_NONE;
    }

    if (mel_track_remove_note(track_index, step, pitch)) {
        return MEL_TRACK_STEP_RESULT_EVENT_REMOVED;
    }

    track->selection_start  = step;
    track->selection_active = true;
    return MEL_TRACK_STEP_RESULT_SELECTION_STARTED;
}

bool mel_track_has_activity(uint8_t track_index) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    return track->event_count > 0 || track->selection_active;
}

uint8_t mel_track_event_count(uint8_t track) {
    return mel_tracks[track % MEL_TRACK_COUNT].event_count;
}

bool mel_track_event_at(uint8_t track_index, uint8_t index, mel_event_snapshot_t *out) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (!out || index >= track->event_count) return false;
    mel_event_t *ev = &track->events[index];
    *out            = (mel_event_snapshot_t){.pitch = ev->pitch, .velocity = ev->velocity, .start_step = ev->start_step, .end_step = ev->end_step};
    return true;
}

bool mel_track_note_descriptor(uint8_t track_index, uint16_t step, mel_note_descriptor_t *out) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    uint8_t      index;
    if (!mel_track_find_event(track, step, &index)) {
        return false;
    }
    if (out) {
        mel_event_t *ev  = &track->events[index];
        out->pitch       = ev->pitch;
        out->velocity    = ev->velocity;
        out->start_step  = ev->start_step;
        out->end_step    = ev->end_step;
        out->event_index = index;
    }
    return true;
}

bool mel_track_set_note_pitch(uint8_t track_index, uint8_t event_index, uint8_t pitch) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (event_index >= track->event_count) {
        return false;
    }
    mel_event_t *ev = &track->events[event_index];
    if (ev->pitch == pitch) {
        return true;
    }
    ev->pitch = pitch;
    seq_melodic_notify_changed();
    return true;
}

bool mel_track_set_note_velocity(uint8_t track_index, uint8_t event_index, uint8_t velocity) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (event_index >= track->event_count) {
        return false;
    }
    mel_event_t *ev = &track->events[event_index];
    if (ev->velocity == velocity) {
        return true;
    }
    ev->velocity = velocity;
    seq_melodic_notify_changed();
    return true;
}

bool mel_track_offset_note_length(uint8_t track_index, uint8_t event_index, int16_t delta) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    if (event_index >= track->event_count) {
        return false;
    }
    mel_event_t *ev      = &track->events[event_index];
    int32_t      new_end = (int32_t)ev->end_step + delta;
    if (new_end < (int32_t)ev->start_step) {
        new_end = ev->start_step;
    }
    if (new_end >= SEQ_MAX_STEPS) {
        new_end = SEQ_MAX_STEPS - 1;
    }
    if (ev->end_step == (uint16_t)new_end) {
        return true;
    }
    ev->end_step = (uint16_t)new_end;
    seq_melodic_notify_changed();
    return true;
}

uint8_t mel_track_snapshot(uint8_t track_index, mel_event_snapshot_t *out, uint8_t max_events) {
    mel_track_t *track = &mel_tracks[track_index % MEL_TRACK_COUNT];
    uint8_t      count = track->event_count;
    if (out && max_events) {
        uint8_t limit = (count < max_events) ? count : max_events;
        for (uint8_t i = 0; i < limit; i++) {
            const mel_event_t *ev = &track->events[i];
            out[i].pitch          = ev->pitch;
            out[i].velocity       = ev->velocity;
            out[i].start_step     = ev->start_step;
            out[i].end_step       = ev->end_step;
        }
    }
    return count;
}

bool mel_track_restore(uint8_t track_index, const mel_event_snapshot_t *events, uint8_t count) {
    if (count > MEL_TRACK_MAX_EVENTS || (count && !events)) return false;
    for (uint8_t i = 0; i < count; i++) {
        if (events[i].pitch > 127 || !events[i].velocity || events[i].velocity > 127 || events[i].start_step > events[i].end_step || events[i].end_step >= SEQ_MAX_STEPS) return false;
    }
    mel_track_reset(track_index);
    for (uint8_t i = 0; i < count; i++) {
        const mel_event_snapshot_t *ev                      = &events[i];
        mel_tracks[track_index % MEL_TRACK_COUNT].events[i] = (mel_event_t){.start_step = ev->start_step, .end_step = ev->end_step, .pitch = ev->pitch, .velocity = ev->velocity};
    }
    mel_tracks[track_index % MEL_TRACK_COUNT].event_count = count;
    seq_melodic_notify_changed();
    return true;
}
