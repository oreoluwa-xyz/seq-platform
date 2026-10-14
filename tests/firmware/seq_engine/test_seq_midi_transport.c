#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "seq_midi_transport.h"

static bool connected = true;
static unsigned budget, calls, received;
static uint8_t output[SEQ_MIDI_TX_CAPACITY * 3][4];

bool seq_midi_usb_connected(void) {
    return connected;
}

bool seq_midi_usb_write(const uint8_t *data, uint8_t size) {
    calls++;
    assert(size > 0 && size <= 64 && !(size % 4));
    if (!budget) return false;
    budget--;
    assert(received + size / 4 <= sizeof(output) / sizeof(output[0]));
    memcpy(output[received], data, size);
    received += size / 4;
    return true;
}

static void enqueue(unsigned index) {
    const uint8_t packet[4] = {9, 0x90 | (index % 16), index % 128, (index / 128) % 128};
    seq_midi_tx_enqueue(packet);
}

int main(void) {
    seq_midi_tx_init();
    for (unsigned i = 0; i < SEQ_MIDI_TX_CAPACITY; i++) enqueue(i);
    seq_midi_tx_flush();
    assert(calls == 1 && received == 0 && !seq_midi_tx_fault());
    budget = 2;
    seq_midi_tx_flush();
    assert(received == 32);
    for (unsigned i = SEQ_MIDI_TX_CAPACITY; i < SEQ_MIDI_TX_CAPACITY + 32; i++) enqueue(i);
    budget = SEQ_MIDI_TX_CAPACITY;
    seq_midi_tx_flush();
    assert(received == SEQ_MIDI_TX_CAPACITY + 32 && !seq_midi_tx_fault());
    for (unsigned i = 0; i < received; i++) {
        assert(output[i][0] == 9 && output[i][1] == (0x90 | (i % 16)));
        assert(output[i][2] == i % 128 && output[i][3] == (i / 128) % 128);
    }

    received = 0;
    budget = SEQ_MIDI_TX_CAPACITY;
    for (unsigned i = 0; i < SEQ_MIDI_TX_CAPACITY * 2; i++) enqueue(i);
    seq_midi_tx_flush();
    assert(received == SEQ_MIDI_TX_CAPACITY * 2 && !seq_midi_tx_fault());

    budget = 0;
    for (unsigned i = 0; i <= SEQ_MIDI_TX_CAPACITY; i++) enqueue(i);
    assert(seq_midi_tx_fault());
    enqueue(99); /* Failed transport cannot emit more note-ons. */
    seq_midi_tx_resume();
    assert(seq_midi_tx_fault()); /* Panic must drain first. */
    received = 0;
    budget = SEQ_MIDI_TX_CAPACITY;
    seq_midi_tx_flush();
    assert(received == 33 && output[32][1] == 0xFC);
    for (unsigned channel = 0; channel < 16; channel++) {
        assert(output[channel * 2][1] == (0xB0 | channel));
        assert(output[channel * 2][2] == 120);
        assert(output[channel * 2 + 1][2] == 123);
    }
    seq_midi_tx_resume();
    assert(!seq_midi_tx_fault());

    seq_midi_tx_set_clock_output(false);
    enqueue(1);
    connected = false;
    seq_midi_tx_flush();
    assert(seq_midi_tx_fault());
    seq_midi_tx_resume();
    assert(seq_midi_tx_fault());
    connected = true;
    received = 0;
    seq_midi_tx_flush();
    assert(received == 32); /* Follower never sends transport to its master. */
    seq_midi_tx_resume();
    assert(!seq_midi_tx_fault());
    puts("MIDI transport: batching, nonblocking retry, queue wrap, order, overload panic and reconnect passed.");
}
