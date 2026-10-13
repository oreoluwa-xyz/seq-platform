#include "virtual_midi.h"

#include "timer.h"

#include <string.h>

#ifdef MIDI_ENABLE
#    include "qmk_midi.h"
#    include "midi.h"
#    ifndef SEQ_MIDI_TRANSPORT_SEND
#        define SEQ_MIDI_TRANSPORT_SEND 1
#    endif
#    ifdef PROTOCOL_LUFA
#        include <LUFA/Drivers/USB/USB.h>
#    elif defined(PROTOCOL_CHIBIOS)
#        include "usb_util.h"
#    endif
#endif

#ifndef VIRTUAL_MIDI_PULSE_MS
#    define VIRTUAL_MIDI_PULSE_MS 2
#endif

#ifndef VIRTUAL_MIDI_MAX_EVENTS
#    define VIRTUAL_MIDI_MAX_EVENTS 64
#endif
#define VIRTUAL_MIDI_CLOCKS_PER_STEP 6

static virtual_midi_state_t state;
static bool                 clock_output = true;
static uint16_t             clock_tempo  = 120;
static uint32_t             clock_error;
void                        virtual_midi_set_clock_output(bool enabled) {
    clock_output = enabled;
}
void virtual_midi_set_tempo(uint16_t tempo) {
    clock_tempo = tempo ? tempo : 120;
}
__attribute__((weak)) void seq_midi_realtime(uint8_t message, uint32_t now) {}
__attribute__((weak)) void seq_midi_song_position(uint16_t position) {}
#ifdef MIDI_ENABLE
static void receive_sysex(MidiDevice *device, uint16_t start, uint8_t size, uint8_t *data) {
    virtual_midi_receive_sysex(start, size, data);
}
static void receive_realtime(MidiDevice *device, uint8_t message) {
    seq_midi_realtime(message, timer_read32());
}
static void receive_position(MidiDevice *device, uint8_t status, uint8_t lsb, uint8_t msb) {
    seq_midi_song_position(lsb | ((uint16_t)msb << 7));
}
#endif

static void push_event(virtual_midi_event_kind_t kind, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t now) {
    if (kind == VIRTUAL_MIDI_EVENT_NONE) {
        return;
    }
    if (state.event_count >= VIRTUAL_MIDI_MAX_EVENTS) {
        memmove(&state.events[0], &state.events[1], (VIRTUAL_MIDI_MAX_EVENTS - 1) * sizeof(virtual_midi_event_t));
        state.event_count = VIRTUAL_MIDI_MAX_EVENTS - 1;
    }
    virtual_midi_event_t *ev = &state.events[state.event_count++];
    ev->kind                 = kind;
    ev->channel              = channel;
    ev->note                 = note;
    ev->velocity             = velocity;
    ev->timestamp_ms         = now;
}

#ifdef MIDI_ENABLE
static inline bool midi_transport_ready(void) {
#    ifdef PROTOCOL_LUFA
    return USB_DeviceState == DEVICE_STATE_Configured;
#    elif defined(PROTOCOL_CHIBIOS)
    return usb_connected_state();
#    else
    return true;
#    endif
}
#    define MIDI_IF_READY(call)           \
        do {                              \
            if (midi_transport_ready()) { \
                call;                     \
            }                             \
        } while (0)
#else
#    define MIDI_IF_READY(call) \
        do {                    \
        } while (0)
#endif

static void emit_clock_pulse(uint32_t now) {
    state.clock_tick_count++;
    state.clock_high    = true;
    state.last_clock_ms = now;
    push_event(VIRTUAL_MIDI_EVENT_CLOCK_HIGH, 0, 0, 0, now);
#if defined(MIDI_ENABLE) && SEQ_MIDI_TRANSPORT_SEND
    if (clock_output) MIDI_IF_READY(midi_send_clock(&midi_device));
#endif
}

static void schedule_next_clock_interval(void) {
    if (state.clock_pulses_remaining == 0) {
        return;
    }
    uint32_t interval = state.clock_interval_base_ms;
    state.clock_interval_error += state.clock_interval_remainder;
    if (state.clock_interval_error >= VIRTUAL_MIDI_CLOCKS_PER_STEP) {
        state.clock_interval_error -= VIRTUAL_MIDI_CLOCKS_PER_STEP;
        interval++;
    }
    if (interval == 0) {
        interval = 1;
    }
    state.next_clock_due_ms += interval;
}

