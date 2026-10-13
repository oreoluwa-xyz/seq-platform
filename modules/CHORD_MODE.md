# Chord mode — interaction and implementation

Status: direct steps, workshop round-trip, inversions, custom/bank recipes and optional quantization implemented; later provider/workshop work below remains planned. Entry: Ctrl + Tab + F1–F12, or the live Mode selector (Y/N confirms conversion). The built-in recipe supports the ten fixed types plus two in-scale types, a 64-entry shared pool, and save/load alongside baked notes. The live parameter panel is read-only; edits use the hardware. Chord mode is a musical editing mode alongside Drum and Melody. It reuses polyphonic MIDI playback, while giving a group of notes a root, chord type and common duration. It is separate from the existing Chord Bank control layer and the browser Harmony workshop.

## Fast placement

Maintain a current chord per track: root including octave, provider/type, voicing, velocity and length. Show it continuously while in chord mode. With a scale enabled, start with an In-scale triad; without one, start with Major. Default duration: four sixteenth-note steps (one beat). These are entry defaults, not notes automatically placed on the track.

| Gesture | Behavior |
| --- | --- |
| Tap a note key | Prepare a new placement at this root; audition while the key is down. This leaves any existing-step selection without modifying it. The root persists after release. |
| O, then knob | Select/adjust Root. Advance through the active scale, or semitones with scale off. |
| ], then knob | Select/adjust Type. Browse the provider's named chord choices. |
| I, then knob | Select/adjust Length in sixteenth-note steps. Shift accelerates length changes. |
| P, then knob | Select/adjust Velocity for all voices. |
| [, then knob | Select/adjust Inversion, preserving root and type. |
| Tap an empty step | Place the current chord. |
| Tap a populated step | Select that chord and load it into the current-chord controls. Do not delete it. |
| Hold a note key + tap a step | Stamp the current chord at that root; explicitly replace a chord starting at that step. |
| Hold a step + knob | Temporarily edit that chord's length. Each new hold starts on Length. Release finishes the edit. |
| While holding the step, tap I / O / P / [ / ] | Choose Length / Root / Velocity / Inversion / Type for that hold; the knob edits the held chord. These keys must not end the hold. |
| While holding the step, tap a note key | Set that chord's root directly. |
| Del + step | Delete the selected chord event, including all its voices. Consume Delete before normal track-clear handling. |
| Double-tap a populated step | Latch the full chord editor for root, type, length and inversion controls. Esc leaves it. |

Scope must be visible: **Current chord** means entry defaults only; **Editing step 05** means an existing chord. Tapping a populated step selects it; I/O/P/[ and knob then edit it as a unit. Preparing a new root or leaving the editor returns to the current-chord placement state; tapping an empty step stamps those defaults. Changing defaults must never rewrite earlier placements.

Example: tap D, select Minor using O + knob, select eight steps using P + knob, then tap steps 1 and 9. Hold step 9 and tap G to make the second chord G minor. All its voices change together; its duration remains eight steps.

This differs deliberately from drum tap-to-delete. A chord has multiple voices and a duration, so deletion should be explicit. The mode label and help must make that difference clear. Retain existing Drum and Melody gestures unchanged.

## Type and scale policy

Providers expose named choices and a bounded renderer. Start with:

- **Fixed types:** major, minor, diminished, augmented, sus2, sus4, dominant 7, major 7, minor 7 and half-diminished 7. Changing root transposes the interval recipe; do not quantize each voice independently.
- **In-scale:** triad and seventh by stacking alternate scale degrees. In a pentatonic scale this is alternate-degree stacking, which need not produce a conventional major/minor chord. Show the resulting pitches.
- **Bank/custom:** use a saved bank voicing or explicit intervals. A bank chord needs a declared root; the lowest note of an inversion is not necessarily its root. Preserve imported voicings and ask for a root in the editor when unavailable, rather than guessing and silently changing it.

Root selection respects the active track scale policy. For direct out-of-scale note keys, display the resolved root immediately; encoder movement visits the next allowed root. Fixed chord types can contain out-of-scale tones: show them, preserving the selected quality. An In-scale type derives all its tones from the scale.

New chords and deliberate edits use the chosen scale context. Existing stored chords retain their rendered notes when the global scale changes. A later explicit Reharmonize operation can update a selection or track with preview. Runtime following of global harmony is a separate future transform contract.

## Length, tails and overlaps

Use the existing sixteenth-note step unit. Show both musical duration and step count, e.g. **2 beats · 8 steps**. Preview a strong onset cell followed by a lighter sustain tail. Selecting/holding a tail selects its owning chord, and the UI shows that owner's start step.

For the first slice, use one progression lane per chord track and no overlapping chord events. A new explicitly stamped onset inside an existing sustain shortens that preceding chord to the new onset; the live view reflects the committed shortening. Extending length stops at the next onset or loop end. No implicit loop wrapping. Layer overlapping harmonies on another track initially.

Imported polyphony with overlapping events or unequal voice lengths remains valid melodic playback. Do not silently normalize it into chords on mode change. Offer an explicit import/grouping action; exact same-start/same-end note groups can become Custom chords after validation, while ambiguous passages stay melodic until the user resolves them.

## Feedback

- A compact contextual panel: **Dm7 · D3 F3 A3 C4 · 8 steps**, selected parameter, scale policy and target step.
- Physical and virtual note keys show root and remaining chord tones in distinct colors. The virtual display includes octaves; an inversion must not relabel its bass as the root.
- Step onsets show chord names; sustain cells show continuity. The orbit can show names at onsets and sounding chord tones at the playhead.
- Root/type/length edits preview the complete chord and its tail. Capacity failure keeps the previous chord unchanged and explains the limiting resource.
- Audition uses the existing sound mute and MIDI note-lifecycle rules. End the previous audition before changing root/type, and release every audition voice on key release, Escape, track switch, transfer, disconnect or transport reset.

## Representation and module boundaries

Current recorded events contain pitch, velocity, start and end only. Editing the first matching event cannot implement root/type/length/inversion changes for a whole chord. Add explicit chord-group metadata without replacing the whole sequencer engine.

A versioned chord recipe records track/start/length, root, provider/type identity, voicing parameters, velocity and scale context where needed. Keep rendered notes in the existing melodic event store for playback and legacy export. Avoid using array indices as durable voice ownership: inserting/removing notes can invalidate them. In the initial non-overlapping lane, exact group timing plus validated membership can identify the owned voices. Every group mutation updates metadata and all owned notes transactionally.

Keep these responsibilities separate:

1. **Timing:** stored chord onsets and duration; later Euclidean onset sources.
2. **Chord provider:** root + type + musical context -> up to eight pitches and a label identifier.
3. **Voicing:** inversion/register/spread applied to those pitches.
4. **Renderer:** emit bounded note events and maintain MIDI note-off ownership.
5. **Editor/workshop:** manipulate the same recipes, preview and serialize them.

Use stable provider and type IDs with explicit versions, not UI list positions. Firmware providers remain supported built-in components; this does not introduce arbitrary executable imports. Future vocabularies can extend the provider contract. Unsupported recipes must retain baked-note playback and be displayed as unsupported/custom rather than reinterpreted.

The workshop should round-trip recipes and rendered notes. It can then author a chord track, read/edit it, or derive a separate arpeggio/bass track. Derivation produces an independent note track unless a future live-link feature is explicitly enabled.

## Capacity and persistence

The current limit is 128 recorded note events per track, not 128 chords. With nothing else recorded, that permits at most 42 three-note chords or 32 four-note chords. A chord's sustain length does not multiply its event count. Never raise this limit as an accidental part of the UX change.

Prefer a bounded shared metadata pool over allocating a large recipe table on every track. The pool has 64 shared 36-byte records and twelve 36-byte current-chord drafts, including alignment. Account separately for metadata, custom voicings, rollback scratch, labels and MIDI voice state.

Version the project format to carry chord recipes alongside rendered notes. Validate that recipes and baked groups agree before restoring. Backups to older firmware must use an explicit baked-note export; do not send new metadata to an old decoder. Group editing, clearing, copying, length doubling, trimming and melodic edits all need to update or explicitly detach recipes. Switching the editing mode alone never deletes voices. Drum-to-chord conversion retains the existing explicit destructive-conversion safeguards.

## Delivery slices

1. **Implemented — direct chord steps:** scale/fixed-type providers, current-chord placement, whole-group root/type/length/inversion editing, non-overlapping durations, root/tone highlighting, bounded metadata, safe note lifecycle, persistence and manual help. Establish a discoverable UI mode selector; choose a hardware mode-entry gesture only after checking Tab/Caps/Shift and recording conflicts.
2. **Workshop round-trip (C/1 through C/3 implemented; explicit custom adoption implemented):** emit/read recipes, import unambiguous custom chord groups, derive arpeggio/bass from an existing chord track, preserve provider identity in project backups.
3. **Voicing and generative timing:** spread, bank providers with explicit roots, Euclidean chord palettes and deliberate chord-onset overrides. Define retrigger/sustain behavior before connecting Euclidean pulses to chords.

Full roadmap validation (including later slices): all voices move together; one-step and loop-end lengths; sustain-tail ownership; insertion inside a sustain; capacity failure rolls back both stores; held I/O/P/[ does not prematurely end editing; old double-tap behavior; note-off cleanup across root/type edits and transitions; scale boundaries; custom/inverted roots; save/load migration; copy/clear/trim consistency; recipe and baked-note agreement; unsupported-provider fallback.

## First-slice boundaries

The workshop now reads and edits C/1 through C/3 recipes and preserves eligible scale recipes, including inversions in new output. The existing Chord Bank provider still authors baked notes; workshop bank voicings with declared roots now become custom recipes; older baked-only voicings need explicit adoption. Custom and bank imports now use explicit interval recipes; unknown provider fallback and baked-only export remain planned; the current codec rejects unknown recipe versions or types before restore. Generic edits/copy/trim detach invalidated recipes after validating exact pitch, velocity and span membership. They never relabel unowned notes as a known chord. Re-entering Chord mode selects recorded timing; installing another module exits Chord mode. Record-arm retains the existing ordinary-note recorder, so grouped chord authoring uses step placement.

An empty step tap places immediately and selects it. A short populated-step tap already latches I/O/P/[ editing; a double tap leaves that same editor selected. A held edit ends on release, with changes retained. In-scale types explicitly use the saved global/local context even if the ordinary root constraint is off. Fixed stored voices bypass playback quantization and the accumulator, so editing length or changing the global scale cannot change their chord quality.

## Workshop recipe integration

The browser decodes the full track object by field boundaries and validates chord ownership before editing. It preserves the raw non-note prefix, CC automation and scale/palette extensions, and the stored-note module. A no-op read/write round-trip preserves all bytes. Foreign notes outside recipe spans survive edits; overlap is rejected. Source comparison against the recovery snapshot prevents writing an edit based on a changed track. Firmware remains authoritative for shared pool admission.

The existing-track editor supports root/type/inversion/start/length/velocity and per-recipe scale context. Derived bass/arpeggio tracks consume recipes but write independent ordinary note events to another destination. Provider output may carry a known recipe; eligible scale voicings receive C/1 or C/2 metadata. Unsupported output and banks without declared roots stay baked and the UI reports the editable count. No runtime provider is downloaded. Inversions add 152 bytes of static recipe storage and reuse command 22 with snapshot version 2. Custom voicings use the C/3 extension below; broader runtime provider identity remains future work.

## Per-chord inversions

Implemented as a voicing stage after the fixed/in-scale provider. Each turn raises the lowest voice by 12 semitones and re-sorts; root and type stay explicit. Inversion ranges from zero to voice count minus one. A type change clamps it to the new voice count; range/ownership failures leave both metadata and recorded voices unchanged. Length and inversion edits use the saved context. Global-scale conformity preserves inversion.

C/1 remains canonical when every recipe and draft is root position. C/2 adds one inversion byte after each scale-kind field; C/1 imports initialize inversion to zero. Live snapshot v2 uses header byte 30 and step byte 7 for inversions. Capability byte 638 bit 4 gates workshop writes. Older firmware receives only root-position recipes; new inverted output remains baked notes there. Generated scale recipes retain the provider's root even when the bass differs.

Next: spread/register controls and Euclidean chord onsets with defined sustain/retrigger behavior.

## Custom provider and quantizer slice

Implemented: built-in type 12 is a custom interval recipe (explicit MIDI root, 2–8 ascending signed intervals, 12-byte plain-text name). Bank candidates may supply this recipe only when their root is explicitly declared; authoring roots are optional in bank JSON and are absent from the existing bank playback wire payload. A generated chord snapshot is independent of later bank edits. This is a reusable bounded renderer contract, not arbitrary executable provider loading.

The workshop explicitly adopts non-overlapping same-start/end/velocity polyphonic groups. It retains foreign events and CC automation and requires a root and preview. Type Custom allows interval/name editing. Hardware uses I Length, O Root, P Velocity, [ Inversion and ] Type. No live event or recipe-pool limit is increased.

Quantization runs after the provider and inversion stages. It has a separate saved key/scale from the provider, snaps nearest with ties down and merges coincident pitches. Disabling it restores source voicing. Global conformity rebuilds built-in scale stacks; custom recipes use voice quantization to retain arbitrary intervals and voice count intentions. Actual sounding count may shrink when pitches merge. Dynamic global following remains separate future work.

Recipe records now occupy 36 bytes including alignment: +1,824 bytes across the existing 64-record and 12-draft pools compared with the inversion slice. No dynamic allocation is used. C/3 stores the new fields only when needed, preserves C/1 and C/2 imports, and validates baked-note ownership before restore. Live capability bit 5 and snapshot version 3 gate support. See COMPONENTS.md for wire details.

## Scale-bound composition (current)

With an enabled global/local track scale, root edits and new placements default to scale-bound harmony. Type cycles stable provider IDs 10 (triad), 11 (seventh), 13 (ninth), 14 (sixth), 15 (add9). They select scale-degree indices [0,2,4], [0,2,4,6], [0,2,4,6,8], [0,2,4,5], [0,2,4,8]. Fixed fully diminished seventh is ID 16; Custom retains ID 12. Every scale family shares the bounded renderer and note-event preflight.

Shift + ] toggles only the selected/held chord's `scale_free` policy. Freezing to a custom recipe retains the exact currently rendered notes, resets the inversion to as-supplied, and disables voice quantization. Free roots are chromatic; Type cycles fixed/custom choices. Returning to a scale rebuilds a triad/seventh, while the next new placement always clears the exception. Length/inversion/velocity edits retain the recipe's saved context. Existing events are not rewritten solely by changing the global scale.

C/4 adds one policy byte before the C/3 extras for each draft/recipe. It is emitted only for Free chords or new type IDs; old C/1–3 remain readable with `scale_free=0`. Command 22 snapshot v4 remains 448 bytes: header 56 Free, 57 effective track scale enabled; per-step flags bit 2 Free, byte 22 scale root, byte 23 scale kind. Capability byte 638 bit 6 advertises support. Browser writes gate extended types/policies on that capability. Recipe and rendered-note validation still rejects partial, inconsistent or capacity-exceeding updates atomically.
