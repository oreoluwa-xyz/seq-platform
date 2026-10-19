**Sequencer Manual**

**Firmware version and control changes**

This guide covers the current keyboard controls: Space-based navigation and track length, whole-track mute, chord palettes, and the chord-aware accumulator. These are the controls on the updated keyboard.

- **Space + F** toggles Follow. **Space + =** doubles with content; add either Shift for an empty extension.
- **Ctrl + Space + knob** changes actual track length by one step; add Shift for 16. Shortening trims content beyond the new end.
- **Shift + Space + F1–F12** toggles a whole track while preserving individual lane mutes on unmute.
- **Ctrl + Space** and **Ctrl + Shift + Space** open Scale and Harmony on Space release if you did not turn the knob.
- On the earlier **v0.1.11** firmware, **Space + =** is Follow; the new Space doubling, knob-length and whole-track mute gestures are unavailable. Its Tab-based duplication shortcuts remain the alternative. Updating this webpage does not update your firmware.

## Project capacity and save status

Open **Capacity** beside the live status (in the desktop performance toolbar when compact). It shows whether the keyboard has unsaved changes or a confirmed on-board save, plus drum lanes, selected-track notes, shared chord recipes, CC automation, selected-lane step edits, and encoded storage sizes. Readings update about once a second and do not stop playback or save to flash. Disconnection hides old values; older firmware shows an update message.

**On-board save** and **TEMP snapshot** are separate limits. A playable project can exceed either limit. TEMP says **ready** only when the current reading fits and no gesture blocks entry; the keyboard still checks again when you capture. While TEMP is active, the panel also shows the size of the original retained drop. Keeping TEMP edits does not immediately save them. A failed oversized save preserves the previous on-board save; use a computer backup for projects that do not fit. These indicators do not claim a computer backup exists.

Full-project restoration now clears the shared chord recipe pool as well as notes, drum lanes and automation. The updated backup utility checks the combined recipe budget before clearing the keyboard; update firmware if it reports that complete chord-capacity preflight is unavailable.

## TEMP performance mode

**Momentary global loop:** hold **Enter** and turn the knob. Each detent changes the playback limit by one step, starting from the focused track's original length. For a 16-step track, turn left eleven detents for a 5-step loop across the project. Tracks already shorter than the limit retain their own length. Release Enter to restore every track's original length at the current transport position. The keyboard shows the step count with Enter orange; the live toolbar shows **Global loop · 5 steps**.

This shortcut preserves all recorded notes, chord recipes, drum hits, lane rhythms and CC automation; it never trims or saves them, and it does not require a TEMP snapshot. It works with internal or external clock. Finish held edits and disarm recording first. Starting another action-key gesture, stopping transport, or exiting the sequencer ends the loop. **Ctrl + knob** retains tempo priority unless Space is held for track length. **Enter + Esc**, **Space + Enter**, and Enter in the scale editor keep their existing actions. The loop limit can also be used during TEMP; releasing Enter leaves that snapshot intact.

Hold **Enter** and tap **Esc** to capture a restore point for all tracks. Esc turns **purple** only after the complete snapshot fits. Release Enter, make your buildup changes, then tap **Esc** to restore the drop and leave TEMP. Playback stays on the current transport position; it does not restart. **Shift + Esc** queues restoration for the start of the next global 16-step bar. Esc blinks purple while queued. Press **Shift + Esc again** to cancel the queued restore and keep TEMP active (solid purple), extending the buildup. Press it again when ready to queue the next bar. Esc alone restores immediately, and Enter + Esc keeps the edits and cancels the queued restore. When stopped, Shift + Esc restores immediately; stopping playback with a restore queued also restores it.

TEMP restores recorded notes and chords, drum hits and modifiers, mutes, loop lengths, Euclid palettes/settings, scales, accumulator state, tempo/swing, MIDI routing, CC values and automation. It restores the original selected track/view as well. The chord editor and ordinary polyphonic notes remain available.

Finish held-key edits and disarm recording before entering TEMP. Recording and editing work normally after entry. To keep your changes, hold **Enter + tap Esc again**: TEMP exits, the purple light clears, and the current edits become the normal sequence. This discards the restore point without restarting playback or immediately saving to flash; normal saving resumes. A later TEMP captures this new version. Leaving sequencer mode also rolls back. Saves and project transfers are blocked during TEMP; live monitoring and edits remain available. External synthesizer changes made outside the keyboard cannot be restored.

The snapshot has a bounded 7.25 KiB buffer shared with USB transfers. If the project is too large or an edit/transfer is busy, Esc gives the existing magenta capacity feedback and TEMP does not activate. There is no partial snapshot. TEMP is RAM-only and does not survive power-off.

Chord-bank modules have been removed to make room for TEMP. Recorded chords and polyphonic notes are retained. Older saved tracks containing banks migrate to recorded-note playback; bank-generated playback and unprinted bank slots are no longer available. New bank installations are rejected. Studio can still use offline bank drafts to create recorded chord progressions.

This manual describes the current feature set for the seq_test sequencer keymap on the Keychron V1 Max (ANSI encoder).

**Key Groups**
| Group | Keys | Notes |
| --- | --- | --- |
| Track keys | Top-row media keys: `Brightness Down`, `Brightness Up`, `Mission Control`, `Launchpad`, `RGB Down`, `RGB Up`, `Prev`, `Play/Pause`, `Next`, `Mute`, `Vol Down`, `Vol Up` | These align with the F1-F12 positions on the board. |
| Step keys (default layout) | `A S D F G H J K` and `Z X C V B N M ,` | 16-step grid in two rows. |
| Step keys (4x4 layout) | `1 2 3 4`, `Q W E R`, `A S D F`, `Z X C V` | Optional grid layout. |
| Melodic note keys | `Q 2 W 3 E R 5 T 6 Y 7 U` | Sharps are on the number row. |
| Melodic control keys | `1` (octave down), `4` (rest/tie), `8` (octave up) | Used in melodic mode. |
| Sequencer switch | `Space` | Navigation and layout controls. |
| Euclid modifier | `Alt/Option` | Hold for Euclid controls, double-tap to latch, tap once to exit. |
| CC modifier | `Cmd/Win` (GUI) | Hold for CC mode, double-tap to latch. |
| CC toggle key | `\` | Toggle latched CC mode. |

**Entering And Exiting**
1. Double-tap `PgUp` to enter or exit the sequencer.
2. Double-tap `Esc` to exit while the sequencer is active.
3. Single-tap `Esc` backs out of active edit modes without leaving the sequencer.
4. The switch key (`Space`) is gated while the sequencer is active so the host app does not receive stray presses.

**Tracks And Types**
1. Single-tap a track key to focus it.
2. Drum tracks default to the sibling lane that matches the track index.
3. Hold a track key and tap another track key to jump directly to that drum lane.
4. Hold `Tab` and tap a track to set it to melodic. Hold `Caps` and tap a track to set it to drum.
5. If you change the active track type, confirm with `Y` or cancel with `N`.
6. Hold a track and tap `,` or `.` to change its MIDI channel (0-15).
7. Hold `Shift` while pressing `,` or `.` to retune the active drum lane (note 0-127).
8. Hold a track key for about 220 ms, type digits `1`-`128` to set its bar count, then release.
9. While holding a track key, tap `Backspace` to switch length entry to raw steps, then type digits.
10. `Space` + `=` doubles the length and copies content. Add either Shift for a blank extension. The older `Tab` + `=` / both-Shifts shortcuts remain available.
11. Hold `Del` and tap a track key to clear it.

**Step Layouts**
1. Default step layout uses the two-row grid listed above.
2. Hold `Space`, tap `S`, then tap `~` to restore the default layout.
3. Hold `Space`, tap `S`, then tap `1` to switch to the 4x4 grid layout.

**Melodic Workflow**
1. Hold note keys to form a chord, then tap a step to place it.
2. Release all notes and tap a step to erase everything there. Hold specific notes to remove just those pitches.
3. Double-tap a step while holding a chord to latch its start point. Release, move to another bar, then tap the final step to stretch the chord.
4. Double-tap any populated step to enter step edit.
5. In step edit, `I` `O` `P` select length, pitch, or velocity for the encoder.
6. In step edit, `[` and `]` step the accumulator, `;` cycles run length, `'` toggles randomness, and `/` toggles the accumulator.
7. `1` lowers the octave, `8` raises it. `4` toggles rest/tie (Shift + `4` arms a tie).
8. Hold `~` to nudge octaves upward. Hold `Shift` + `~` to nudge downward. Release to return to the base octave.