void virtual_midi_init(void) {
    memset(&state, 0, sizeof(state));
    clock_error = 0;
#ifdef MIDI_ENABLE
    midi_register_sysex_callback(&midi_device, receive_sysex);
    midi_register_realtime_callback(&midi_device, receive_realtime);
    midi_register_songposition_callback(&midi_device, receive_position);
#endif
}

void virtual_midi_start(uint32_t now) {
    state.running                  = true;
    state.last_clock_ms            = now;
    state.clock_high               = false;
    state.clock_pulses_remaining   = 0;
    state.clock_interval_base_ms   = 0;
    state.clock_interval_remainder = 0;
    state.clock_interval_error     = 0;
    state.next_clock_due_ms        = now;
    clock_error                    = 0;
    push_event(VIRTUAL_MIDI_EVENT_START, 0, 0, 0, now);
#if defined(MIDI_ENABLE) && SEQ_MIDI_TRANSPORT_SEND
    if (clock_output) MIDI_IF_READY(midi_send_start(&midi_device));
#endif
}

void virtual_midi_stop(uint32_t now) {
    if (!state.running && !state.clock_high && !state.reset_high) return;
    if (state.clock_high) {
        state.clock_high = false;
        push_event(VIRTUAL_MIDI_EVENT_CLOCK_LOW, 0, 0, 0, now);
    }
    if (state.reset_high) {
        state.reset_high = false;
        push_event(VIRTUAL_MIDI_EVENT_RESET_LOW, 0, 0, 0, now);
    }
    state.running                  = false;
    state.last_clock_ms            = now;
    state.last_reset_ms            = now;
    state.clock_pulses_remaining   = 0;
    state.clock_interval_base_ms   = 0;
    state.clock_interval_remainder = 0;
    state.clock_interval_error     = 0;
    state.next_clock_due_ms        = now;
    push_event(VIRTUAL_MIDI_EVENT_STOP, 0, 0, 0, now);
#if defined(MIDI_ENABLE) && SEQ_MIDI_TRANSPORT_SEND
    if (clock_output) MIDI_IF_READY(midi_send_stop(&midi_device));
#endif
}

void virtual_midi_reset(uint32_t now) {
    state.reset_count++;
    state.reset_high    = true;
    state.last_reset_ms = now;
    push_event(VIRTUAL_MIDI_EVENT_RESET_HIGH, 0, 0, 0, now);
    virtual_midi_song_position(0, now);
}

void virtual_midi_clock_tick(uint32_t now, uint32_t step_interval_ms) {
    if (!state.running) {
        return;
    }

    if (step_interval_ms == 0) {
        step_interval_ms = 1;
    }

    state.clock_pulses_remaining   = VIRTUAL_MIDI_CLOCKS_PER_STEP;
    state.clock_interval_base_ms   = step_interval_ms / VIRTUAL_MIDI_CLOCKS_PER_STEP;
    state.clock_interval_remainder = step_interval_ms % VIRTUAL_MIDI_CLOCKS_PER_STEP;
    state.clock_interval_error     = 0;
    state.next_clock_due_ms        = now;

    emit_clock_pulse(now);
    state.clock_pulses_remaining--;
    if (state.clock_pulses_remaining > 0) {
        state.next_clock_due_ms = now;
        schedule_next_clock_interval();
    }
}

void virtual_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t now) {
    push_event(VIRTUAL_MIDI_EVENT_NOTE_ON, channel, note, velocity, now);
#ifdef MIDI_ENABLE
    MIDI_IF_READY(midi_send_noteon(&midi_device, channel, note, velocity));
#endif
}

void virtual_midi_note_off(uint8_t channel, uint8_t note, uint32_t now) {
    push_event(VIRTUAL_MIDI_EVENT_NOTE_OFF, channel, note, 0, now);
#ifdef MIDI_ENABLE
    MIDI_IF_READY(midi_send_noteoff(&midi_device, channel, note, 0));
#endif
}

