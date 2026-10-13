#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sequencer_config.h"
#ifndef SEQ_CC_EVENT_CAPACITY
#    define SEQ_CC_EVENT_CAPACITY 512
#endif
#ifndef SEQ_CC_TRACK_EVENT_CAPACITY
#    define SEQ_CC_TRACK_EVENT_CAPACITY 128
#endif
typedef struct {
    uint16_t step;
    uint8_t  track;
    uint8_t  index;
    uint8_t  value;
} seq_cc_event_t;
void                  seq_automation_init(void);
uint16_t              seq_automation_count(void);
uint16_t              seq_automation_track_count(uint8_t track);
const seq_cc_event_t *seq_automation_event(uint16_t index);
bool                  seq_automation_record(uint8_t track, uint8_t index, uint16_t step, uint8_t value);
void                  seq_automation_clear(uint8_t track);
void                  seq_automation_trim(uint8_t track, uint16_t length);
bool                  seq_automation_can_duplicate(uint8_t track, uint16_t length);
bool                  seq_automation_duplicate(uint8_t track, uint16_t length);