**Drum Workflow**
1. Each drum track contains up to 12 sibling sequences (one per note lane).
2. Hold the active drum track and tap a track key to select that lane. Tap the track by itself to return to the lane matching its index.
3. Toggle steps on/off with the current step layout keys.
4. Hold additional track keys while tapping steps to place multiple drum lanes at once.
5. Double-tap a filled drum step to open its editor. `I` `O` `P` select velocity, ratchet count, or spacing for the encoder.
6. Delete a step by tapping it while its lane is selected.
7. Clear the track’s selected lane by holding `Del` and tapping its track key. Hold both Shift keys as well to clear the whole drum track.
8. `Space` + `=` doubles the track’s exact step length, copying all its lanes, step modifiers, melodic events, and CC points into the new region. Add either Shift to leave the new region blank. An edit that exceeds event or modifier capacity is rejected.

**Euclid Mode**
1. Hold `Alt/Option` to peek at Euclid controls for the focused track. Double-tap to latch the session; tap once to exit.
2. Tap `Q` Steps, `W` Fills, `E` Rotation, `R` Probability, or `T` Accent, then turn the encoder. Hold `Shift` for accelerated changes.
3. Tap `[` or `]` to rotate by -1 or +1 without leaving performance grip.
4. Tap `T` and turn the encoder for Accent (0–48 MIDI velocity points, alternating boost/reduction). Shift accelerates changes; Shift + W + knob remains an alternate Accent gesture.
5. `Alt/Option` + `Tab` snapshots the current 16-step window into a fresh Euclid template.
6. Hold `Alt/Option` + `Shift` and tap melodic keys to capture notes, then release to commit. **Add notes** (default) keeps existing entries and appends unique notes into free slots, up to 8 total. Hold **Ctrl + Shift + Alt/Option** before tapping notes for a dedicated Replace capture, or choose **Replace** in the live palette. Your first capture automatically replaces the untouched starter note.
7. Hold `Alt/Option` to display the stored palette on the melodic key row.
8. `Alt/Option` + `1` to `8` pins playback to a palette slot. `Alt/Option` + `0` returns to cycling.
9. `Alt/Option` + `R` adjusts probability with the encoder (Shift = +/-10 percent).
10. `Alt/Option` + `F` adjusts the auto-ratchet threshold. Set it to `0` to disable auto-ratchets.
11. On melodic tracks, **Palette notes** cycles through the actual named pitches at Euclidean hits. Recorded notes/chords override the palette wherever they span a step, without additional palette transposition. In the live UI, **Use palette** removes the recorded pitches only on that step; sustained notes on neighboring steps remain. **Transpose phrase** adds palette offsets to recorded notes, or held/last-picked notes on empty steps (palette root if nothing is picked). Older saved modules retain Transpose phrase. Both modes use one-step gates; probability applies to the whole chord. Disabling Euclid restores sustained recorded-note playback.
12. The step LEDs and live step view show every generated hit, including hits that use picked-note fallback. Probability and mutes can still suppress those hits.

Euclid settings are independent for each drum lane and each melodic track. While latched, tap F1–F12 to change track. To choose a drum lane, hold the current track’s F key, tap the lane’s F key, then release both. The live controls name the lane being edited. Steps, Fills, Rotation, Probability, Accent and auto-ratchet all belong to that lane. The track’s loop length is the upper limit for generator Steps.

To clear generated hits, set **W / Fills to 0**. This keeps recorded hits and other settings. To reset the selected lane’s Euclid settings (or the melodic track’s generator), hold **Delete and double-tap Alt/Option while Euclid is latched**. **Delete + track key** clears both recorded hits and Euclid for the selected drum lane; holding both Shifts with that gesture clears the entire drum track. Other lanes and tracks keep playing.

Older projects retain their original shared-generator routing when loaded. Editing a drum lane gives it its own generator without changing its neighbours. Full project backups and on-board saves preserve lane settings; module JSON remains track-level configuration and does not include lane overrides.

