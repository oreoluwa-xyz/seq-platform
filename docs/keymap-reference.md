Shared Drum Lane Storage

## TEMP performance mode

Hold **Enter** and tap **Esc** to capture a restore point for all tracks. Esc turns **purple** only after the complete snapshot fits. Release Enter, make your buildup changes, then tap **Esc** to restore the drop and leave TEMP. Playback stays on the current transport position; it does not restart. **Shift + Esc** queues restoration for the start of the next global 16-step bar. Esc blinks purple while queued. Press **Shift + Esc again** to cancel the queued restore and keep TEMP active (solid purple), extending the buildup. Press it again when ready to queue the next bar. Esc alone restores immediately, and Enter + Esc keeps the edits and cancels the queued restore. When stopped, Shift + Esc restores immediately; stopping playback with a restore queued also restores it.

TEMP restores recorded notes and chords, drum hits and modifiers, mutes, loop lengths, Euclid palettes/settings, scales, accumulator state, tempo/swing, MIDI routing, CC values and automation. It restores the original selected track/view as well. The chord editor and ordinary polyphonic notes remain available.

Finish held-key edits and disarm recording before entering TEMP. Recording and editing work normally after entry. To keep your changes, hold **Enter + tap Esc again**: TEMP exits, the purple light clears, and the current edits become the normal sequence. This discards the restore point without restarting playback or immediately saving to flash; normal saving resumes. A later TEMP captures this new version. Leaving sequencer mode also rolls back. Saves and project transfers are blocked during TEMP; live monitoring and edits remain available. External synthesizer changes made outside the keyboard cannot be restored.

The snapshot has a bounded 7.25 KiB buffer shared with USB transfers. If the project is too large or an edit/transfer is busy, Esc gives the existing magenta capacity feedback and TEMP does not activate. There is no partial snapshot. TEMP is RAM-only and does not survive power-off.

Chord-bank modules have been removed to make room for TEMP. Recorded chords and polyphonic notes are retained. Older saved tracks containing banks migrate to recorded-note playback; bank-generated playback and unprinted bank slots are no longer available. New bank installations are rejected. Studio can still use offline bank drafts to create recorded chord progressions.


  - Keep all 12 tracks and their 12 note-lane controls, with a shared
    pool of 48 lanes for stored steps and step modifiers. Use three
    lanes on one track and one or two on several others; allocation
    is automatic, with no four-track restriction.
  - Empty lanes consume no pool slots. Clearing the last content,
    clearing a track, or converting it to melodic returns storage.
    Every allocated lane still supports 128 bars.
  - Esc flashes magenta when an edit needs unavailable lane storage.
    Existing patterns are preserved. See MANUAL.md for details.

Tracks & Selection

  - Switching – Single-tap a track key (F1…F12 or the mapped media
    keys) to focus it. Melodic slots glow with their hue; drum slots
    show a solid track color. Drum tracks default to the sibling lane
    matching the key you tapped (F5 → lane 5).
  - Drum sequences – Hold the active drum track key, then tap another
    F-key to jump straight to that lane’s sibling sequence. Release
    the held track to exit length-entry mode without changing focus.
  - Melodic vs Drum – Hold Caps Lock + track to convert/prepare a drum
    slot, or hold Tab + track for a melodic slot. If you change an
    active track’s type you’ll be prompted (Y/N) because data will be
    cleared. You can prepare tracks without switching by holding the
    combo briefly and releasing.
  - Hold a track and tap `,` / `.` to cycle its MIDI channel. Hold
    Shift at the same time to retune the active drum lane (per-note
    mapping).

  Melodic Workflow

  - Hold note keys (Q-U, sharps on 2,3,5,6,7) to form a chord, then
    tap a step to place it. Release all notes and tap a step to erase
    everything there; hold specific notes and tap to remove just those
    pitches.
  - Double-tap a step while holding a chord to latch its start point.
    You can release the notes, jump to another bar, then tap the final
    step to stretch the chord across that span.
  - Double-tap any populated step to enter step edit. The melodic note
    keys now toggle individual voices, while `I/O/P` choose length,
    pitch, or velocity for the encoder. Use `[` and `]` to step the
    accumulator forward/backward, `;` to cycle run length, `'` to
    toggle randomness, and `/` to enable the accumulator.
  - While step edit is active, the number row mirrors parameter values:
    velocity/ratchet/spacing draw left-to-right bars, and melodic
    length/pitch/velocity show their digits; the `I/O/P` cluster (and
    `[ ] ; ' /` in melodic mode) glow with the color of the option
    currently under the encoder.
  - 1 lowers octave, 8 raises it. 4 toggles rest/tie (Shift+4 arms
    a tie).
  - Hold ~ to nudge octaves upward (tap-dance stacks +1/+2/+3);
    hold Shift+~ for downward offsets. Releasing returns to the base
    octave.

