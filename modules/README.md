# Interchangeable track modules

**Current firmware:** chord-bank modules were removed for TEMP performance snapshots. Use Euclid/track notes or the recorded-chord editor. Bank files and examples below remain useful as offline harmony drafts for producing recorded notes; bank installation/performance instructions apply only to older firmware.

Module Studio configures one built-in engine per track: Euclid or Chord Bank. The 12-track facade, shared 48 drum lanes, MIDI routing, recorded notes, automation, transport and project storage remain core services. Installing a module preserves track contents. Chord Bank adds a melodic layer if the destination is a drum track, preserving its drum lanes.

Modules import **declarative JSON configuration**, not executable JavaScript or C. Version 1 engine files remain supported. Version 2 composes independent, versioned rhythm and pitch components; start with [Four chord pulses](examples/four-chord-pulses.json). See [Component contracts and evolution](COMPONENTS.md) for supported combinations, resource limits and compatibility. Components are compiled into firmware and share fixed per-track storage; swapping modules does not unload code. New component algorithms still require firmware development and a rebuild.

## Palette pitches and step overrides

New Euclid modules use JSON version 3 with `config.pitchMode: "palette"`; compositions use `pitch.track@2` with `params.pitchMode`. Named palette pitches play on empty Euclidean hits. Recorded notes override them unchanged. Live **Use palette** removes only the chosen step's recorded pitches. **Transpose phrase** retains the older interval-based behavior. Existing v1 modules, pitch.track@1 compositions, and saved projects preserve their sound until you explicitly change modes. The Cmd+/ manual includes both modes and their keyboard/UI controls. See [the versioned contract](COMPONENTS.md#palette-pitch-contract-version-2).

## Open the editor

From this keymap directory, run:

```sh
python3 tools/seq_server.py serve
```