**CC Mode**
1. Hold `Cmd/Win` for momentary CC mode. Double-tap `Cmd/Win` to latch CC mode.
2. Press `\` to toggle latched CC mode.
3. Hold `Shift` while entering CC mode with `Cmd/Win` to make CC changes temporary. Leaving the mode restores the previous CC values.
4. In CC mode, tap a track key to target that track. Hold a track key to temporarily target it while you tweak.
5. Default CC mappings use 16 slots: `1 2 3 4 5 6 7 8 9 0 - = Q W E R` mapped to CC 16-31, default value 64, channel follows the track.
6. Press a CC key and turn the encoder to change its value (0-127).
7. Double-tap a CC key for a temporary performance tweak that reverts on release.
8. **Hold either Shift and tap PgDn once in CC mode to toggle Automation / Manual for the target track.** T remains an alternate shortcut. Automation is the default: while transport runs, changes record at the current step and saved movements play back. Manual sends your tweaks normally but pauses both recording and playback; existing CC points are kept. Repeat Shift + PgDn to resume from the next scheduled automation point. The choice remains when leaving CC mode and is included in project saves/backups. Other tracks are unaffected. Temporary gestures still revert as usual.
9. With CC live-state firmware, latching CC mode opens a compact panel pinned to the bottom of the UI. It shows Automation or Manual status, the automation shortcut, the actual CC target track, each mapped key, CC number, MIDI channel (1–16), value, and held/selected controller. Hold a labelled key while turning the knob: ±3 per turn, or ±15 with Shift. Temporary gestures are marked. Minimize keeps a compact header; unlatching closes the panel. Values describe the keyboard’s outgoing CC state, not feedback from your DAW.

**Track length with the knob**

Hold **Ctrl + Space** and turn the knob: clockwise adds one step, counterclockwise removes one. Add **either Shift** for 16-step increments. This changes the current track’s actual loop length (1–2048 steps); it works with either internal or external clock. Expansion adds empty space. Shortening trims notes, drum hits/modifiers and CC automation beyond the new end; expanding again does not restore them. Finish held edits and disarm recording first.

**Space + =** doubles the exact length and repeats existing content, including melodic events, drum lanes/modifiers and CC points. **Space + Shift + =** doubles with an empty extension. Capacity failures reject the entire duplication. Space + knob still controls swing, and Ctrl + knob without Space controls tempo. Tap Ctrl + Space or Ctrl + Shift + Space without turning the knob to open Scale or Harmony on Space release.

**Navigation And Follow**
1. Hold `Space` to access navigation controls.
2. Tap digits to jump to bars (0 = 10). Rapid taps multiply. Hold `Shift` while tapping to double the target.
3. While holding `Space`, press `F` to toggle follow mode. Follow keeps the view synced to the playhead.
4. Bar LEDs show a binary readout of the viewed bar and playhead. The live UI mirrors the hardware’s viewed bar and labels the absolute steps (for example, bar 2 shows steps 17–32). If playback reaches a different bar while the view remains held, the UI shows the playing bar and the Space + F follow shortcut.

**Mutes**

Hold **Shift + Space** and tap **F1–F12 once** to mute/unmute that entire track, including every drum lane and any melodic layer. Either Shift works. This does not change the selected track or lane. Sounding notes and queued drum repeats stop immediately; unmuting rejoins playback on the next step with individual lane/layer mutes preserved. You can still edit individual lane mutes while the track is muted. The live track indicator shows it fully muted while the track mute is active.

Track mutes are saved with the project, included in computer backups and restored by TEMP. Older projects load with track mutes off. Backups with an active track mute need firmware that supports this setting; older firmware rejects the new field. CC automation retains its existing behavior.

1. While holding `Space`, double-tap a melodic track key to mute or unmute it.
2. For drum tracks, hold `Space`, tap the drum track once, then tap a track key for the lane index you want to mute or unmute.
3. While holding `Space`, tap `Backspace` to clear all track, melodic-layer and lane mutes.

**Recording**
1. Double-tap `PgDn` to arm or disarm note recording. In CC mode, **Shift + PgDn** toggles CC Automation / Manual instead, without changing note-record arming.
2. The `PgDn` LED blinks red when armed and solid red when the transport is running.
3. While armed and running, hold melodic notes to record sustained segments. Release to end the segment.
4. While armed and running, hold drum lane keys to stamp steps at the playhead.

**Transport And Tempo**
1. `Home` toggles transport. When starting, it resets the playhead to the beginning and sends MIDI start. When stopping, it sends MIDI stop.
2. Hold **Ctrl + turn the knob** for global tempo in every active sequencer mode, including Chord, Euclid, CC and step editing. Each detent changes 1 BPM; **Ctrl + Shift + knob** changes 5 BPM. The range is 10–400 BPM. With Space held it changes track length instead; otherwise it takes priority over the mode’s knob parameter. Transfers block it; when following external MIDI clock, use the DAW tempo instead.
3. A plain knob turn still changes tempo in the ordinary drum/melody view when no other parameter or track is held. `BPM Up` / `BPM Down` also work if exposed by your keymap.

**Defaults And Limits**
1. 12 tracks total.
2. 16 steps per bar.
3. Up to 128 bars (2048 steps).
4. 12 drum lanes per drum track.
5. 16 CC mappings per track.

**Shared Drum Lanes**
1. All 12 track slots still offer the same 12 note lanes. Up to 48 lanes with stored steps or step modifiers can be used across those tracks in any distribution; there is no four-drum-track restriction.
2. Adding the first step or a step modifier claims a lane automatically. Selecting an empty lane, changing its length, or muting it does not claim storage.
3. Deleting its final step releases an otherwise empty lane. Trimming away its remaining content, clearing its track, or converting the track to melodic also releases storage. Step modifiers are cleared with their deleted steps.
4. Each allocated lane retains the full 128-bar capacity and up to 16 custom step modifiers. MIDI routing, lane selection, and track lengths work as before.
5. If an edit needs more than the available lanes, Esc flashes magenta briefly. Existing patterns remain intact; clear an unused lane or track and retry. Multi-lane step entry, recording at each tick, and duplication check capacity before writing.
6. Each independently configured drum Euclid generator uses one of the 48 pool lanes, shared with that lane’s recorded hits and modifiers. Clearing its generator and stored hits releases that lane. Legacy shared generators remain readable without allocating all their lanes.
7. The pool is fixed in size: it does not use the runtime heap. `SEQ_DRUM_LANE_POOL_CAPACITY` in `sequencer_config.h` controls its capacity at compile time (default 48).

**Inactive Behavior**
1. When the sequencer is inactive, double-tap a drum track key to set the default track for the next session.


**Sync and Timing**

- `Space` + `Home` toggles MIDI master/follower; the choice is saved with the project.
- In master mode, `Home` starts from step 1 or stops. The keyboard sends a steady
  24-PPQN MIDI clock and transport over USB MIDI. Each track wraps independently.
- In follower mode, enable MIDI clock output to the keyboard in your DAW. MIDI
  Start restarts at step 1; Stop silences notes; Continue resumes; Song Position
  selects the next six-clock step. Two seconds without clock after receiving
  clock stops playback. Use DAW transport when following.
- `Space` + encoder sets internal timing swing from 0 to 50 percent; this shifts
  alternating step durations without changing the outgoing clock tempo. External
  playback follows DAW timing. Euclid's older “velocity swing” is an accent effect.

**Saving and Computer Backup**

`Space` + `Enter` stops transport and saves the complete project on board. Stop and exit also save
changes. The next startup loads a valid save. The 3 KiB on-board area accommodates
smaller projects; actual size depends on events, lane lengths, and modifiers.
It never truncates a project. If it does not fit, the previous save remains and
Esc flashes red/amber persistently. The same warning indicates invalid saved
content. A successful save clears it. Flash settings use a versioned layout;
this firmware does not migrate dormant saves from older development builds.

For full projects, connect the keyboard by USB and install the backup utility's
optional dependencies in a Python environment:

```sh
python3 -m pip install mido python-rtmidi
python3 tools/seq_backup.py ports
python3 tools/seq_backup.py backup my-project.json --input "INPUT PORT" --output "OUTPUT PORT"
python3 tools/seq_backup.py restore my-project.json --input "INPUT PORT" --output "OUTPUT PORT"
python3 tools/seq_backup.py save --input "INPUT PORT" --output "OUTPUT PORT"
python3 tools/seq_backup.py status --input "INPUT PORT" --output "OUTPUT PORT"
```

Use the exact input/output names printed by `ports`. Backup captures global
settings and all 12 tracks, including all allocated lanes, modifiers, melodic
notes, CC points, mutes, palettes, and accumulator settings. Backup files contain
checksums and will not overwrite an existing file. Restore validates every object
and the shared capacities, writes a dated backup of the current project, then
restores and reads everything back to verify it. Restore changes RAM; run `save`
to persist it if it fits. A failed restore attempts to recover the previous
project. If USB disconnects during restore, reconnect and restore the generated
`before-restore` backup; whole-project restore is not power-loss atomic.

Transfers stop playback and lock sequencer editing for consistency. Completing
or timing out a transfer releases the lock. Stop the DAW before backup/restore;
transport messages are ignored while the transfer lock is active.

**Current Limits and Feedback**

- 12 tracks; 48 shared drum lanes; 2,048 steps per track; 16 custom modifiers per lane.
- 128 melodic events per track. A chord consumes one event per voice; a note
  crossing the recording loop boundary consumes two. Recording stays within the
  track's existing loop length.
- 512 CC points shared by all tracks, capped at 128 per track. Points are keyed
  by track, controller, and full step position. Temporary performance gestures
  are excluded from recording.
- Capacity rejection briefly flashes Esc magenta. Melody insertion and multi-note
  edits preserve previous events on failure. Duplication checks capacities first.
- Melodic editing uses the default two-row steps even when drums use the 4x4 grid.
  Erasure with no held pitches waits through the double-tap window; double-tap
  preserves the note and opens its editor. Held pitches toggle only those voices.
- Euclidean window capture retains the exact rhythm. Adjusting fills or step count
  switches back to generated spacing. An empty melodic track generates Euclidean
  notes using its palette; `Alt/Option` + `Shift` + note keys captures that palette.
- Track mutes, lane mutes, and step modifiers survive exiting sequencer mode.

Run `sh tests/seq_engine/run.sh` for the host regression suite. Compile the actual
firmware before flashing. Real USB MIDI/DAW timing, flash interruption recovery,
and physical LED behavior still require a keyboard hardware check.

While Space is held, Home glows amber for internal master or cyan for DAW follower.

## Interchangeable track modules



## UI manual and live view

Click **Manual** in Module Studio, or press `Cmd` + `/` (`Ctrl` + `/` on Windows/Linux), to open this guide. Search for a control or workflow, then choose a section. Press `Esc` or the shortcut again to close it. The shortcut works while the editor has focus; the browser must receive the key combination.

The hardware panel shows the keyboard's track, type, installed module, transport, recording arm, clock source, position, selected notes and sounding notes. **Sync track selection** links selection in both directions; turn it off to browse drafts independently. The editor keeps local module drafts until you explicitly install them. Live updates require the live-state firmware and do not stop playback. They sample state up to roughly ten times per second, so very short notes may occur between samples.

## Visual keyboard guide

Open the manual with `Cmd` + `/` (or `Ctrl` + `/`) and choose **Keyboard map**. The V1 Max ANSI model shows the physical keys and knob. Switch among drum, melodic, Euclid, CC, navigation, and step-edit views; select any key for its gesture. Arrow keys move between keys after the map has focus. Choose Mac or Windows to match modifier positions. The optional 4×4 grid applies to the drum view; melodic steps always use two rows.

Enable **Follow keyboard** for the live view. With visual-state firmware, the map follows the hardware mode, Mac/Windows modifier layout, and drum grid. Colored strips on the keys mirror the firmware LED buffer. The focused track, recorded steps, playhead, selected pitches/chord slot, sounding pitch classes, recording arm, and transport also have live markers. Notes are marked by pitch class, not as physical key-down events.

Choose a mode to return to the reference view, or turn Follow keyboard off. Clicking diagram keys only shows help. Older live-state firmware still provides basic track, step, and note markers; the UI identifies when a firmware update is needed for modes and LEDs. Lost connections clear live markers and dim the last view. Snapshots are sampled rather than animated locally: very brief notes or flashes may fall between samples. The LED strips show firmware color values, not a calibrated reproduction of physical brightness.

Use **Written guide** for search and the full instructions.

## UI test sound

The speaker button in the header mutes or unmutes the browser's audition sound. Muting stops any preview already playing and disables **Audition voicing** until sound is enabled again. The setting is remembered and shared between open UI tabs. This controls the browser preview only; the keyboard's MIDI output and your DAW are unaffected.

### Notes on melodic steps

With step-note firmware, the live hardware panel shows every stored pitch on each melodic step, including chords. Sustained notes appear on all steps they span; an em dash means no stored notes. The view follows the keyboard's selected track and viewed bar, and updates while stopped or running. Notes beyond the loop are hidden. These are source notes: Euclidean rhythm, palette offsets and other playback processing may change what sounds. Use **Sounding now** for sampled MIDI output.

In the keyboard diagram, melodic step keys show the first note and a remaining-note count, such as **C4 +2**. Click or focus a key to see the complete chord in its description. Older firmware keeps the basic step view and displays an update hint rather than guessing note names.

### Composing a module

In **How this track plays**, choose **When notes play**, then **Which notes play**. These answer two separate musical questions: when should a note start, and which note or chord should you hear?

**Euclidean pulses** spaces hits around the loop. **Follow recorded steps** uses the timing of notes already recorded on the track. Choose **Track notes + palette** to choose palette pitches with recorded overrides, or transposition of recorded/picked notes.

**Use Euclid instrument** keeps the Euclid rhythm and palette together as one instrument, including its existing keyboard controls and accumulator. These choices describe the built-in instruments; they do not require knowledge of an earlier platform version.

Exporting a combination saves a version 2 composition; importing or reading it restores timing and notes together.

For Euclidean chords, use a melodic track’s **chord palette**: hold Alt/Option and tap C to toggle it, use 1–8 to pin a slot and 0 to resume cycling. Hold a slot and turn the knob to choose its chord quality. This is separate from recorded Chord mode. Hardware chord-bank modules are no longer supported; local bank drafts remain authoring sources for Harmony workshop.

Follow recorded steps + Track notes plays the original sustained phrase. Euclid + Stored notes retains the note-span/palette behavior described above. Compositions preserve source notes; choose Follow recorded steps + Track notes to hear the recorded phrase with its full note lengths. The accumulator also works with recorded-step compositions and moves each onset group together.

Live module readouts identify the rhythm and pitch source together. Step-note cards continue to show stored source notes; **Selected on keyboard** and **Sounding now** show the selected notes and sampled output. Updated firmware is required for compositions. The UI checks supported component versions and size limits before installation.

### Help for this track

Press Cmd+/ (Ctrl+/ on Windows/Linux), then choose **This track**. **Local draft** explains the editor’s selected setup; it has not necessarily been installed. **On keyboard** follows the keyboard’s currently selected track using live state. It identifies the setup but does not display full device parameter values. Each component includes its purpose, parameters, controls, an example and compatibility notes. If live state is unavailable, reconnect rather than treating the draft as the installed setup.

### Live rhythm orbit

The hardware panel’s orbit follows the keyboard’s viewed 16-step bar, including eligible hits and the running playhead. Changing fills, rotation or steps on the keyboard updates this view. The orbit is a window into the track. Above it, live-rhythm firmware reports the full generator’s Steps, Fills, Rotation, Probability, Accent and enabled state. Pulses / view counts generated pulses in this bar; Eligible hits / view includes hits supplied by picked-note fallback. These counts precede probability and mutes. The editor orbit is explicitly labelled Draft and remains your unsent configuration.

With live-rhythm firmware, outer labels and step cards show projected melodic pitches, including palette offsets, pinned notes and chord-palette voicings; +N indicates more notes. The projection is before probability, mutes and the accumulator, so it does not promise a note will sound. The center shows sampled sounding notes; short notes may fall between live samples. Older firmware shows stored notes instead. When capturing a palette while Euclid is latched, physically hold Alt/Option + Shift, tap the notes, then release Alt/Option to commit.

### Watching palette capture

The hardware panel shows **Saved palette**, its note names (or reference names and semitone offsets in Transpose phrase), cycling or pinned status, and **After adding** (or **Replacement**) as you capture notes. Release the capture modifier to commit. **Add notes** keeps existing entries and appends unique new pitches into free slots; **Replace** starts a fresh palette. Existing playback continues using the saved palette until then. P1…P8 labels on steps and inside the orbit identify the palette entry contributing to the projected pitch. In Palette notes mode, named pitches are the generated output, and P1…P8 identifies their source. Recorded override badges mark intentional step notes; Use palette clears that step only. In Transpose phrase mode, the reference names describe intervals from the first captured note: F3 alone is +0, so a C3 source remains C3. Choose Palette notes to play F3 instead.

The shortcut follows the keyboard’s hardware layout, not the computer’s operating system. In Mac layout it is Shift + Option. In Windows layout, Alt is beside Space: with Mac keycaps, that physical key is labelled Command. The live palette shows which layout is reported and whether Alt and Shift are actually held. If Q changes Steps instead of appearing in the pending palette, the capture modifier has not been recognized. You may switch the keyboard’s OS selector to Mac to match Mac keycap legends.

### Choosing the melodic pitch source

New Euclid modules default to **Palette notes**. An F3-only palette plays F3 on empty generated steps regardless of the last-picked note. Capture commits on modifier release; Add notes keeps existing pitches, and Replace starts fresh; octave controls move its register. Recorded steps remain intentional overrides. The live palette provides **Palette notes / Transpose phrase** buttons, and each recorded step offers **Use palette**. This removes all recorded pitches at that step, splitting sustained notes when needed; surrounding notes, rhythm, and other tracks remain. If event storage is full, a split is rejected without changing notes. Live controls require updated firmware and pause while capture or another edit is active. Changes are marked dirty; save on board or back up the project to persist them. Older saved modules retain Transpose phrase until explicitly switched.

### Adding notes without replacing the palette

The live palette appears only while the sequencer is active and the keyboard is in **Euclid controls on a melodic track** (held or latched). Leaving those controls or selecting a drum track hides it and expands the step view. Playback and the saved palette remain unchanged.

Choose **Add notes** or **Replace** under CAPTURE. Hold the usual capture modifier + Shift, tap notes, then release the capture modifier. Add notes preserves the saved root, intervals, order and pinned slot, appends each unique note once, and never evicts an entry when all eight slots are full. The preview shows the resulting palette; retained entries have solid borders and additions dashed borders. At capacity, extra notes are ignored and the live status says the palette is full. Duplicate-only or full captures leave playback untouched. Replace discards the previous entries on release and resumes cycling. Capture choice is per track for this session and starts at Add notes after a restart or track reset. The mode buttons are disabled during capture.

**Dedicated Replace shortcut:** hold `Ctrl` + `Shift` + `Alt/Option`, tap the new melodic notes, then release `Alt/Option` to commit. Hold Ctrl before the first note; the choice is fixed for that capture even if Ctrl is released early. This does not change the UI’s Add/Replace preference. In Windows layout with Mac keycaps, use **Ctrl + Shift + the Command-labelled key beside Space**. In Mac layout, use **Ctrl + Shift + Option**.

**First capture:** an untouched starter palette is replaced automatically, even with Add notes selected. For example, starting with C and capturing F + A gives F + A, not C + F + A. Later Add captures preserve your notes. Intentionally capturing C makes it a normal saved note. This distinction survives new project saves and on-board restarts. Older saved projects and imported modules are treated as intentional palettes; use the Replace shortcut once to discard their notes. Capturing no notes leaves the palette unchanged.


### Clear the melodic Euclid palette

Hold **Shift + Alt/Option** (the palette capture modifiers) and tap **Delete**. In Windows layout with Mac keycaps, use **Shift + the Command-labelled key beside Space + Delete**. Ctrl may also be held. Clear acts immediately, empties all eight slots, cancels any pending palette capture, and removes pinning. Releasing the modifiers afterwards does not restore the old notes. You can keep the modifiers held and tap new notes to begin a fresh capture.

Generated hits without recorded notes stay silent until you populate the palette. Recorded step notes and rhythm settings remain; in Transpose phrase, recorded notes play without a palette offset. Clear releases this track’s currently sounding notes. The empty palette persists when saved and does not regain a starter note when Euclid is re-enabled. Other tracks are unaffected. The shortcut is available in melodic Euclid controls and is disabled during recording, step editing, chord capture, or a project transfer.


### Editing live rhythm values

The live hardware row follows the keyboard: **Q Steps · W Fills · E Rotation · R Probability · T Accent**. Enter Euclid controls (hold Alt/Option or double-tap to latch), select the parameter, then turn the knob. Brackets **[ / ]** nudge rotation backward/forward; **F** still selects the drum ratchet threshold. Palette capture keeps its note-key mapping while Shift + Alt/Option is held.

Click a live value, type a number, and press Enter or leave the field to apply it directly to the keyboard. Escape cancels typing. Rhythm Enabled/Disabled controls the generator; enabling from the UI preserves zero fills. Steps ranges from 1 to the track length, Fills from 0 to Steps, Rotation from 0 to Steps − 1, Probability from 0–100%, and Accent from 0–48 MIDI velocity points. Reducing Steps clamps Fills/ratchet threshold and wraps Rotation. Changing Steps or Fills returns a captured template to evenly spaced hits.

Pulses / view and Eligible hits / view are calculated counts for the displayed bar, before probability and mutes. They update as the rhythm changes. Finish held-key gestures or recording before editing from the browser. If the track or value changes while you type, the keyboard rejects that stale edit. Changes stay live during playback and are saved with the project. Use chord palettes for generated harmony; hardware chord-bank installations are rejected. Older firmware stays read-only and displays its older key mapping until flashed.


### Global key, scale, and quantization

While the sequencer is active, tap **Ctrl + Space** to open Scale mode on Space release. Turning the knob while held edits track length instead and suppresses the editor. Release any other held gestures first. Use the melodic note keys (Q, 2, W, 3, E, R, 5, T, 6, Y, 7, U) to choose C through B. Select **A Ionian, S Dorian, D Phrygian, F Lydian, G Mixolydian, H Aeolian, J Locrian, K Harmonic minor, L Harmonic major, ; Major pentatonic, ' Minor pentatonic**. The root lights orange, other scale tones green, and the selected mode violet. The virtual keyboard follows the same layout. When connected, Ctrl + Space also opens a live scale picker modal in the UI. It follows pending keyboard choices and closes after Enter applies or Esc cancels. Hide dismisses only the window. **Enter applies; Esc cancels; 0 toggles global scale on/off.** Picking a root or mode enables the pending scale. Playback continues with committed settings until Enter. The UI shows the pending scale and its notes live. Harmonic scales contain seven notes; pentatonic scales contain five. All eleven scales are also available in the per-track Local scale menu.

Open **Scale** in the hardware panel to choose root and mode visually, audition the pending scale using the browser sound, then Apply global. The header’s Sound mute applies to auditions. Global scale starts off, preserving older projects. Per-track choices are **Follow global**, **Local key / scale**, or **Unconstrained**, with nearest, downward or upward snapping. Nearest ties snap down; at MIDI boundaries the nearest in-range scale tone is used. Apply track commits those settings.

Quantization runs after melodic pitch generation and the accumulator, without rewriting stored notes or palettes. Disabling it restores source pitches. Scale changes release affected sounding notes; playback then continues in the new scale. Drums bypass it. Stored melodic chords are quantized voice by voice and duplicate output pitches share note ownership. Live step labels include quantization but remain before accumulator effects. Use Save or computer backup to persist global and track settings; standalone module files do not contain these host track settings. Old projects default to global off and Follow global; older firmware cannot load the new scale-setting extensions.


### Create a complementary chord track

Open **Harmony workshop** by tapping **Ctrl + Shift + Space** (release Space without turning the knob), or its button in the hardware live view. The keyboard shortcut requires the latest firmware and a connected UI; it opens the workshop without stopping playback. Finish held-note/recording gestures first. Ctrl + Space still opens global scale editing. For melody capture, select a melodic hardware track and choose **A captured melody**. This requires harmony-capture firmware. Click **Capture melody** to freeze the complete loop; capture stops transport. The source uses the highest projected voice at each step, including Euclid timing, recorded overrides, palette choices and scale quantization. Probability, mutes and accumulator movement are excluded, matching the projected live-note view.

Choose **Scale chords** to build three-note chords by alternating scale degrees, or **Closest bank chord** to use a chosen track's local chord-bank draft. The bank provider preserves its pitch classes, using octave placement and inversions. Both prefer a chord containing the melody anchor, then smooth movement from the previous chord. When no exact match exists, the preview says “nearest match.” Pentatonic harmony uses alternating degrees of the five-note scale; it is not restricted to conventional major/minor triads.

Set changes to **Each melody hit**, **Each beat** (four steps), or **Each bar** (16 steps). Beat/bar mode takes the first sounding melody note in that interval; empty intervals stay silent. Hit mode sustains each chord until the next hit or the loop end. Preview and audition the progression, choose a different destination and MIDI channel, then acknowledge replacement. The copy replaces that destination's notes, module, drum lanes and CC automation; the source is unchanged. A full recovery backup downloads before installation, and failed verification attempts restore the destination from that backup.

Output is ordinary editable melodic chord events, with recorded timing, no accumulator, and scale quantization disabled to preserve the chosen voicings. It plays alongside the source; it is a snapshot, not a live link. Recapture to derive another progression. The existing 128-note-event limit applies (a triad uses three events); choose a slower rate if needed. Use **Save on board** afterward, or keep the computer backup if the project does not fit.

### Build chords first, then generate companion parts

In **Harmony workshop**, choose **A new chord progression**. No melody track is required, and you can preview while disconnected. Choose the global key/scale or a local chord-bank draft, then edit each chord, duration, inversion and octave. Add or remove positions, up to 32 chords. Durations use sixteenth-note steps: 4 is one beat, 16 is one bar. Scale chords stack alternating degrees; bank chords keep the bank's pitches.

**Generate** selects chords, ascending/descending/up-down arpeggios, or bass. Arpeggios restart their note order at each chord and clip the last note to its boundary. Bass repeats the lowest note of the chosen voicing, so inversions change its bass note; choose Output octave −1 or −2 for a lower register. Note every sets the spacing; Output octave transposes the generated part.

Audition, choose the destination and MIDI channel, acknowledge replacement, then **Back up & create track**. The workshop stays open: change Generate and choose another destination/channel to create an accompanying part from the same progression. The captured melody's source track is protected in capture mode; chord-first mode allows any explicitly selected destination. Installation backs up the full project and verifies the destination. Save on board afterward.

Outputs are ordinary editable melodic notes, with a maximum of 128 note events and a 2048-step loop. The preview blocks outputs beyond those limits; use a slower note rate or shorter progression. Workshop recipes are temporary browser-session drafts; installed notes persist with project saving. Existing hardware chord tracks are not yet imported as full polyphonic progressions—the melody capture reads their highest voice. Full chord capture, recipe files and live links are future work.


### Hold a step to make a quick edit

In the normal drum or melodic step view, hold a populated step for a quarter-second, then turn the knob. Turning the knob while the step is down starts editing immediately; you do not need to wait. Release the step to finish. Double-tap still opens the full editor, and Shift + step remains available.

- **Drums:** the knob selects Normal, Accent (+20) or Ghost (−40) relative to the track velocity. This edits the selected lane's step. New drum hits are added on press and can be edited during that same hold.
- **Melody:** release the note keys first, hold a recorded step and turn the knob to lengthen or shorten its selected note by one step per turn. While keeping the step held, tap **I Length · O Pitch · P Velocity** to choose what the knob edits. You can tap the parameter immediately, without waiting for the hold timeout. Every new hold starts on Length. Pitch editing follows the track's active global/local scale; Scale Off uses semitones. The physical and virtual keyboards highlight the selected note.
- A quick tap on an existing step still toggles/removes it, now on release. Holding without turning leaves its notes intact. Parameter keys can change the melodic hold edit; release the step before using other controls. Esc cancels the hold gesture without removing the step. Changes already made with the knob remain.

These shortcuts apply in the normal step view; leave latched Euclid, CC or scale controls first. Recording and held-note chord placement keep their existing gestures. On a sustained melodic event, changing its pitch edits that whole event, including its other steps. A polyphonic step currently edits one selected voice; it does not transpose the entire chord. Hold editing of generated-only melodic pulses is not yet supported: place a recorded note override first.

The full drum editor still uses I Velocity, O Ratchets and P Spacing. The full melodic editor uses I Length, O Pitch and P Velocity; its existing chromatic pitch editing remains available. Save on board to retain edits.

## Chord step mode

While the sequencer is active, hold **Ctrl + Tab** and tap **F1–F12** to choose a track in Chord mode. You can also choose **Chord** in the live hardware view's Mode selector. If the keyboard asks for confirmation, press **Y** to proceed or **N** to cancel. Leave held controls and disarm recording first. Switching between Melody and Chord preserves the recorded notes; converting a drum track uses the existing conversion confirmation.

Chord mode starts with a four-step chord: Major with scale off, or an In-scale triad when the track follows an enabled scale. Nothing is placed until you tap a step.

| Gesture | Action |
| --- | --- |
| Note key | Choose a root, including its current octave; audition while held. Prepares a new chord without changing an already selected step. |
| **I**, then knob | Change **Length**, in sixteenth-note steps. Shift changes four steps per turn. |
| **O**, then knob | Change **Root** through the active scale, or semitones with scale off. |
| **P**, then knob | Change **Velocity** for every voice. Shift changes five units per turn. |
| **[**, then knob | Change **Inversion**: root position, first, second, or third for a four-note chord. |
| **]**, then knob | Cycle **Type**: Triad → Seventh → Ninth → Sixth → Add9 while scale-bound. |
| **Shift + ]** with a chord selected or held | Toggle **Scale-bound / Free** for this chord only. Free preserves its current sounding notes and permits chromatic roots and unrestricted types. Toggle again to rebuild a scale-derived triad or seventh. |
| Tap an empty step | Place the current chord. |
| Tap a populated step | Select the whole chord for I/O/P/[/] editing. Tap a sustain tail to select its owning chord. |
| Hold a step + knob | Edit that chord's length. Tap I/O/P/[/] while holding for Length/Root/Velocity/Inversion/Type. Each new hold starts on Length. Release finishes the temporary edit. |
| Hold a step + note key | Change that chord's root directly. |
| Hold a note key + tap a step | Stamp or replace a chord. Starting inside a sustain shortens the preceding chord. |
| **Del + step** | Delete the whole chord, including every voice. |
| **Esc** | Leave the selected chord and return to preparing the current chord. |

The live panel shows **Current chord** or **Editing step**, the full pitch list and selected knob parameter. Step labels show chord names; arrows mark sustain tails. The virtual keyboard follows Chord mode, showing the root, chord tones and selected group. Open **Cmd + /** → Keyboard map or Written guide for these controls.

With an enabled global or local scale, new chords are **Scale-bound**. In **F natural minor**, choose **G** for **G diminished** (G–B♭–D♭); the Seventh variation gives **G half-diminished seventh** (G–B♭–D♭–F). A fully diminished seventh contains F♭ and is available in Free mode. Root edits follow scale notes; Type selects scale-derived Triad, Seventh, Ninth, Sixth and Add9 variations. Harmonic and pentatonic scales use their own degrees, so some results have less familiar voicings. The live panel displays the resulting chord quality and Scale-bound/Free status.

**Shift + ]** applies only to the selected chord. It preserves the voicing as a custom recipe when freeing it, and the choice persists through saves and backups. The next new chord remains scale-bound. In Free mode (or with the track scale off), Type offers Major, Minor, Diminished, Diminished 7, Augmented, Sus2, Sus4, Dominant 7, Major 7, Minor 7, Half-diminished 7 and Custom. A Free chord's root moves chromatically. Return it to the scale with Shift + ] while a scale is enabled; this rebuilds a triad or seventh based on voice count. Existing chords retain their notes when the global scale changes; a root/type edit deliberately rebuilds the chord with the current context. Length edits keep the existing pitches. Inversion edits preserve the root, type and saved scale context, raising the lowest voice by an octave at each turn. For example, C4–E4–G4 becomes E4–G4–C5, then G4–C5–E5. Set Inversion to Root position to undo this voicing change. Changing a seventh to a triad clamps third inversion to second. For custom chords, inversion zero means the supplied voicing; an octave already occupied by another voice is skipped to preserve distinct voices. Out-of-range edits retain the previous chord.

Chord mode uses recorded timing. Each chord ends at the next onset or the loop boundary; it does not wrap or overlap another chord on the same track. Switching into Chord selects recorded-note playback and bypasses the accumulator. Installing a different module exits Chord editing while retaining recorded notes. Use **Tab + F1–F12** or the Mode selector to return to Melody.

There are **64 chord recipes shared across all tracks** and the existing **128 recorded note events per track**. Triads use three events; sevenths, sixths and add-nine chords use four; ninths use five. Failed edits retain the previous chord. On-board saving and computer project backups preserve recipes and notes, subject to the existing on-board storage limit. Older firmware does not support this recipe extension.

This first slice edits chords authored in Chord mode. Existing melody, migrated bank notes and Harmony workshop notes keep playing, but are not automatically grouped; an overlapping placement reports that recorded notes occupy the region. Individual-note edits, clears, trims or copies outside Chord mode can detach the affected recipes; their remaining notes stay ordinary melodic events. Workshop read/edit/write is described below. Custom/bank recipe import is available in the workshop below. Spread remains planned. Euclidean chord palettes are available in Melody mode, as described below. Live recording continues to create ordinary melodic events; step placement is the way to author grouped chords in this slice.

## Editing chord tracks in the Harmony workshop

Open **Ctrl + Shift + Space** or click **Harmony workshop**. On a Chord track, the workshop opens **An existing chord track**. Click **Read chord track** to capture its saved recipes; playback stops during the read.

- Edit each chord’s **Root, Type, Inversion, Start step, Length and Velocity**. In-scale chords also expose their saved **Scale key** and **Scale**. The heading shows the exact resulting pitches.
- Start step is numbered from 1 across the whole loop. Length uses sixteenth-note steps. Gaps remain rests; chords cannot overlap or extend beyond the captured loop.
- Use **Add chord** or **Remove** to change the progression. Adding starts after the last chord; shorten or reposition it if the loop is full. Removing every recipe leaves the other recorded notes intact.
- Choose **Chords** to write the progression back to its source, or copy it to a different destination. The captured track’s other recorded notes, CC automation, settings and MIDI channel are retained unless you change the channel. Writing to another destination replaces that destination with this edited copy.
- Choose an **Arpeggio** or **Bass** output to derive an independent melody on another track. Only the chord recipes drive this output; other source notes and CC automation are not copied. The source chord track is preserved.
- Check the destination confirmation and choose **Back up & write track**. The app downloads a recovery backup, validates the output, verifies the write and attempts recovery if verification fails. If the source changed on the keyboard since reading it, the write is refused: read again before applying edits. Choose **Save on board** afterward to persist the result.

For **A new chord progression** or **A captured melody**, **Keep supported chords editable on the keyboard** preserves known recipes when chord-mode firmware is connected. Scale voicings, including inversions, can be edited later with I/O/P/[/] on inversion-capable firmware. Bank voicings with declared roots remain editable on custom-chord firmware; other bank voicings remain ordinary notes; the preview reports how many chords retain recipes. Arpeggio/bass output remains melodic. The workshop never guesses a chord’s root from its lowest voice.

Inversion controls require updated firmware. When connected to older chord-mode firmware, the workshop preserves root-position recipes and writes inverted output as ordinary notes. Existing chord projects load in root position. New backups containing inversions require inversion-capable firmware.

## Conform a chord progression to the global scale

Set and enable the global key and scale with **Ctrl + Space**, then open **Harmony workshop → An existing chord track → Read chord track**. Click **Apply global scale to all chords…**. This operates on every saved chord recipe on the captured track; other tracks and ordinary recorded notes are unchanged.

The preview shows the original and proposed chord pitches. Each root snaps to the nearest allowed scale note, choosing the lower note on a tie. Three-note chords become **In-scale triads**; four-note chords become **In-scale sevenths**. This deliberately rebuilds chord quality to fit the scale. Start positions, rests, lengths, inversions and velocities are preserved. All supported modes, harmonic scales and pentatonic scales use their own scale degrees.

Use **Audition scale preview** to hear the proposal. **Cancel preview** leaves your progression unchanged. **Use these chords** applies it to the workshop draft only. Confirm the destination and choose **Back up & write track** to send it to hardware, then **Save on board**. If either the source track or global scale changed since capture/preview, the write is rejected so you can read and preview again. An out-of-range chord or capacity failure rejects the whole proposed transformation.

This converts existing recipes; it does not establish a live link to the global scale. Later global scale changes still require an explicit preview and apply. Custom recipes keep their root, intervals and name and quantize each voice to the global scale. Other ordinary notes are unchanged; adopt them into recipes first if eligible.

## Custom chords, bank roots and note-group adoption

Custom chords preserve an explicit root, a name and **2–8 intervals**. The root may sit above the bass or may not be sounded at all. A chord name is a label for the voicing; changing its root does not rewrite that label. This format is shared by bank imports, adopted note groups and manually edited Custom recipes.

**From a bank:** open Harmony workshop, choose **Closest bank chord**, then expand **Bank chord roots & names**. Choose a MIDI root for each voicing you want to keep editable. You can also set **Declared root** in the main bank slot editor. A missing root stays unknown; the workshop never guesses from the bass. Names and roots remain in the local bank and exported module JSON. The bank playback wire format still carries notes only, so keep the JSON for those authoring labels. Creating editable chord steps copies the root, short name and intervals to hardware, where on-board save and project backup retain them. Names on chord steps use up to 12 plain characters; bank names are shortened and musical flat/sharp symbols become b/# for that display.

**From recorded notes:** choose **An existing chord track → Read chord track** on a Melody or Chord track. Eligible groups appear with their exact pitches and duration. Select the **Declared root** and **Name**, inspect the intervals, then choose **Adopt group … into draft**. Adoption preserves the notes. Only groups with the same onset, end and velocity, 2–8 distinct pitches and no overlapping events are eligible. Ambiguous groups and other notes stay untouched. Writing an adopted track selects Chord mode with stored timing. When replacing a different playback module, its rhythm and palette settings reset; the captured CC automation and remaining note events are retained.

**Edit a custom recipe:** in the existing-track editor, choose **Type → Custom**, then edit **Name** and **Intervals**. Intervals are ascending semitone offsets from the root, separated by commas; negative offsets keep notes below the root. For example, root E4 with intervals −4, 0, 3, 10 produces C4, E4, G4, D5. Built-in-to-Custom conversion preserves the current sounding voicing. Select a step on hardware to edit its Length, Root, Velocity, Inversion or Type using **I / O / P / [ / ]**, then the knob. Type changes replace the chosen recipe deliberately. Custom interval/name editing happens in the workshop.

Draft edits are previewed before hardware writing. Use **Audition chord progression**, confirm the destination, then **Back up & write track**. Save on board afterward. Custom and quantized recipes require the updated firmware; the UI disables unsupported editing and generates ordinary notes when an older keyboard cannot retain those recipes.

## Optional chord-voice quantization

There are two different scale operations:

- **Apply global scale to all chords…** rebuilds built-in triads/sevenths from the global scale. Custom chords retain their source intervals and quantize their voices instead.
- **Quantize all voices to global scale…** preserves every source recipe and previews snapping its voices to the nearest allowed notes, with ties going lower. Voices that land on the same MIDI pitch merge into one note. Root identity, source intervals, inversion, timing and velocity stay intact. Choose **Use these chords** only when the preview sounds right.

For an individual recipe, use **Quantize voices → On**, then choose its **Quantize key** and **Quantize scale**. Turning it on initially uses the enabled global scale when available. The quantizer has its own saved scale: an In-scale chord can retain its original construction scale while its output is constrained to another. Switching **Quantize voices → Off** restores the source voicing. Editing intervals while quantization is enabled changes that source voicing.

For newly generated progressions or harmonized melodies, check **Quantize voices to the selected key and scale**. This works with both scale and bank providers and shows the resulting output notes before creation. Bass/arpeggio companions use those resulting pitches. Quantization does not establish a live link: later global-scale changes do not silently rewrite stored chords. Preview and apply again when needed.

## Application shortcuts for harmony

| Shortcut | Action |
| --- | --- |
| **Ctrl + Shift + Space** | Open the Harmony workshop (hardware or browser). |
| **Cmd/Ctrl + Shift + Enter** | In the chord workshop, after reading a track, preview **Apply global scale to all chords**. |
| **Cmd/Ctrl + Option/Alt + Enter** | In the chord workshop, preview **Quantize all voices to global scale**. |
| **Cmd/Ctrl + /** | Open or close this manual. |

The two application shortcuts open the existing scale preview; they do not write to the keyboard. Review/audition it, choose **Use these chords**, then **Back up & write track**. The sequencer keyboard consumes its own composition controls; use a normal typing keyboard or leave sequencer mode for browser-only shortcuts.

## Firmware management

Open **Firmware** in the UI header. The V1 Max firmware manager shows builds registered on this computer, their release notes and SHA-256 checksums. **Connect & check MIDI** compares the keyboard's reported release version, build ID and board target with the selected release, and checks the expected features. Identity-enabled builds report an exact release match or a mismatch. Legacy releases can only verify features and are explicitly labelled as such. The file SHA-256 validates the downloaded artifact; a build identity is not a readback checksum of flash memory.

1. While connected normally, choose **Back up keyboard project**. Keep the downloaded JSON. A recovery copy is also retained in this browser. Backups contain all twelve tracks and global settings; local module drafts remain in browser storage and can be exported separately.
2. Set the hardware switch to **Cable**. Unplug USB, hold **Esc** or the reset button under the space bar, and reconnect USB. Release the key.
3. Select the release and the detected bootloader serial. The STM32 bootloader cannot identify the keyboard model, so choose the V1 Max you just connected. Acknowledge that the current project is backed up or no longer needed, then click **Install selected firmware**.
4. Keep USB connected and the local service running until the flash completes. The panel shows erase/write progress and a diagnostic log. Closing the panel does not cancel an in-progress flash. Reopening it or reloading the page recovers the running job from the service.
5. Reconnect normally if needed, then choose **Reconnect & verify**. This checks the reported build identity, module capabilities and all chord-state pages. If Chrome does not receive MIDI, follow the existing Connection help. Flash completion and MIDI verification are separate results.

Firmware updates reboot the keyboard and discard unsaved RAM changes. On-board saves are not a substitute for a computer backup. Restore a saved project with `tools/seq_backup.py restore backup.json`; restoration is never automatic. Older firmware may reject projects containing newer chord data. Keep a compatible firmware release with important backups.

The UI needs the local service instead of a plain file server. From the `seq_test` folder, run `python3 tools/seq_server.py serve`, then open `http://localhost:8765/ui/`. On macOS the service uses dfu-util bundled in QMK Toolbox; otherwise it uses dfu-util from PATH. It binds only to this computer.