Euclid Mode

  - Hold Alt/Option to peek at the Euclid layer for the focused track;
    double-tap that modifier to latch an edit session and keep the
    Euclid controls on the encoder. Tap the modifier once to exit a latched session.
    The Euclid button glows amber when latched and cyan while held; its LED
    follows the keyboard’s selected Mac/Windows layout.
  - Tap Q/W/E/R/T while the session is active for Steps, Fills,
    Rotation, Probability, or Accent—turn the encoder for fine or Shift-accelerated
    changes.
  - Tap [/] to bump rotate ±1 without leaving performance grip; encoder
    stays free for other duties.
  - While adjusting fills, hold Shift and move the encoder to dial in
    a velocity swing—Euclid hits alternate between boosted and softened
    accents.
  - Option + Tab snapshots the visible 16-step window into a fresh
    Euclid template, perfect for capturing improvised riffs.
  - Hold Alt/Option + Shift and tap the melodic keys (Q–U plus sharps on the
    number row) to capture a Euclid palette—up to eight slots recorded
    in the order you play them. Release Alt/Option to commit. Add notes
    keeps existing notes; the first capture replaces an untouched starter.
    Hold **Ctrl + Shift + Alt/Option** before the first note for a one-capture
    Replace, without changing the UI preference. On Windows layout with Mac
    keycaps, Alt is the Command-labelled key beside Space. Palette notes
    plays these pitches on generated hits; Transpose phrase uses their offsets.
    Holding Option later highlights the stored palette on the melodic
    key row so you always see the available notes.
  - Option+1…8 pins playback to a specific palette slot (Option+0
    returns to normal cycling) so you can spotlight a single pitch on
    demand.
  - Hold Alt/Option and tap `R` to grab the Euclid probability slider,
    then use the encoder (Shift = ±10%) to decide how often those hits
    should fire; set it to 100% for guaranteed triggers.
  - Alt/Option + `F` adjusts the auto-ratchet threshold—once fills meet
    or beat that value, Euclid layers a two-hit ratchet on fresh
    entries (set the threshold to 0 to disable the behavior).

  Drum Workflow

  - Each drum track contains up to 12 sibling sequences (one per note
    lane). With the track active:
      - Hold the track key and tap any F-key to select that sibling
        directly (F3 edits lane 3, F11 edits lane 11, etc.). The LED
        color shifts to reflect the new lane.
      - Tap the track key by itself to snap back to the sibling that
        matches its index.
  - Toggle steps on/off with the 4×4 grid (1-4, Q-R, A-F, Z-V). Hold
    additional track keys while tapping steps to place multiple drum
    lanes at once.
  - Double-tap any filled drum step to open its editor. Inside, the
    encoder follows `I/O/P` for velocity mode, ratchet count, or
    spacing; the note-grid keys remain available for quick velocity
    tiers.
  - Delete steps by tapping them while their lane is selected; to
    clear the entire sibling sequence, hold Del and tap the track key.
  - Duplicate the current sibling sequence into the next slot
    by holding Tab + = (copies). Hold both Shifts as well
    (Shift+Shift+Tab+ =) to double the length but leave the new region
    blank.

  Length & Duplication

  - Hold a track key ~220 ms, type digits (1–128) to set its bar
    count, then release.
  - While holding a track key, tap Backspace to switch the length
    entry from bars to raw steps; subsequent digits set the exact step
    count.
  - Tab + = doubles the exact step count and copies all track lanes into the new
    region. Add both Shift keys to create blank space instead. Works
    for melodic tracks too.

  Navigation & Follow

  - Hold the sequencer switch (Space) to access navigation controls:
      - Tap digits to jump to bars (0 = 10). Rapid taps multiply (e.g.
        Space+3 twice → bar 6). Hold Shift while tapping to double
        the target.
      - Press = (while still holding Space) to toggle follow mode.
        When on, the view snaps to the playhead every step; when off,
        you can scrub freely. The `=` key glows teal while follow is
        engaged and pulses red when you’re viewing a different bar.
      - Space + S + ~ returns to the default layout presets.
  - Bar LEDs (row 9,0,-,+,O,P,[,]) give a binary readout: cyan =
    viewed bar, orange = playhead, white = overlap. Esc pulses red
    when follow is off and the playhead leaves view; steady green
    means follow on; amber means manual scrubbing.

  Erasing & Reset

  - Drum lane: hold Del and tap a track key to clear its selected lane.
    Add both Shift keys to clear the entire drum track.
  - Melodic lane: hold Del and tap the track key to clear the entire
    track.
  - Press PgUp twice to exit the sequencer; transport (Home) toggles
    playback and emits MIDI start/stop while resetting the clock.

  Quality-of-Life Touches

  - The switch key is fully gated while the sequencer is active, so
    the DAW never receives stray spacebar presses.
  - Chord holds and multi-taps (track selection, bar jumping, octave
    holds) are debounced with a 300 ms window, making double-shots
    responsive without accidental triggers.
  - LED color schemes distinguish melodic keys (light yellow/blue)
    from the step grid so you always see where you’re working.
  - MIDI output mirrors every action—clock, song-position, start/stop,
    drum hits, melodic notes—so your DAW stays in sync with hardware
    edits.
  - Esc now consistently backs out of the active edit mode—CC latch,
    chord capture, step edit—without leaving the sequencer, giving you
    an always-on escape hatch.

  Enjoy exploring: start with Track 1 for drums, drop a simple 16-step
  pattern, hold that track while tapping F2 for a variation lane, then
  switch to a melodic track for bass or chords—all while the playhead
  follows along.


