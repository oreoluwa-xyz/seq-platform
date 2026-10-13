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