Open `http://localhost:8765/ui/`. This local server also enables the header’s **Firmware** manager. A plain static server still supports editing and MIDI, but not flashing. See [Firmware management](../MANUAL.md#firmware-management). Offline editing, JSON import/export and browser audition work without a keyboard. Drafts are retained per track and engine in this browser. Export files to keep portable copies. Browser audition is a simple synthesized reference sound, not the keyboard's MIDI instrument.

For device transfers, use a browser supporting Web MIDI SysEx, allow MIDI access when requested, and choose the keyboard's input and output ports. Web MIDI requires a secure context (HTTPS or localhost); see [requestMIDIAccess documentation](https://developer.mozilla.org/en-US/docs/Web/API/Navigator/requestMIDIAccess). The keyboard must be running firmware with this module implementation. The editor checks capabilities before enabling transfers.

1. Choose a track and engine. Edit a draft or import a module JSON file.
2. Select **Review installation**. Installation stops playback and takes a full project backup before replacing the selected track's module settings.
3. The editor validates on the device, installs, and reads back the module to verify it. It attempts to restore and verify the preceding complete track if installation or verification fails. A disconnected keyboard may require reconnecting and restoring the backup.
4. Installation affects RAM. Select **Save on-board** to persist the project. If the project exceeds the 3072-byte on-board slot, the previous save remains intact; retain the computer backup.

The recovery backup is downloaded and retained locally in the browser before installation. Use **Download last recovery** if the automatic download was missed. Computer backups use the existing `seq_test_project` format and include module settings. Restore with `tools/seq_backup.py` (run `python3 tools/seq_backup.py --help` for commands). The editor's Read action replaces the selected engine's local draft with device settings.

Names and IDs are file/editor metadata; firmware stores only musical settings. A matching local draft retains its labels when read back. Otherwise, device reads use generic labels. Export your module JSON to preserve names.

## Live keyboard view

The live-state firmware adds a hardware panel showing sequencer mode, running/stopped transport, recording arm, selected track, drum/melodic/hybrid type, installed engine, clock source, position and loop length, the keyboard's visible 16-step window, selected notes and currently sounding notes. The module editor remains a local draft; telemetry does not overwrite its notes, parameters or engine choice. An installed Chord Bank and a local Euclid draft can therefore appear together, explicitly labeled as hardware versus draft.

**Sync track selection** is enabled initially. Selecting a track on the keyboard changes the UI's track; clicking the UI rack selects that root track on the keyboard. Turn sync off to browse drafts independently while still watching the actual keyboard selection. Automatic editor navigation waits while an input or dialog is open. Remote selection is refused during project transfer, held-note gestures, recording capture, or step edit; release/finish the gesture and try again.

Snapshots are requested after a 100 ms idle interval, so the rate is at most roughly 10 Hz, depending on USB/browser latency. This is a state monitor: very brief notes or fast steps may occur between snapshots. Hidden tabs pause polling. Save/install/read operations pause the monitor and wait for its outstanding request; reconnect starts a fresh monitor. Failed replies mark state unavailable and retry; old firmware reports **Live view needs updated firmware** without breaking ordinary module transfers. Monitoring does not stop playback, lock the sequencer, allocate lanes, or write EEPROM.

### Live protocol v1

Command **11**, object/offset/payload zero, returns a 60-byte snapshot. Command **12**, object 0–11 and offset/payload zero, selects a root track and returns the same snapshot. Both are handled before the project-transfer code. Status 2 means selection is temporarily busy; 3 means invalid arguments. Old firmware rejects these commands with status 1. Existing 21-byte module capabilities and project formats remain unchanged.

| Bytes | Value |
| --- | --- |
| 0 | Live version, 1 |
| 1 | Flags: mode active=1, running=2, record armed=4, external clock=8, transfer active=16, drum child view=32 |
| 2–7 | Track index; type (0 drum, 1 melodic, 2 hybrid); engine; zero-based MIDI channel; selected chord slot (255 if none); allocated drum lanes |
| 8–17 | Little-endian uint16 tempo, local step, loop length, visible bar, selected drum-lane mask |
| 18–23 | Little-endian 12-bit masks: melodic tracks, hybrid tracks, Chord Bank tracks |
| 24–39 | 128-bit sounding-note mask, including playback and module audition |
| 40–55 | 128-bit selected-note mask (step edit, drum selection, chord slot, or held/last melodic selection) |
| 56–59 | Little-endian visible-window step mask; selected edit step (65535 if none) |

Note bit N represents MIDI note N. Internal positions and track indices are zero-based; the UI displays them one-based. With external sync, the panel shows External MIDI instead of claiming that the configured internal BPM measures the external clock.

The live build adds 1112 bytes of linked flash code relative to the corrected MIDI build, with `.data` 1476 and `.bss` 53224 unchanged. The engine suite, actual-firmware-parser browser bridge and polling lifecycle tests pass. Simulated browser checks cover the panel, two-way selection, independent draft browsing, disconnect clearing and phone layout. The live build was flashed successfully on 2026-09-08 (135436-byte BIN, device serial 34AA366E3233). The DFU download and leave request completed, but no keyboard MIDI endpoint was present afterward. A normal USB reconnect is required before physical live-state validation.

## Chord Bank on the keyboard

Each bank has 12 slots, each containing zero to eight ascending, unique MIDI pitches. Empty slots are allowed. A chord has velocity 1–127 and duration 1–2048 sequencer steps.

| Gesture | Action |
| --- | --- |
| Alt/Option + `1 2 3 4 5 6 7 8 9 0 - =` | Select and audition slot 1–12 while held |
| Alt/Option + a step key | Add the selected chord at that visible step |
| Alt/Option + encoder | Transpose the bank by semitones, within ±24 |
| Alt/Option + Shift + encoder | Change inversion, within ±3 |
| Armed, running + Alt/Option + bank key | Record the selected chord at the current local track step |

Audition ends on release, track/module change, transport stop, Escape or entering Space controls. LEDs show the selected bank while Alt is held. Playback and audition share note ownership to avoid cutting one another off. Recorded chords retain their pitches when the bank is changed later.

Stamping adds notes without erasing existing events. Duration is capped to the existing track loop; a chord crossing the loop end is split into two segments per voice. This consumes additional events. A chord is rejected in full if capacity is insufficient. A visible step outside the track's current loop is rejected. Recording uses the configured duration, not how long a bank key is held. Transposition/inversion clamps pitches to MIDI 0–127 and collapses duplicates at the boundaries.

Euclid uses the existing keyboard controls. Its module owns the rhythmic filter and palette; those controls and processing are inactive on Chord Bank tracks. Switching engines preserves recorded data but changes how it is played.

## Author a module with Codex or ChatGPT

Use **Write one with AI** in the editor for a prompt containing the current file and constraints. Ask for a complete JSON file, save it, then import it. No account integration is required.

Start with [After hours chord bank](examples/after-hours.json) or [Four on sixteen Euclid](examples/four-on-sixteen.json). The [JSON Schema](module.schema.json) describes structural validation. `ui/module.mjs` is the authoritative browser semantic validator; firmware validates the binary settings independently.

All fields shown in the examples are required. Unknown fields are rejected. IDs contain lowercase letters, numbers, dots or hyphens and start with a letter or number. Names and IDs have 1–64 characters. JSON notes are MIDI integers; the editor also accepts note names, with C4 = MIDI 60.

Additional Euclid rules: fills cannot exceed steps; rotation is below steps; ratchet is a threshold from 0 to min(255, steps); root is a pitch class 0–11; pool contains 1–8 signed offsets; pin is null or an index within the pool; template is null or a 16-bit captured mask with steps at most 16. A template is advanced state preserved on device reads. Chord notes must be sorted ascending and unique. JSON Schema alone cannot express all these cross-field constraints.

## Protocol and compatibility

The existing non-commercial SysEx protocol retains its framing and commands. Read/write object IDs 13–24 address module configuration for tracks 1–12. Track objects 1–12 append an optional module trailer, so full project backups also preserve modules. Old track objects without a trailer restore the Euclid engine and their legacy settings. Existing global/project format version remains unchanged.

Command 10 is a read-only capabilities query: version, track count, chord-slot count, voice limit, little-endian maximum module wire size, engine bitmask, 12 engine-kind bytes, then a little-endian melodic-track bitmask. It does not stop playback or acquire the transfer lock. The other transfer commands keep their existing validation, staging and lock behavior.

Module wire data starts with `M`, version 1 and engine kind (0 Euclid, 1 Chord Bank). Integers are little-endian; signed offsets use two's complement. See `seq_module.c` and `ui/module.mjs` for paired codecs. No dynamic allocation is used in the firmware module layer.

## Validation

Run `sh tests/seq_engine/run.sh` for engine regressions, bounded pools, backup tests, JSON/codec tests and a browser MIDI transport test against the actual firmware parser. Build firmware with the project's normal QMK target. Physical key, LED and USB behavior still require hardware validation after flashing.

The 2026-09-07 module build linked successfully for `keychron/v1_max/ansi_encoder:seq_test`: `.data` 1476 bytes, `.bss` 53224 bytes, reserved stacks 3072 bytes and `.heap` 7760 bytes. Modules add 1808 bytes of static RAM relative to the preceding gap-fix build. Linker headroom is not a runtime peak-usage measurement. Host regressions and the actual-parser MIDI bridge pass; the corrected MIDI receive-task firmware has since been flashed; native capability replies are confirmed, while full keyboard performance validation remains outstanding.

USB MIDI receive fix: the vendor ChibiOS `protocol_post_task()` called a MIDI endpoint drain that discarded late-arriving input. This keymap uses a linker wrapper to run `midi_device_process()` there instead, keeping the change local. The editor also assembles fragmented SysEx replies across MIDI events. Native macOS probing confirmed the flashed module firmware answers capability queries; A simultaneous native monitor also confirmed that Chrome requests reach the corrected firmware and trigger replies. Chrome did not deliver those replies to JavaScript during the observed failure, even through a temporary diagnostic input that combined the fragments. Do not treat a successful native query as proof of a successful browser connection.

## Chrome on macOS: SysEx connection failure

On 2026-09-08, Chrome **152.0.7977.76** failed even after a full restart. An independent browser diagnostic received ordinary MIDI from a temporary native source, but interpreted SysEx payload bytes as running-status notes: the F0/F7 framing was missing. The installed version's `DispatchMidiFromUmpWords()` forwards only the SysEx payload, confirming the same defect as [Chromium's upstream SysEx7 fix](https://chromium.googlesource.com/chromium/src/+/5af1bee2ffd0d5234865b1848a52262b2081ecb2). This happens before the editor receives MIDI events; changing the editor's packet parser cannot repair it.

Use a Chrome release containing that fix. A temporary alternative is to use Chrome's legacy macOS MIDI backend: save unfinished browser work, fully quit Chrome with Cmd+Q, then run this in Terminal:

```sh
open -a "Google Chrome" --args --disable-features=MidiMacUmp http://localhost:8765/ui/
```

Chrome must be fully stopped first; an existing process ignores new startup flags. This changes the MIDI backend for that launch, not MIDI permissions. After starting normally again, Chrome returns to its default backend. Keep the existing firmware; select Keychron V1 Max for both ports and connect. The user confirmed that Module Studio connected successfully after this launch on 2026-09-08. Browser automation could not open internal settings; the user applied the launch flag.

See the editor's [Connection help](../ui/connection-help.html). Native capability replies and the source-level diagnosis are confirmed; the user has confirmed a successful Module Studio handshake. Hardware module installation and readback still need separate validation.

### Live keyboard visual snapshots

Command 13 is read-only and does not acquire the project-transfer lock or stop playback. Offset 0, object 0 captures a 352-byte image and returns its first 64 bytes. Remaining 64-byte pages use the generation token from byte 62 as the object, with offsets 64, 128, 192, 256, 320 (last page 32 bytes). A replaced or older-than-two-second token returns status 2; invalid requests return 3. Clients discard incomplete images. Commands 11 and 12 retain their 60-byte v1 replies.

Visual v2 retains bytes 1–59 from live v1, changes byte 0 to 2, and adds mode at 60 (off, drum, melodic, Euclid, chord, CC, navigation, drum edit, melodic edit, confirmation = 0–9). Byte 61 flags: 1 = 4×4 drums, 2 = Windows, 4 = LED data valid, 8 = OS layout known. Byte 62 is the 1–127 generation; byte 63 is model 1 (V1 Max ANSI). Bytes 64–351 are RGB triplets in 6×16 matrix order; absent LEDs are zero. Colors come from the SNLED27351 SPI driver buffer; disabled RGB yields black. No shared driver code is changed.

The UI requests visual pages only while the keyboard manual is open with Follow keyboard enabled. Unsupported firmware falls back to command 11. Paging stays serialized with MIDI commands; pausing for transfers discards unfinished snapshots. The visual firmware build adds 360 bytes static RAM and 488 bytes to the binary relative to the previous live build; build output is `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-visual.bin` (135,924 bytes). Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233` (SHA-256 `9618b603d1774786cc11b52f23183b3fac5aad1b75e2df7d88925218050bd53a`). After a normal USB reconnect, native CoreMIDI verification received the complete 352-byte visual v2 snapshot: model 1, drum mode, track 1, stopped, OS/LED flags valid, and 23 lit matrix positions. The verifier completed all six pages in about 0.12 seconds. Browser rendering and mode-follow behavior were tested with an isolated fixture; physical mode changes through the connected browser still need a hands-on check.

### Stop/restart save fix (2026-09-08)

Stop schedules an automatic project save. QMK's generic `eeprom_update_block` allocates `read_buf[len]` on the process stack; the 3,072-byte project exceeds this target's 2,048-byte process stack. A local linker wrapper in `seq_eeprom.c` compares at most 64 bytes at a time and performs one full wear-leveling write only if the data differs. Save format, capacity and auto-save behavior are unchanged. The same wrapper protects initialization and explicit saves, without editing shared QMK code.

The ARM compiler reports 88 bytes of stack for the wrapper. The linked `eeconfig_update_user_datablock` branches to that wrapper, and the unsafe generic block-update symbol is absent. Host tests exercise the same helper for repeated keyboard Home start/stop/save cycles, unchanged 3 KiB saves, last-byte changes, partial blocks and zero-length updates. All 15 engine groups, lane-pool variants, backup tests and browser/firmware protocol tests pass.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-restart-fix.bin`, SHA-256 `4260c82f1d8d4ae17d3f604d4ffe9699f07a372a0664ed8bd7bc174c1c26acf6`. Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233` (135,940 bytes). The keyboard reconnected automatically and returned a full visual v2 snapshot: sequencer off, track 1, 81 lit matrix positions. Physical Home start/stop/restart verification remains pending.

### Melodic Euclidean fill fix (2026-09-08)

Melodic Euclid previously tested generated hits only when a stored note began, missing later fills inside sustained notes. Each generated hit now retriggers the source notes spanning that step, with one-step gates and one probability decision for the whole chord. Source rests remain rests; empty tracks still use the palette. Disabling Euclid restores normal sustained playback. LEDs and the live pattern mask show eligible generated hits while the stored-note mask remains available for editing.

The regression reproduced the old failure and now covers sustained chords, fill/rotation changes during playback, chord probability, source rests, palette generation and live masks. All 16 engine groups and the complete lane-pool, backup, module and browser/firmware protocol suites pass. The exact V1 Max target compiles with unchanged static RAM: `.data` 1,476 bytes, `.bss` 53,584 bytes, reserved stacks 3,072 bytes and `.heap` 7,400 bytes.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-euclid-fix.bin`, SHA-256 `12e9cda9bffdcb451cdd83998c647fa6d71097220712ab70f92965bde4be5331`. Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233` (136,196 bytes). The keyboard reconnected automatically and returned the full 352-byte visual v2 snapshot: sequencer off, track 1, 81 lit matrix positions. Physical verification of latched Euclid with Q plus encoder on a melodic track remains pending.


### Stored melodic step notes (2026-09-08)

Command 14 adds a frozen 608-byte v3 snapshot. Bytes 1–351 retain the visual v2 fields; bytes 352–607 contain sixteen 128-pitch bitsets in viewed-step order. Each bit represents a stored source pitch spanning that step, before Euclidean/accumulator playback transformations. Chords include all stored pitches, sustained notes appear throughout their spans, and steps outside the selected track loop are empty. Drum and drum-child views return empty note bitsets. The header, notes and LED buffer belong to the same capture even if the keyboard changes tracks or notes between pages.

Paging uses the same 64-byte requests, generation token and two-second expiry as command 13; offsets are 0 through 576 (last page 32 bytes). Tokens cannot cross snapshot formats. Commands 11–13 retain their existing formats. No project-transfer lock is acquired and playback continues. The UI prefers command 14 for live polling, with one-time fallback to older formats when unsupported. On current firmware, LEDs and notes arrive together even with the manual closed. Interrupted or incomplete snapshots are discarded.

The live panel shows full note names/chords on melodic step cards. The keyboard diagram shows the first note plus a voice count, with all notes in the key description. Older firmware explicitly reports that per-step notes require an update. Source rests remain distinguishable from eligible Euclidean hits.

All 17 engine regression groups, lane pools, backup, module codecs and browser/firmware bridge tests pass. Tests cover chords, MIDI pitches 0/127, sustained notes across bars, frozen pages during edits/track changes, loop bounds, drum-child views, fallback and interrupted snapshots. An isolated browser fixture verified step cards and keyboard labels; physical note editing still needs validation after flashing.

The exact V1 Max target compiles: `.data` 1,476 bytes, `.bss` 53,840 bytes, reserved stacks 3,072 bytes and `.heap` 7,144 bytes. Static RAM increased by 256 bytes relative to the Euclid fix. Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-step-notes.bin`, SHA-256 `cefc6465fd73e890dd0c5145820fe81727a158b33818b6289c1c834c0ae3a94e`. Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233` (136,380 bytes). The keyboard reconnected automatically and a read-only native MIDI query returned all ten pages of the 608-byte v3 snapshot: sequencer off, track 1, model 1 and 81 lit matrix positions. Physical melodic note editing and browser follow behavior remain to be checked by the user.


### First composable module slice (2026-09-08)

Implemented independent stored/Euclidean rhythm and track/chord-bank pitch contracts, with all four pairings. The editor assembles, exports, imports and reads v2 compositions while preserving v1 drafts. Firmware reports supported component versions through command 15; unsupported compositions are rejected before installation. The existing project transfer, recovery backup and verified readback workflow is retained. See [Component contracts](COMPONENTS.md).

All 18 engine regression groups and the full lane-pool, backup, browser codec/protocol and live-state suites pass. Coverage includes all pairings, truncated/future-version rejection without destination mutation, source preservation, one-step chord gates, probability, selection changes, disabled-rhythm fallback, stop cleanup, save/load and v1 restoration. The browser-to-real-parser bridge verifies every composition pairing and project backup round-trips. Isolated browser checks verified composition creation, native numeric edits surviving pitch-source switches, persisted draft reload, v2 authoring prompts, and return to the legacy editor.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-components.bin`, 137,700 bytes; SHA-256 `5141c8f349ba0f0410b971345fbaf89f1eda66713d80e44585da06df7e107176`. The exact V1 Max target links with `.data` 1,476 bytes, `.bss` 54,008 bytes, reserved stacks 3,072 bytes and `.heap` 6,976 bytes. Relative to step-note firmware, this adds 168 bytes static RAM and 1,320 binary bytes. Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233`; dfu-util completed with exit status 0. The keyboard reconnected automatically. Read-only native MIDI verification returned the expected 14-byte component catalog (module v2, two stages, all four component IDs at version 1) and the complete 608-byte live v3 snapshot: drum mode, track 1, stopped, model 1 and 23 lit matrix positions. Physical composed-playback verification remains pending.

### Live Euclid parameters and projected pitches (2026-09-08)

Read-only command 16 adds a frozen 896-byte v4 snapshot. Bytes 1–607 retain the detailed v3 layout; byte 0 is 4. Bytes 608–639 contain rhythm metadata: flags at 608 (1 Euclidean, 2 enabled, 4 captured template, 8 accumulator active, 16 melodic projection available); little-endian steps/fills/rotation at 610/612/614; probability/accent/ratchet at 616/617/618; generated pulse mask for the viewed bar at 620. Other bytes are reserved zero. Bytes 640–895 contain sixteen projected 128-pitch bitsets.

Projection uses eligible source spans, palette offsets and pinning, pitch clamping, or the selected voiced bank chord. It precedes probability, mutes and the stateful accumulator; it never advances RNG, accumulator state, transport or note lifetimes. Stored note bitsets remain available separately. Drum views do not advertise melodic projection. All metadata, source notes, projected notes and LEDs are captured together. Paging remains 64 bytes with a two-second token expiry and cross-format rejection. Commands 11–15 retain their formats. The UI tries command 16 first, falling back only when unsupported.

The hardware view displays live steps, fills, rotation, probability, accent, enabled state, generated pulses per viewed bar and eligible hits per viewed bar. Orbit and step-card note labels use projected pitches when available. Host engine and protocol suites cover palette cycling/pinning, stored chords, clamping, disabled Euclid, bank chords, out-of-loop steps, accumulator non-mutation, frozen settings, paging and interruption. The browser-to-real-parser bridge verifies v4 decoding; an isolated browser fixture verified the readouts and palette labels. Physical knob-edit and melodic playback validation remains to be checked on the keyboard.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-live-rhythm.bin`, 138,396 bytes; SHA-256 `22287023caa04c3eb6fae082428dce49365b97851588533bbe0e186be4c9cd51`. Exact V1 Max build: `.data` 1,476, `.bss` 54,296, reserved stacks 3,072 and `.heap` 6,688 bytes. Static RAM increases by 288 bytes and binary size by 696 bytes versus the components build. Flashed successfully on 2026-09-08 to DFU serial `34AA366E3233`; dfu-util reported File downloaded successfully. The keyboard reconnected automatically and a read-only native CoreMIDI query returned all fourteen pages of the 896-byte v4 snapshot: track 1, sequencer off, model 1, Euclid steps 16, fills 0, rotation 0 and probability 100.

### Picked-note fallback for melodic Euclid (2026-09-09)

Every generated melodic hit now has a pitch source. Recorded notes spanning the step take precedence. If none span it, playback uses held melodic notes, then the last-picked notes, then the palette root when neither selection exists. Existing palette offsets/pinning, one-step gates, probability, mutes and accumulator processing still apply. Turning Euclid off restores recorded timing and rests. Generated notes do not become stored events. This applies to the Euclid instrument and composed Euclidean rhythm with track pitches; chord-bank behavior is unchanged.

Playback and v4 note projection share the same picked-note helper, and the live/LED hit mask includes fallback hits. Tests compare projected note bitsets against actual active MIDI pitches across held/last-selection changes, recorded spans and palette offsets, plus stop cleanup, mute/probability suppression, disabled Euclid and composed modules. All 20 engine regression groups, lane-pool/backup suites and browser-to-firmware protocol tests pass.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-picked-notes.bin`, 138,500 bytes; SHA-256 `7cd3392b0073a8e9773d6ccc999acba2b44d5875b8ada8a45a95e2e15ffc0f06`. Exact V1 Max build succeeds with unchanged static RAM (`.data` 1,476, `.bss` 54,296, reserved stacks 3,072 and linker heap 6,688 bytes); binary increases by 104 bytes. Flashed successfully on 2026-09-09 to DFU serial `34AA366E3233`; dfu-util reported File downloaded successfully and exited 0. The keyboard reconnected automatically and returned a complete 896-byte v4 live snapshot through native CoreMIDI: track 1, sequencer off, model 1, steps 16, fills 0, rotation 0 and probability 100. Hands-on picked-note playback verification remains pending.

### Live palette capture and layout guidance (2026-09-09)

Read-only command 17 returns a frozen 960-byte v5 snapshot, retaining bytes 1–895 of v4. Bytes 896–959 add palette data: flags at 896 (1 palette available, 2 Alt held, 4 Shift held, 8 capture primed, 16 Euclid latched); saved count/pin/root pitch at 897–899; pending count/root pitch at 900–901; eight signed saved offsets at 904, saved reference pitches at 912, pending offsets at 920 and pending reference pitches at 928. Sixteen step-to-palette indices start at 936; 255 means no palette hit. Reserved bytes are zero. The entire image shares one token, including pending capture, saved settings, projected pitches and LEDs. Older commands remain unchanged; the UI falls back when command 17 is unsupported.

The UI separates saved and pending palettes, shows modifier-held state and cycling/pinning, and labels step/orbit pitches with P1–P8 references. Notes are reference pitches at the palette root, while offsets transform the recorded or picked notes. A live native query during diagnosis reported Windows layout (visual flags 14); the stock Windows keymap puts Alt beside Space where Mac keycaps say Command. The UI now explains this instead of assuming keycap labels match the active hardware layout. No modifier mapping is changed.

All 21 engine groups and the full host/protocol suites pass. Gesture regression verifies Alt + Shift + Q/E/T capture without rotation, deduplication, deferred commit, immutable paged capture, cycling, pinning and track isolation. The browser-to-firmware bridge decodes command 17. Browser fixture verification covers saved/pending rows, slot badges, Windows hint and commit transition.

Build: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-palette.bin`, 138,928 bytes; SHA-256 `7b01b8e793ccde6ab0990f66b2df2525add0afa99eb07ea7152ad31812e79d37`. Exact V1 Max build: `.data` 1,476, `.bss` 54,360, reserved stacks 3,072, linker heap 6,624 bytes. This adds 64 bytes static RAM and 428 binary bytes over picked-note firmware. Flashed successfully on 2026-09-09 to DFU serial `34AA366E3233`; dfu-util reported File downloaded successfully. The keyboard reconnected automatically and a read-only native CoreMIDI query returned all fifteen pages of the 960-byte v5 snapshot: track 1, sequencer off, model 1, Windows layout. Palette availability is false on this initial drum view, as expected. Hands-on melodic palette capture and UI verification remain pending.

### Palette pitch build — 2026-09-09

Built exact `keychron/v1_max/ansi_encoder:seq_test`. Image: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-palette-pitches.bin`, 139,576 bytes; SHA-256 `98b436c9c7f559efc51c3af4aea1989e5fee2697df2b71716cfbbc56e8ef10a9`. Data 1,476 B, BSS 54,360 B, reserved stacks 3,072 B: unchanged RAM; binary +648 B from live-palette firmware. Flashed successfully on 2026-09-09 to V1 Max DFU serial `34AA366E3233`; dfu-util exited 0. Native MIDI reconnected and returned the v3 module catalog with `pitch.track@2`.

Validation: 22 engine regression groups, lane-pool/toggle/backup suites, browser-to-real-C protocol bridge (including live pitch commands), module/pitch version migration and persistence, malformed-mode rejection, F-only capture after C selection, exact step overrides, sustained-note splitting and full-capacity rejection. Isolated 1280×720 browser fixture verified Use palette and mode switching; eight capture notes remain visible (pending row bottom 690 px). New firmware presence verified over native MIDI after flashing; musical playback remains to be checked on the instrument.

### Add-notes capture build — 2026-09-09

Capture now defaults to Add notes, with an explicit Replace choice in melodic Euclid controls. It preserves root, order, existing entries and pinning, skips duplicates, and rejects additional pitches once eight slots are occupied. The palette panel is hidden outside the active melodic Euclid control view and the steps expand into its space. Capture choice is a per-track session preference; committed notes use normal project persistence.

Exact V1 Max image: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-palette-append.bin`, 139,760 bytes, SHA-256 `32bb84815ea62fa47caa5f6c1e09deb2aeadd39aee25e8991f24c9719e16cacc`. Data 1,476 B, BSS 54,376 B, stacks 3,072 B; +16 B RAM and +184 B binary versus palette-pitches firmware. Flashed successfully to V1 Max serial `34AA366E3233`; dfu-util exited 0. Native MIDI reconnected and returned a complete 960-byte live v5 snapshot (sequencer off, Track 1 drum view). Add/Replace capture on hardware remains to be exercised in melodic Euclid controls.

Validation: 23 engine regression groups, existing pool/toggle/backup suites, real firmware protocol bridge including capture-preference commands, append ordering, duplicate/full handling, pin preservation, replace behavior, capture freeze, track isolation, and UI visibility predicates. Browser fixtures verified Add/Replace controls, hidden palette with a two-column expanded step view, and eight pending notes fitting at 1280×720 (bottom 618 px). An editing-script stylesheet overwrite was recovered from the earlier workspace command history, checked against the pre-edit CSS rules, and visually verified before delivery.


## Palette Replace shortcut and starter behavior (2026-09-09)

Ctrl + Shift + Alt/Option + note keys forces Replace for one capture. On Windows layout with Mac keycaps, use the Command-labelled key beside Space for Alt. The first committed capture replaces an untouched starter palette; later Add captures preserve authored notes, including an intentional C. Project saves retain starter provenance. Legacy saves and imported modules remain authored and can be replaced explicitly. Cmd+/ manual, component help, live palette guidance, and keyboard guide document the gesture.

Validated: 25 host engine regression groups, pool/toggle/backup suites, real C/JS protocol bridge, UI tests, and exact V1 Max compile. Isolated browser preview verified starter guidance, Ctrl help, and pending F4/A4 replacing C4 with controls disabled during capture.

Prepared image (not yet flashed): `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-palette-replace.bin`, 140,008 bytes, SHA-256 `45b87036377579946f286d91cdaac97e86ef245673a7c3297e0b8c23836718e8`. Data 1,476 B, BSS 54,400 B, stacks 3,072 B, heap 6,584 B. Compared with palette-append: +248 binary bytes, +24 RAM bytes. Untouched track project trailers add 3 bytes each; older firmware cannot decode those trailers. Hardware gesture validation awaits flashing.

Flash verification: palette-replace image successfully downloaded to V1 Max serial `34AA366E3233` on 2026-09-09. Native MIDI input/output reappeared and command 17 returned a complete 960-byte v5 snapshot (sequencer off, Track 1 drum view). Physical Replace/first-Add gestures still require user exercise in melodic Euclid controls.


## Clear palette shortcut (2026-09-09)

Shift + Alt/Option + Delete clears the melodic Euclid palette immediately and cancels pending capture. Recorded spans and rhythm remain; empty generated hits stay silent until new notes are captured. Empty palettes survive project/module backup and on-board save/load, and are not reseeded when Euclid is toggled. Live UI, keyboard guide and Cmd+/ manual describe the gesture.

Validated with 26 engine regression groups plus pool/toggle/backup suites, C/JS protocol round-trips including empty module/project payloads, UI codec tests, and an isolated browser fixture showing all eight empty slots and Clear guidance. Exact V1 Max build passed. Prepared (not flashed): `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-palette-clear.bin`, 140,176 bytes, SHA-256 `e80956d7b78871068bef536f55219aeef85ec45bcd5fa8a014a57d7c6b21e6b3`. Data 1,476 B, BSS 54,400 B, stacks 3,072 B, heap 6,584 B: unchanged RAM, +168 binary bytes versus palette-replace. Physical gesture validation awaits flash. Older firmware rejects empty-pool modules; use the updated firmware to restore them.


## Ordered live rhythm controls (2026-09-09)

Direct hardware inputs now expose Steps, Fills, Rotation, Probability, Accent, and Enabled. Shortcut order is Q/W/E/R/T for the first five; bracket keys replace the old R/T rotation nudges, and F retains ratchet selection. Accent is a dedicated knob parameter and is shown as MIDI velocity points rather than percent. Pulses and eligible hits remain computed readouts. Cmd+/ manual, component help and keyboard model reflect the new order; connected older firmware retains its actual labels and read-only values. Includes the pending Clear Palette feature.

Validated: 28 engine groups, pool/toggle/backup suites, C/JS command 19 bridge with stale-edit rejection, UI codecs and keyboard mapping, and exact V1 Max build. Browser fixture with 150 ms snapshots verified focus retention, Enter applying Fills 7→9, Escape cancelling Probability 35→100, and layout. Prepared, not flashed: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-live-controls.bin`, 140,728 bytes, SHA-256 `6ffc2941119332acb6a66aff6d7b68e7ffcb14d61293c480289a58147773d553`. Data 1,476 B, BSS 54,400 B, stacks 3,072 B, heap 6,584 B. RAM unchanged, binary +552 B versus palette-clear. Physical control validation awaits flashing.

Flash verification (2026-09-10): live-controls image successfully downloaded to V1 Max serial `34AA366E3233`. Native MIDI input/output reconnected; a complete 960-byte snapshot advertised byte 609 = 3 (live rhythm edits and ordered Q/W/E/R/T controls). Hardware gesture testing remains manual.


## Global scale foundation (2026-09-10)

Ctrl + Space opens root/mode selection: melodic note keys choose root, A/S/D/F/G/H/J select Ionian/Dorian/Phrygian/Lydian/Mixolydian/Aeolian/Locrian, K/L select harmonic minor/major and semicolon/apostrophe select major/minor pentatonic. Enter applies, Esc cancels, 0 toggles enable. Physical LEDs and the virtual keyboard show root, scale membership and selected mode. The UI adds global visual selection, browser audition respecting Sound mute, and Follow/Local/Unconstrained plus snap direction per track. Pending hardware selections are read-only in the browser. Project transfers cancel pending scale editing.

The pure `scale.quantizer@1` foundation runs after melodic pitch generation and accumulator; stored notes remain intact and drums/chord-bank voicings bypass it. Context and host track settings persist with optional global/track project extensions. Arbitrary module transform ordering remains future work. The UI now offers harmony authoring providers that capture a melody and render an independent chord track; see the manual’s Create a complementary chord track section. Fixed Ctrl forwarding/tracking so physical Ctrl combinations register and release correctly in sequencer mode, including the earlier Replace shortcut. Removed a paused live-state read from the rhythm editor that could temporarily blank the live view; normal polling refreshes after edits.

Validation: 30 engine regression groups, all 7 modes at every root/MIDI note/direction, collapsed-voice ownership, sounding-note cleanup on scale changes, local/off/bank behavior, keyboard apply/cancel with actual Ctrl key events, project and on-board persistence, malformed and stale rejection, transfer locking, existing pool/toggle/backup/UI suites, and real C/JS global-scale protocol round-trip. Browser fixture verified D Dorian highlights/payload, local F Lydian payload, and disabled editing during hardware selection. Exact V1 Max compile passed.

Prepared, not flashed: `/tmp/seq-module-build/keychron_v1_max_ansi_encoder_seq_test-global-scale.bin`, 142,872 bytes, SHA-256 `f6c84b16e5d061e24f070c302f4a67f901ff1426d258b3186dad1c014928d0c8`. Data 1,476 B, BSS 54,456 B, stacks 3,072 B, heap 6,528 B: +56 static RAM and +2,144 binary bytes versus live-controls. Physical scale gestures and musical audition await flashing.

Drum Euclid can also be configured independently per lane from the keyboard or live hardware controls. Those overrides are project data, saved in on-board projects and computer backups. Module JSON is still track-level configuration: importing it updates the underlying track generator while existing lane overrides remain intact. Use the live controls to edit a lane.

## CC Automation / Manual (2026-10-07)

CC mode uses Shift + PgDn (single tap, either Shift) or T to toggle the target track between Automation (default) and Manual. Manual bypasses CC event recording and playback without deleting points; live MIDI CC output and temporary-gesture reversion still work. The choice stays active outside CC mode and persists through project save/restore. The live dock reports the mode and shortcut. Command 21 retains its 192-byte shape: byte 160 advertises automation-mode version 1, and byte 161 is 0 for Automation or 1 for Manual. Byte 162 bit 0 advertises the Shift + PgDn shortcut (2026.10.12.1); the UI retains the T hint for earlier automation-mode firmware. Older firmware has zero at byte 160; the UI does not advertise the new shortcut there.

Track project objects optionally include `A, 1, mode` immediately after CC events and before the lane rhythm extension. Omitted extensions restore Automation. Writers omit the extension in Automation mode, retaining existing default-object compatibility. Older firmware cannot import objects containing the new Manual extension. Chord-workshop edits preserve it.