Build registration is a maintainer action, not a module import. Run `python3 tools/seq_server.py register /path/to/firmware.bin --target keychron/v1_max/ansi_encoder:seq_test --label "Release name"`. Only register a trusted image built for this exact board. The service checks its bounds, reset vectors and checksum, then stores a copy under `.build/firmware/`. These checks protect integrity; they do not authenticate the publisher. Previous registered releases remain available to download or reinstall.

### Remove one palette note

Hold the **Euclid button + Delete**, without Shift, and tap **1–8** to remove that palette slot. The Euclid button is the same physical key used to open Euclid controls; on Windows layout with Mac keycaps it is the Command-labelled key beside Space. You can also click **Remove** beside a note in the live palette panel with updated firmware.

Remaining slots close up in their existing order and keep their pitches. A pin follows its surviving note; deleting the pinned note resumes cycling. Removing the last note leaves an empty palette. Recorded step notes and rhythm settings remain. Finish capture, recording or step editing before removing notes. Save with **Space + Enter** to keep the change.

A plain tap of the Euclid button exits a latched session. Holding it for palette capture, pinning, removal or knob edits keeps the session open. The existing **Delete + double-tap Euclid** generator-reset gesture remains available.

### Print a Euclid loop

While Euclid controls are latched, hold **Ctrl** and tap the **Euclid button**, then release Ctrl. On Windows layout with Mac keycaps, the Euclid button is the Command-labelled key beside Space. Do not hold Shift or use any other note/parameter controls during the gesture.

