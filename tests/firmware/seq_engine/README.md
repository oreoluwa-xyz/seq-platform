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

Project restore regressions redistribute a full chord-recipe pool, verify byte-exact recorded-chord restoration, and recover after a failed later object. Backup preflight includes the combined recipe limit and refuses older incomplete validation replies before clear. Command 25 regressions compare measured sizes with real serialization, including stale recipes, and check playback, MIDI output, EEPROM writes, edit rollback, transfer scratch, and active TEMP preservation.

Global-loop regressions exercise Enter + encoder across mixed track lengths, byte-identical project encoding, chord/CC/lane preservation, loop wraps, sustained-note re-entry, ratchet cancellation, external-clock phase, TEMP interactions, bounds, busy-state guards and existing Enter actions. The shortcut is a playback overlay, separate from destructive track resizing.


Chord palette regressions cover deferred pinning, slot/all quality edits, modifier and release-order cancellation, Euclid+C toggling, fixed and Auto playback, global/local/off scale contexts, recorded overrides and printing, versioned codecs, malformed/truncated data, EEPROM/TEMP restoration, removal alignment, telemetry, and stale UI writes. UI tests round-trip the new contracts through the real C protocol bridge and verify quality labels and scale projections.


Variation regressions cover one accumulator advance per chord onset, sustained releases, MIDI-range clamping, probability skips, reverse/rotate/shuffle/random selection, 128-entry permutation coverage, variable-size chord swaps, individual unchanged gates, deterministic scale/palette mutation, read-only projections, stale/all-track edits, queue cancellation and next-bar application, TEMP and EEPROM round trips, malformed extension rejection, C/JS protocol round trips, and chord-workshop extension preservation. The desktop smoke suite checks stable focused fields, stale drafts, scope/timing payloads, queue labels, scale context, track switching, and phone width.