Sync, Saving, and Capacity

  - The default keyboard lighting is Solid Reactive Simple: pressed keys
    light up in the selected color and fade out. Fn + Q still cycles effects;
    a saved lighting choice takes precedence over this reset default.
  - Space + Home switches between internal MIDI master and external DAW
    follower. Home starts/stops internal playback; in follower mode use the
    DAW's MIDI Clock, Start, Stop, Continue, and Song Position messages.
    Clock loss for two seconds stops playback after a clock has been received.
    External mode does not echo clock or transport back to the DAW.
  - Space + encoder sets actual timing swing (0–50%). Master MIDI clock
    remains steady at 24 pulses per quarter note. Swing applies to internal
    and external playback: odd steps are delayed, while even steps stay on
    the common clock. Ratchets subdivide each step's actual swung duration.
    Every allocated drum lane can play its full repeat count.
  - Converting, clearing, or resizing a track during playback keeps it on
    the shared transport. Its position follows the song step modulo its
    current length, including non-bar lengths. Other tracks keep playing;
    obsolete ratchets are cancelled only on the edited track.
  - After a stalled scan, all tracks resume at the current step together;
    missed steps and repeats are not replayed as bursts. Missed master clock
    pulses trigger Stop/Song Position/Continue recovery at the next timely
    straight-grid boundary. A following device must support those messages.
  - USB MIDI messages are batched and queued without blocking step playback.
    If the output queue fills, playback stops for all tracks and releases
    receiver notes. Restart after the USB connection can drain the queue.
  - Space + Enter saves on board. Stopping playback or exiting also saves
    changes. Startup loads the last valid save. On-board storage is 3 KiB:
    if the complete project will not fit, the old save is preserved and Esc
    continues flashing red/amber. Use computer backup for larger projects.
  - Full USB MIDI backup/restore is available through tools/seq_backup.py;
    see MANUAL.md for commands. Backup temporarily stops transport and locks
    sequencer editing. A transfer timeout releases the lock after 10 seconds.
  - Melodies hold 128 note events per track. CC automation holds 512 points
    shared across tracks, at most 128 per track, at full 16-bit step positions.
    Each allocated drum lane holds 16 step modifiers. Capacity failures flash
    Esc magenta and reject the edit; they never evict earlier melody notes.
  - Melodic step keys always use the default two-row layout so they do not
    overlap the note keyboard. A no-notes-held erase waits through the 300 ms
    double-tap window so a second tap can open the existing step in the editor.
  - Euclidean templates retain the actual captured hits. Changing fills or
    step count returns to generated Euclidean spacing. Empty melodic tracks
    can generate notes from their Euclidean palette without pre-entered notes.
  - On melodic tracks, every Euclidean hit retriggers notes spanning that step,
    with one-step gates and one probability decision for the whole chord.
    Source-note gaps remain rests. Disabling Euclid restores sustained playback.
    Step LEDs and the live step view show hits with an available note source.
  - Mutes and drum step modifiers survive leaving sequencer mode. Temporary
    CC gestures are not written into automation.
  - In CC mode, **Shift + PgDn** (single tap; **T** also works) toggles Automation / Manual for the target track.
    Manual sends live tweaks while pausing CC recording and playback, keeping
    saved points intact. Repeat the shortcut to resume automation at the next scheduled point.
    The choice is included in project saves and backups; other tracks are unaffected.