This replaces the current melodic track's recorded notes with the generated **whole track loop**, disables Euclid and the accumulator, and exits to normal note editing. Playback keeps its current position. The printed events have one-step lengths and include palette cycling/pinning, recorded-note overrides, scale-quantized pitches and Euclid accents. Track volume and scale settings remain active. Palette and rhythm settings remain available for reuse.

The destination always has the **current track length**. If the melodic Euclid cycle is longer, print only the portion inside the track. If it is shorter, repeat that cycle's pitches and recorded overrides until the track is filled, clipping the final repetition. For example, an 8-step cycle fills a 20-step track as 8 + 8 + 4 steps.

On **drums**, Ctrl + Euclid prints generated hits into the **selected lane**, including existing manual hits. The lane's generator is disabled and the edit session exits; sibling lanes retain their patterns and generators. Existing per-step velocity and repeat edits are retained on printed hits. This prints hit positions, not Euclid's live probability, accent or auto-ratchet effects. Short rhythms repeat to the track length; longer rhythms are clipped. If no storage lane is available, the operation leaves the pattern unchanged and flashes Esc magenta.

Printing captures the underlying pattern before probability skips, mutes and evolving accumulator movement; it does not record one randomized performance. Up to 128 note events fit, counting each chord voice. If the print would exceed that limit, Esc flashes magenta and the original notes and Euclid mode remain unchanged. Finish recording, step editing, chord/palette capture and project transfers first. This shortcut supports melodic Euclid tracks and the selected drum lane, hardware chord-bank tracks are no longer supported.

