#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIRTUAL_MIDI_EVENT_NONE = 0,
    VIRTUAL_MIDI_EVENT_CLOCK_HIGH,
    VIRTUAL_MIDI_EVENT_CLOCK_LOW,
    VIRTUAL_MIDI_EVENT_RESET_HIGH,
    VIRTUAL_MIDI_EVENT_RESET_LOW,
    VIRTUAL_MIDI_EVENT_NOTE_ON,
    VIRTUAL_MIDI_EVENT_NOTE_OFF,
    VIRTUAL_MIDI_EVENT_CC,
    VIRTUAL_MIDI_EVENT_START,
    VIRTUAL_MIDI_EVENT_STOP,
    VIRTUAL_MIDI_EVENT_CONTINUE,
    VIRTUAL_MIDI_EVENT_SONG_POSITION,
} virtual_midi_event_kind_t;

typedef struct {
    virtual_midi_event_kind_t kind;
    uint8_t                   channel;
    uint8_t                   note;
    uint8_t                   velocity;
    uint32_t                  timestamp_ms;
} virtual_midi_event_t;

#ifndef VIRTUAL_MIDI_MAX_EVENTS
#    define VIRTUAL_MIDI_MAX_EVENTS 128
#endif

typedef struct {
    bool                 running;
    bool                 clock_high;
    bool                 reset_high;
    uint32_t             clock_tick_count;
    uint32_t             reset_count;
    uint32_t             last_clock_ms;
    uint32_t             last_reset_ms;
    uint32_t             next_clock_due_ms;
    uint32_t             clock_interval_base_ms;
    uint32_t             clock_interval_remainder;
    uint32_t             clock_interval_error;
    uint8_t              clock_pulses_remaining;
    virtual_midi_event_t events[VIRTUAL_MIDI_MAX_EVENTS];
    uint8_t              event_count;
} virtual_midi_state_t;

void                        virtual_midi_init(void);
void                        virtual_midi_start(uint32_t now);
void                        virtual_midi_stop(uint32_t now);
void                        virtual_midi_reset(uint32_t now);
void                        virtual_midi_clock_tick(uint32_t now, uint32_t step_interval_ms);
void                        virtual_midi_song_position(uint16_t position, uint32_t now);
void                        virtual_midi_continue(uint32_t now);
void                        virtual_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t now);
void                        virtual_midi_note_off(uint8_t channel, uint8_t note, uint32_t now);
void                        virtual_midi_control_change(uint8_t channel, uint8_t control, uint8_t value, uint32_t now);
void                        virtual_midi_task(uint32_t now);
const virtual_midi_state_t *virtual_midi_state(void);
void                        virtual_midi_clear_events(void);

void virtual_midi_flush(void);
bool virtual_midi_transport_fault(void);
void virtual_midi_resync_clock(uint32_t position, uint32_t boundary_ms, uint32_t now);

void virtual_midi_set_clock_output(bool enabled);
void virtual_midi_set_tempo(uint16_t tempo);

void virtual_midi_backup_reply(uint8_t command, uint8_t object, uint16_t offset, uint8_t status, const uint8_t *data, uint8_t size);
void virtual_midi_receive_sysex(uint16_t start, uint8_t size, const uint8_t *data);
