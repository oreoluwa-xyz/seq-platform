#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef SEQ_MIDI_TX_CAPACITY
#    define SEQ_MIDI_TX_CAPACITY 512
#endif

void seq_midi_tx_init(void);
void seq_midi_tx_enqueue(const uint8_t packet[4]);
void seq_midi_tx_flush(void);
void seq_midi_tx_resume(void);
void seq_midi_tx_set_clock_output(bool enabled);
bool seq_midi_tx_fault(void);

/* Backend accepts an entire USB packet or nothing, without waiting. */
bool seq_midi_usb_write(const uint8_t *data, uint8_t size);
bool seq_midi_usb_connected(void);