You can now add notes between the printed hits, remove notes, or edit their pitches, velocities and lengths. **Space + Enter** saves the result. Plain Euclid tap still only exits the controls; **Ctrl + Shift + Euclid + note keys** still replaces the palette.

### Live track mute indicators

Desktop and web track lists use the same live mute information. Fully muted tracks show a steady red indicator with **Muted** in the workshop or **M** in the desktop strip. A pulsing red indicator with **Partial mute** (or **±**) means some drum lanes or a wrapped melodic layer are muted while other layers may still sound. Hover a track for its status. Pulsing respects reduced-motion preferences. A disconnected keyboard or older firmware reports mute status as unavailable rather than assuming the track is unmuted.

CC controls use compact tiles with the keyboard key, CC number, value and MIDI channel. The desktop strip keeps all sixteen in two rows; the web workshop adapts its column count to available width.


## Euclidean chord palettes

On a melodic Euclid track, capture root notes into the palette as usual. Select **Chord palette** in the companion, or hold the **Euclid button and tap C** to toggle Palette notes / Chord palette. Newly captured slots start Major; changing pitch modes preserves existing qualities. Recorded notes on a step still override the palette.

Double-tap the Euclid button to latch its controls. **Hold 1–8 and turn the knob** to change that slot's chord quality; **hold 0 and turn** to set every slot. A number tap without a turn pins that slot; tapping 0 resumes cycling. The pin is applied only on release, so editing never briefly pins a progression. Release the number to return the knob to its previous Euclid parameter. Changes take effect on the next generated hit. Ctrl + knob retains tempo priority and cancels the pending slot gesture.

