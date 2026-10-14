#include "seq_midi_transport.h"
#include <string.h>

_Static_assert(SEQ_MIDI_TX_CAPACITY >= 33 && SEQ_MIDI_TX_CAPACITY <= 16384, "Invalid MIDI queue capacity");

static uint8_t packets[SEQ_MIDI_TX_CAPACITY][4];
static uint16_t head, count;
static bool failed;
static bool clock_output = true;

void seq_midi_tx_set_clock_output(bool enabled) {
    clock_output = enabled;
}

void seq_midi_tx_init(void) {
    head = count = 0;
    failed = false;
}

static void seq_midi_tx_panic(void) {
    /* Overload is a transport failure for the whole sequencer, never silent
     * loss of later lanes. Discard stale work and release all receiver voices. */
    head = count = 0;
    failed = true;
    for (uint8_t channel = 0; channel < 16; channel++) {
        uint8_t packet[4] = {0x0B, 0xB0 | channel, 120, 0};
        memcpy(packets[count++], packet, 4);
        packet[2] = 123;
        memcpy(packets[count++], packet, 4);
    }
    if (clock_output) {
        const uint8_t stop[4] = {0x0F, 0xFC, 0, 0};
        memcpy(packets[count++], stop, 4);
    }
}

void seq_midi_tx_enqueue(const uint8_t packet[4]) {
    if (failed) return;
    if (count == SEQ_MIDI_TX_CAPACITY) seq_midi_tx_flush();
    if (failed) return;
    if (count == SEQ_MIDI_TX_CAPACITY) {
        seq_midi_tx_panic();
        return;
    }
    memcpy(packets[(head + count) % SEQ_MIDI_TX_CAPACITY], packet, 4);
    count++;
}

void seq_midi_tx_flush(void) {
    if (!seq_midi_usb_connected()) {
        if (count && !failed) seq_midi_tx_panic();
        return;
    }
    /* At most one queue's worth per call; a congested endpoint returns
     * immediately and retains all unsent events in their original order. */
    while (count) {
        uint16_t batch = count < 16u ? count : 16u;
        if (batch > SEQ_MIDI_TX_CAPACITY - head) batch = SEQ_MIDI_TX_CAPACITY - head;
        if (!seq_midi_usb_write(packets[head], batch * 4u)) return;
        head = (head + batch) % SEQ_MIDI_TX_CAPACITY;
        count -= batch;
    }
}

void seq_midi_tx_resume(void) {
    if (!count && seq_midi_usb_connected()) failed = false;
}

bool seq_midi_tx_fault(void) {
    return failed;
}
