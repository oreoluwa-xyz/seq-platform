Run `sh tests/seq_engine/run.sh` from this keymap or any directory.

The host harness includes the actual private sequencer engine with small QMK
hardware stubs. It checks capacity rejection and atomic melodic edits, dense
polyphony, polymeter, restart, rational clock timing with swing, MIDI follower
start/stop/continue and timeout, late-step CC points, looped recording,
on-board round trips and overflow protection, corrupt/truncated imports,
large track backup transfers and fragmented SysEx, melodic gestures, modifier
duplication, retained mutes, and empty-track Euclidean generation.

The runner also exercises pool capacities 4/48/144, toggle timing including
32-bit wraparound, and the Python backup codec and preflight checks.
Firmware compilation remains necessary to test QMK integration. USB timing,
flash power-loss behavior, and physical LED/key interactions need hardware.

The runner includes `sh tests/seq_engine/run_timing.sh`, which can also run
separately. It tests the actual engine with simulated scan times: all repeats
across 12 tracks/48 lanes, mixed drum/melodic polymeter, 10/137/400 BPM,
0/25/50% swing, late scans, clock resynchronization, timer rollover, and
single/multi-step incoming clock batches. It also tests the production MIDI
output queue against a controllable USB sink: packet batching, blocked writes,
FIFO wrap/order, overload panic, and reconnect recovery.

Live-edit regressions cover Tab+F2 conversion followed by held-note offbeat
bass entry during a running kick pattern, comparing output before and after
restart. Conversion, clearing, shrinking, growing and doubling retain song
phase under internal/external clocks with swing and non-bar loop lengths;
editing one track leaves other tracks' ratchets playing.

Ratchets occupy one entry per allocated lane. Late scans coalesce overdue
repeats and skip expired steps; new steps never replay already-expired repeats.
The USB queue never waits for the endpoint. Exhaustion stops the whole
transport and sends all-sound-off/all-notes-off, rather than dropping later
lanes silently. Recovering from missed master clocks repositions followers
using MIDI Stop, Song Position and Continue on a timely straight-grid boundary.

The host event log cannot establish physical simultaneity. USB delivery,
receiver support for transport recovery, and audible jitter need hardware
validation. Neither runner flashes firmware.