While a slot is held, the picker uses the scale picker's colors: **orange root, green chord tones, violet selected quality**, with dim violet alternatives. Home-row keys directly choose **A Major, S Minor, D Diminished, F Augmented, G Sus2, H Sus4, J Dominant 7, K Major 7, L Minor 7, ; Half-diminished 7, ' Diminished 7**. **Z Auto triad / X Auto seventh** follow the active global or local scale. Auto lights both its own key and the resolved home-row quality when the voicing matches a named chord. The companion shows the selected slot, quality, resulting notes, and an explicit “Auto → …” label. For all-slot editing, the keyboard previews the first slot; the companion reports mixed qualities until they are unified.

Fixed qualities preserve their intervals even when global scale is enabled. Auto is offered only with an enabled global/local scale. If that scale is later turned off, an existing Auto entry plays its root alone and the companion says “scale off, root only”; re-enabling the scale restores harmonization. A chord that exceeds MIDI pitch range is silent; lower the palette octave. New palette captures replace qualities with Major; Add notes retains existing qualities, while removal moves each quality with its root.

Palette chords cycle on Euclidean hits, or stay pinned. They use one-step gates, the existing whole-chord probability and accent controls, and do not consume stored chord recipes or note events until printed. Printing renders the same chords subject to the existing 128-note limit and sets that track to Unconstrained, preserving the baked pitches instead of quantizing fixed qualities again. The global scale and other tracks are unchanged. Save, project backup, module export and TEMP preserve qualities. The chord-palette wire extension requires current firmware and companion software; older modules retain their existing behavior.