void virtual_midi_task(uint32_t now) {
    if (state.clock_high && TIMER_DIFF_32(now, state.last_clock_ms) >= VIRTUAL_MIDI_PULSE_MS) {
        state.clock_high = false;
        push_event(VIRTUAL_MIDI_EVENT_CLOCK_LOW, 0, 0, 0, now);
    }
    if (state.reset_high && TIMER_DIFF_32(now, state.last_reset_ms) >= VIRTUAL_MIDI_PULSE_MS) {
        state.reset_high = false;
        push_event(VIRTUAL_MIDI_EVENT_RESET_LOW, 0, 0, 0, now);
    }
    for (uint8_t catchup = 0; clock_output && state.running && catchup < 48 && timer_expired32(now, state.next_clock_due_ms); catchup++) {
        emit_clock_pulse(now);
        uint32_t denominator = (uint32_t)clock_tempo * 24u;
        uint32_t numerator   = 60000u + clock_error;
        state.next_clock_due_ms += numerator / denominator;
        clock_error = numerator % denominator;
    }
}

void virtual_midi_control_change(uint8_t channel, uint8_t control, uint8_t value, uint32_t now) {
    push_event(VIRTUAL_MIDI_EVENT_CC, channel, control, value, now);
#ifdef MIDI_ENABLE
    MIDI_IF_READY(midi_send_cc(&midi_device, channel, control, value));
#endif
}

const virtual_midi_state_t *virtual_midi_state(void) {
    return &state;
}

void virtual_midi_clear_events(void) {
    state.event_count = 0;
}

void virtual_midi_song_position(uint16_t position, uint32_t now) {
    uint8_t lsb = (uint8_t)(position & 0x7F);
    uint8_t msb = (uint8_t)((position >> 7) & 0x7F);
    push_event(VIRTUAL_MIDI_EVENT_SONG_POSITION, lsb, msb, 0, now);
#if defined(MIDI_ENABLE) && SEQ_MIDI_TRANSPORT_SEND
    if (clock_output) MIDI_IF_READY(midi_send_songposition(&midi_device, position));
#endif
}

void virtual_midi_continue(uint32_t now) {
    state.running           = true;
    state.next_clock_due_ms = now;
    clock_error             = 0;
    push_event(VIRTUAL_MIDI_EVENT_CONTINUE, 0, 0, 0, now);
#if defined(MIDI_ENABLE) && SEQ_MIDI_TRANSPORT_SEND
    if (clock_output) MIDI_IF_READY(midi_send_continue(&midi_device));
#endif
}

__attribute__((weak)) void seq_backup_request(uint8_t command, uint8_t object, uint16_t offset, const uint8_t *data, uint8_t size) {}
void                       virtual_midi_receive_sysex(uint16_t start, uint8_t size, const uint8_t *data) {
    static uint8_t  packet[96];
    static uint16_t received;
    if (!start) received = 0;
    if (start != received || (uint32_t)start + size > sizeof(packet)) {
        received = UINT16_MAX;
        return;
    }
    memcpy(packet + start, data, size);
    received += size;
    if (!received || packet[received - 1] != 0xF7) return;
    if (received < 11 || memcmp(packet, "\xF0\x7DSEQ\x01", 6) || ((received - 11u) & 1u)) return;
    uint8_t payload[42];
    uint8_t count = (received - 11u) / 2u;
    for (uint8_t i = 0; i < count; i++) {
        if (packet[10 + 2 * i] > 15 || packet[11 + 2 * i] > 15) return;
        payload[i] = packet[10 + 2 * i] | (packet[11 + 2 * i] << 4);
    }
    seq_backup_request(packet[6], packet[7], packet[8] | ((uint16_t)packet[9] << 7), payload, count);
}
__attribute__((weak)) void virtual_midi_backup_reply(uint8_t command, uint8_t object, uint16_t offset, uint8_t status, const uint8_t *data, uint8_t size) {
#ifdef MIDI_ENABLE
    uint8_t packet[140] = {0xF0, 0x7D, 'S', 'E', 'Q', 1, (uint8_t)(command | 0x40), object, offset & 0x7F, (offset >> 7) & 0x7F, status};
    if (size > 64) return;
    for (uint8_t i = 0; i < size; i++) {
        packet[11 + 2 * i] = data[i] & 15;
        packet[12 + 2 * i] = data[i] >> 4;
    }
    packet[11 + 2 * size] = 0xF7;
    MIDI_IF_READY(midi_send_array(&midi_device, 12 + 2 * size, packet));
#endif
}

#if defined(MIDI_ENABLE) && defined(PROTOCOL_CHIBIOS)
// ChibiOS calls this after keyboard_task. Parse late arrivals instead of
// letting the vendor midi_ep_task consume them without invoking callbacks.
void __wrap_midi_ep_task(void) {
    midi_device_process(&midi_device);
}
#endif