While Space is held, Home glows amber for internal master or cyan for DAW follower.

## Module Studio and interchangeable engines

The sequencer supports per-track **Euclid** modules and a recorded-chord editor, with a local browser editor for JSON import/export, chord audition, device installation and project backup. Start with [the module guide](modules/README.md), which includes setup, keyboard gestures and AI authoring examples. Run `python3 -m http.server 8765 --bind 127.0.0.1` here and open `http://localhost:8765/ui/`.

Module files configure built-in engines. New engine code still requires a firmware rebuild. Installing a supported module preserves patterns and automation. Use **Save on-board** after installation for persistence, or retain a full computer backup when the project exceeds on-board capacity.


### Clear the melodic Euclid palette

Hold **Shift + Alt/Option** (the palette capture modifiers) and tap **Delete**. In Windows layout with Mac keycaps, use **Shift + the Command-labelled key beside Space + Delete**. Ctrl may also be held. Clear acts immediately, empties all eight slots, cancels any pending palette capture, and removes pinning. Releasing the modifiers afterwards does not restore the old notes. You can keep the modifiers held and tap new notes to begin a fresh capture.

Generated hits without recorded notes stay silent until you populate the palette. Recorded step notes and rhythm settings remain; in Transpose phrase, recorded notes play without a palette offset. Clear releases this track’s currently sounding notes. The empty palette persists when saved and does not regain a starter note when Euclid is re-enabled. Other tracks are unaffected. The shortcut is available in melodic Euclid controls and is disabled during recording, step editing, chord capture, or a project transfer.

Live rhythm values can be edited directly in the UI. The ordered row is Q Steps, W Fills, E Rotation, R Probability, T Accent; bracket keys nudge rotation. Accent is 0–48 MIDI velocity points. Pulses and eligible hits are computed readouts. See the Cmd+/ manual for ranges and saving.


### Global key, scale, and quantization

While the sequencer is active, press **Ctrl + Space** to open Scale mode. Release any other held gestures first. Use the melodic note keys (Q, 2, W, 3, E, R, 5, T, 6, Y, 7, U) to choose C through B. Select **A Ionian, S Dorian, D Phrygian, F Lydian, G Mixolydian, H Aeolian, J Locrian, K Harmonic minor, L Harmonic major, ; Major pentatonic, ' Minor pentatonic**. The root lights orange, other scale tones green, and the selected mode violet. The virtual keyboard follows the same layout. When connected, Ctrl + Space also opens a live scale picker modal in the UI. It follows pending keyboard choices and closes after Enter applies or Esc cancels. Hide dismisses only the window. **Enter applies; Esc cancels; 0 toggles global scale on/off.** Picking a root or mode enables the pending scale. Playback continues with committed settings until Enter. The UI shows all eleven scales, highlights the pending choice and its notes live, and supports editing either the global scale or a track’s local scale. Harmonic scales have seven notes; pentatonic scales have five.

Open **Scale** in the hardware panel to choose root and mode visually, audition the pending scale using the browser sound, then Apply global. The header’s Sound mute applies to auditions. Global scale starts off, preserving older projects. Per-track choices are **Follow global**, **Local key / scale**, or **Unconstrained**, with nearest, downward or upward snapping. Nearest ties snap down; at MIDI boundaries the nearest in-range scale tone is used. Apply track commits those settings.

Quantization runs after melodic pitch generation and the accumulator, without rewriting stored notes or palettes. Disabling it restores source pitches. Scale changes release affected sounding notes; playback then continues in the new scale. Drums bypass it. Stored melodic chords are quantized voice by voice and duplicate output pitches share note ownership. Live step labels include quantization but remain before accumulator effects. Use Save or computer backup to persist global and track settings; standalone module files do not contain these host track settings. Old projects default to global off and Follow global; older firmware cannot load the new scale-setting extensions.


