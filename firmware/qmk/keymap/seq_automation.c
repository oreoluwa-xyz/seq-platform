#include "seq_automation.h"
#include <stddef.h>
#include <string.h>
static seq_cc_event_t events[SEQ_CC_EVENT_CAPACITY];
static uint16_t       count;
void                  seq_automation_init(void) {
    count = 0;
}
uint16_t seq_automation_count(void) {
    return count;
}
uint16_t seq_automation_track_count(uint8_t track) {
    uint16_t total = 0;
    for (uint16_t i = 0; i < count; i++)
        total += events[i].track == track;
    return total;
}
const seq_cc_event_t *seq_automation_event(uint16_t index) {
    return index < count ? &events[index] : NULL;
}
bool seq_automation_record(uint8_t track, uint8_t index, uint16_t step, uint8_t value) {
    if (track >= SEQ_TRACK_COUNT || index >= SEQ_CC_MAPPING_COUNT || step >= SEQ_MAX_STEPS || value > 127) return false;
    for (uint16_t i = 0; i < count; i++) {
        if (events[i].track == track && events[i].index == index && events[i].step == step) {
            events[i].value = value;
            return true;
        }
    }
    if (count == SEQ_CC_EVENT_CAPACITY || seq_automation_track_count(track) == SEQ_CC_TRACK_EVENT_CAPACITY) return false;
    events[count++] = (seq_cc_event_t){.track = track, .index = index, .step = step, .value = value};
    return true;
}
void seq_automation_trim(uint8_t track, uint16_t length) {
    for (uint16_t i = 0; i < count;) {
        if (events[i].track == track && events[i].step >= length) {
            memmove(&events[i], &events[i + 1], (--count - i) * sizeof(events[0]));
        } else
            i++;
    }
}
void seq_automation_clear(uint8_t track) {
    seq_automation_trim(track, 0);
}
bool seq_automation_can_duplicate(uint8_t track, uint16_t length) {
    uint16_t extra = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (events[i].track == track && events[i].step < length && events[i].step + length < SEQ_MAX_STEPS) extra++;
    }
    return count + extra <= SEQ_CC_EVENT_CAPACITY && seq_automation_track_count(track) + extra <= SEQ_CC_TRACK_EVENT_CAPACITY;
}
bool seq_automation_duplicate(uint8_t track, uint16_t length) {
    if (!seq_automation_can_duplicate(track, length)) return false;
    uint16_t original = count;
    for (uint16_t i = 0; i < original; i++) {
        seq_cc_event_t event = events[i];
        if (event.track == track && event.step < length && event.step + length < SEQ_MAX_STEPS) {
            seq_automation_record(track, event.index, event.step + length, event.value);
        }
    }
    return true;
}