## Vary a playing phrase

Open **Variation** in the companion’s live keyboard view. These controls work with recorded Melody/Chord tracks and Euclidean note/chord palettes. They change playback without copying or rewriting the source notes.

- **Reverse**, **Rotate**, and **Reshuffle** change pitch order. Reshuffle uses each source entry once; **Random selection** can repeat entries. A palette entry carries its chord quality. Recorded notes sharing an onset move together as a chord. Reordered chords use the destination onset, its longest gate, and its first voice’s velocity. Original-order groups retain their individual voice gates and velocities. A pinned palette entry remains pinned.
- **Mutation chance** controls how often a group’s root changes. With global/local scale enabled, **Range** measures scale degrees. With scale off, it chooses an existing palette root within the range in semitones; no eligible root means no change. Every chord voice moves by the same amount, preserving the current interval shape. This also preserves the currently rendered quality of Auto chords rather than reharmonizing each mutated root.
- **Voicing** raises the lowest one, two, or three voices by octaves, or chooses a random inversion. MIDI-range limits never clip a chord voice or collapse it onto another voice.
- **Lock** freezes the current random seed so the variation repeats each track loop. **Unlock / evolve** chooses a new deterministic variation each track loop. **Reroll** changes the seed; it affects Reshuffle, Random selection, mutation, and random inversion. Forward/reverse/rotation alone do not depend on the seed.
- Choose **This track** or **All melodic tracks**, then apply immediately or at the **next global bar** (16 steps). Drum lanes remain outside this scope. A queued change can be cancelled; stopping transport applies it. The panel distinguishes a local draft, a queued change, and the active settings.

**Reset variation** returns to the source phrase and leaves accumulator settings intact. **Save on board** keeps the playback settings; a computer backup includes them. TEMP restores the source and the variation/accumulator settings from TEMP entry. This keeps the result as a reversible transform; it does not bake a new performance into note events. Disable/reset variation before using Euclid’s print shortcut.

### Chord-aware accumulator

The accumulator now chooses one palette offset per onset, including complete chords and recorded-step compositions. A three-voice chord advances it once, not three times. Sustained voices keep that offset until their release; probability-skipped Euclid hits do not advance it. Near MIDI limits, the shared shift is reduced so the whole chord fits. Multi-voice groups keep their intervals; single-note output retains normal scale quantization.

The Variation panel exposes enable, forward/backward/random traversal, and reset after 2 or 4 onsets. Accumulator edits apply immediately; existing step-edit shortcuts remain available. Chord mode no longer bypasses the accumulator. Live projected labels include variation but exclude accumulator movement and probability.