### Create a complementary chord track

Open **Harmony workshop** with **Ctrl + Shift + Space**, or its button in the hardware live view. The keyboard shortcut requires the latest firmware and a connected UI; it opens the workshop without stopping playback. Finish held-note/recording gestures first. Ctrl + Space still opens global scale editing. For melody capture, select a melodic hardware track and choose **A captured melody**. This requires harmony-capture firmware. Click **Capture melody** to freeze the complete loop; capture stops transport. The source uses the highest projected voice at each step, including Euclid timing, recorded overrides, palette choices and scale quantization. Probability, mutes and accumulator movement are excluded, matching the projected live-note view.

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

These shortcuts apply in the normal step view; leave latched Euclid, CC, scale or Chord Bank controls first. Recording and held-note chord placement keep their existing gestures. On a sustained melodic event, changing its pitch edits that whole event, including its other steps. A polyphonic step currently edits one selected voice; it does not transpose the entire chord. Hold editing of generated-only melodic pulses is not yet supported: place a recorded note override first.

The full drum editor still uses I Velocity, O Ratchets and P Spacing. The full melodic editor uses I Length, O Pitch and P Velocity; its existing chromatic pitch editing remains available. Save on board to retain edits.

### Chord step mode

Ctrl + Tab + F1–F12 selects Chord mode (Y/N confirms when requested). Note keys choose/audition the root; I/O/P/[/] select Length/Root/Velocity/Inversion/Type for the knob. Tap an empty step to place, tap an existing chord to select, or hold a step to edit the whole group. Del + step deletes every voice. The live UI includes a Mode selector, current/selected chord panel, chord names and sustain-tail labels; Cmd+/ documents all gestures. Fixed and in-scale types share the melodic playback engine. Recipes persist alongside rendered notes, with a 64-recipe shared limit and 128 note events per track. See [the manual](MANUAL.md#chord-step-mode) and [implementation design](modules/CHORD_MODE.md) for scale semantics and the first-slice limitations.

### Edit chord tracks in the workshop

Harmony workshop → **An existing chord track** → **Read chord track** reads saved chord recipes. Edit root, type, onset, length, velocity and scale context; write back with other notes and CC automation preserved, or derive bass/arpeggio onto another track. Writes include recovery backup, source-change detection and readback verification. New root-position scale progressions can retain hardware-editable recipes. Bank/inverted voicings stay ordinary notes with the editable count shown explicitly. This UI update uses the chord-mode firmware already built in the previous slice.

### Global tempo and scale conformity

**Ctrl + knob** changes global tempo from every active sequencer mode; add Shift for 5 BPM. External-clock followers keep their tempo in the DAW. In the Harmony workshop, read a chord track and use **Apply global scale to all chords…** for a cancellable, auditionable before/after preview. Roots snap to the nearest scale tone, triads/sevenths are rebuilt from scale degrees, and timing/velocity stay intact. Applying edits the draft; the existing backed-up write sends it to hardware and checks both source and global-scale freshness.


### Custom chord steps and scale quantization

Harmony workshop now preserves bank chords with declared roots as editable custom recipes. Read a Melody/Chord track to adopt exact, non-overlapping note groups after choosing a root and name. Custom recipes expose 2–8 signed intervals; root, inversion, duration and whole-chord velocity remain editable on the keyboard (I/O/P/[/] + knob).

Optional voice quantization uses a separate saved key/scale, merges coincident pitches and preserves the source voicing when switched off. Global and per-chord previews show the resulting notes before writing. Names and roots persist in custom chord project backups. Bank module JSON keeps authoring roots locally; its existing playback wire format carries notes only. See [the manual](MANUAL.md#custom-chords-bank-roots-and-note-group-adoption) for the complete workflow.

### Firmware management from the UI

Run `python3 tools/seq_server.py serve` from this keymap folder and open `http://localhost:8765/ui/`. **Firmware** in the header provides registered V1 Max releases, project backup, bootloader instructions/detection, flashing progress and MIDI verification of the installed version, build ID, board target and expected capabilities. Older releases remain explicitly labelled as legacy capability-only checks. See [the manual](MANUAL.md#firmware-management) for setup, registration and recovery.

### Scale-bound chord composition

With a track scale enabled, new chords use the scale's harmony. `]` + knob cycles Triad, Seventh, Ninth, Sixth and Add9. F natural minor + G produces G diminished; its scale seventh is G half-diminished. Select/hold a chord and tap **Shift + ]** to free that chord (chromatic root and unrestricted types); tap again to conform it. The exception is saved per chord and never becomes the next new chord's default. Length remains the default for held steps.

In the chord workshop, **Cmd/Ctrl + Shift + Enter** previews global-scale conformity; **Cmd/Ctrl + Alt/Option + Enter** previews voice quantization. Review and use the normal backed-up write to apply. The live view and virtual keyboard describe the new controls; see Cmd+/ for the full guide. Requires chord snapshot/recipe version 4 for new variations and per-chord Free metadata.

### Independent drum Euclid rhythms

Drum lanes now own their Steps, Fills, Rotation, Probability, Accent and ratchet settings. Live controls show the selected lane and reject stale lane edits. Melodic Euclid remains per track. Latched Euclid uses the normal F-key track/lane selection gestures. W / Fills = 0 silences generated hits; Delete + double-tap Alt/Option resets the current generator while keeping recorded notes. See [the manual](MANUAL.md) for clearing and project compatibility.

### Desktop window

Run the existing UI as a floating Electron window with Pin on top, remembered placement and a compact Performance view. On this Mac, open **Start SEQ.command**; setup and lifecycle details are in [desktop/README.md](desktop/README.md). No firmware update is required.

### Remove one palette note

Hold the **Euclid button + Delete**, without Shift, and tap **1–8** to remove that palette slot. The Euclid button is the same physical key used to open Euclid controls; on Windows layout with Mac keycaps it is the Command-labelled key beside Space. You can also click **Remove** beside a note in the live palette panel with updated firmware.

Remaining slots close up in their existing order and keep their pitches. A pin follows its surviving note; deleting the pinned note resumes cycling. Removing the last note leaves an empty palette. Recorded step notes and rhythm settings remain. Finish capture, recording or step editing before removing notes. Save with **Space + Enter** to keep the change.

A plain tap of the Euclid button exits a latched session. Holding it for palette capture, pinning, removal or knob edits keeps the session open. The existing **Delete + double-tap Euclid** generator-reset gesture remains available.

### Print a Euclid loop

While Euclid controls are latched, hold **Ctrl** and tap the **Euclid button**, then release Ctrl. On Windows layout with Mac keycaps, the Euclid button is the Command-labelled key beside Space. Do not hold Shift or use any other note/parameter controls during the gesture.

This replaces the current melodic track's recorded notes with the generated **whole track loop**, disables Euclid and the accumulator, and exits to normal note editing. Playback keeps its current position. The printed events have one-step lengths and include palette cycling/pinning, recorded-note overrides, scale-quantized pitches and Euclid accents. Track volume and scale settings remain active. Palette and rhythm settings remain available for reuse.

The destination always has the **current track length**. If the melodic Euclid cycle is longer, print only the portion inside the track. If it is shorter, repeat that cycle's pitches and recorded overrides until the track is filled, clipping the final repetition. For example, an 8-step cycle fills a 20-step track as 8 + 8 + 4 steps.

On **drums**, Ctrl + Euclid prints generated hits into the **selected lane**, including existing manual hits. The lane's generator is disabled and the edit session exits; sibling lanes retain their patterns and generators. Existing per-step velocity and repeat edits are retained on printed hits. This prints hit positions, not Euclid's live probability, accent or auto-ratchet effects. Short rhythms repeat to the track length; longer rhythms are clipped. If no storage lane is available, the operation leaves the pattern unchanged and flashes Esc magenta.

Printing captures the underlying pattern before probability skips, mutes and evolving accumulator movement; it does not record one randomized performance. Up to 128 note events fit, counting each chord voice. If the print would exceed that limit, Esc flashes magenta and the original notes and Euclid mode remain unchanged. Finish recording, step editing, chord/palette capture and project transfers first. This shortcut supports melodic Euclid tracks and the selected drum lane, but not Chord Bank tracks.

You can now add notes between the printed hits, remove notes, or edit their pitches, velocities and lengths. **Space + Enter** saves the result. Plain Euclid tap still only exits the controls; **Ctrl + Shift + Euclid + note keys** still replaces the palette.
