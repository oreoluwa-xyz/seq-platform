# Component contracts and evolution

The module is a composition, not a new engine enum for each combination. This first slice has two ordered stages: **rhythm → pitch**, followed by host-managed playback. Existing v1 engines remain compatibility adapters. Euclidean hit calculation and chord voicing are shared by legacy and composed playback; the existing track-note playback path supplies stored spans and palette behavior.

## Supported contracts

| Stage | Component | Wire ID | Version | Behavior |
| --- | --- | --- | --- | --- |
| Rhythm | rhythm.stored | 1 | 1 | Uses starts/spans from the underlying track material. |
| Rhythm | rhythm.euclid | 2 | 1 | Emits pulses with an ordinal from steps, fills, rotation or a captured template. Probability applies once per pulse; accent shapes velocity. Disabled generation falls back to stored rhythm. |
| Pitch | pitch.track | 3 | 1 | Uses stored melodic notes, with palette/root/pin for Euclidean playback and empty-track generation. Existing drum tracks retain their lane/palette interpretation. |
| Pitch | pitch.chord-bank | 4 | 1 | Selects up to eight pitches from the chosen chord slot, with transpose and inversion. |

All four pairings are supported. Stored rhythm + track pitches preserves sustained source notes. Euclidean rhythm + track pitches retriggers source spans with one-step gates. A generated hit without a source span uses held or last-picked melodic notes, or the palette root if no notes are picked; palette offsets and pinning apply to all these sources. Disabling Euclid restores recorded timing and rests. Either rhythm + chord-bank pitches uses one-step chord gates. Stored rhythm uses note starts for those chord triggers. Slot length is a stamping duration, not a generated gate length. Ratchet is the existing drum-only setting and must be zero with chord-bank pitches.

The platform owns clocking, transport reset, MIDI channel/velocity scaling, mutes, note deduplication, note-off ownership and resource limits. Components do not send MIDI, allocate memory, own transport timers, or alter stored events. Source pitches remain independent of playback transformations. Stamping is an explicit operation that writes notes; installing a composition preserves existing material. The legacy accumulator remains available in v1 but is bypassed in compositions until represented by an explicit component.

## Files and compatibility

V2 JSON uses `format`, `version`, `id`, `name`, and a two-element `components` array. Each component has `id`, `version`, and `params`. See [Four chord pulses](examples/four-chord-pulses.json). Unknown fields, component IDs, versions, ordering, extra stages, invalid parameters and incompatible pairings are rejected before mutation. The shared [schema](module.schema.json) accepts v1 and v2; the browser semantic validator and firmware codec enforce cross-field constraints.

V1 JSON/wire files and project objects remain readable. Existing drafts remain in original behavior until the user selects a composed rhythm. V2 import/read populates both component stages; export/save/backup retain them. Older project objects with no module trailer reset to the v1 Euclid adapter. Firmware must advertise the needed contracts before a v2 install. Old firmware can still connect and use v1 files; it cannot interpret new compositions.

## Wire protocol

Module objects remain 13–24, with the existing validation/staging/readback/rollback flow. V2 wire begins with `M, 2`, followed by rhythm ID and contract version, its parameter payload, then pitch ID and contract version and its payload. Stage order and payload shapes are fixed by the contract version; no native structs are serialized. Integers are little-endian, signed bytes use two's complement. Replies remain paged within the existing 64-byte MIDI payload.

- Stored rhythm has no payload.
- Euclid rhythm: enabled:u8, steps:u16, fills:u16, rotation:u16, probability:u8, accent:u8, ratchet:u8, template_active:u8, template_mask:u16.
- Track pitch: root:u8, count:u8, pin:u8 (255 cycles), then eight signed pool offsets.
- Chord pitch: the existing v1 chord-bank payload (selected, transpose, inversion, twelve count/velocity/length/notes slot records).

Read-only command 15 advertises `[catalog_version=1, module_version=2, max_stages=2, max_chord_voices=8, max_wire_lo, max_wire_hi]`, followed by `(component_id, contract_version)` pairs. Nonzero object/offset/payload is rejected. The current wire limit is 192 bytes. Command 10 retains its v1 envelope and source-kind fields; its maximum module size increases accordingly. Live flags add 64 for a composition and 128 for its Euclidean rhythm; legacy clients ignore these bits. Existing live snapshot sizes/versions remain unchanged.

## Resource model and future stages

Storage is fixed per track. The source configurations share a union; the rhythm descriptor is independent. No per-step pattern copy, allocator or unbounded event queue is introduced. Chord-bank output is capped at eight voices; stored track pitches retain the existing bounded source-event capacity. Saving all compositions and notes must fit the existing 3 KiB project allocation or use computer backup; a richer module format does not enlarge storage.

Future transforms (scale, mutation, voice leading, articulation) should receive bounded note batches and advertise distinct IDs/versions and resource costs. Modulation should use an explicitly bounded shared pool and address versioned component parameters. A future stage-order expansion requires a module-format version with a migration path; unsupported stages must fail closed, never be silently removed. Arbitrary graphs, feedback, executable imports and dynamic code loading are deliberately outside this slice.

Keyboard bank gestures are preserved. For a chord-bank composition, edit rhythm in the UI and install; Alt remains chord selection/audition/stamping. General component control mappings, additional transforms, modulation routing, and capture of generated playback are future increments.

## Roadmap: module-aware field guide

Add versioned help metadata to each component definition: purpose, parameters, keyboard gestures, musical examples and compatibility. Cmd+/ should assemble a guide for the selected track's installed rhythm and pitch components, distinguish installed settings from local drafts, and expose their controls on the keyboard map. New registered components should bring their help with them; imported data must remain safely rendered text, never executable markup. Preserve the searchable general manual and provide an explicit fallback when component help is unavailable.

First increment delivered: Cmd+/ → This track builds a guide from versioned component help metadata in `ui/component-help.mjs`, referenced by the component registry. Local draft and On keyboard are explicit sources; the latter follows live track identity and component types without claiming to read full parameters. Unknown component versions get a help-unavailable fallback. Content is rendered as text.

Remaining: component-driven keyboard-map overlays, current parameter values from verified device readback, and searchable component help alongside the general manual. The written guide still uses `MANUAL.md`; keyboard mappings remain explicitly maintained.

## Palette pitch contract, version 2

`pitch.track@2` adds `pitchMode` (`palette` or `transpose`) to root/pool/pin. Palette mode uses root in the track octave plus the selected pool interval on empty Euclidean hits. Recorded spans override with their exact pitches. Transpose mode preserves `pitch.track@1`: recorded or held/last-picked notes plus the interval. Recorded timing and drum behavior are unaffected. Capture either appends unique notes into free slots or replaces the pool; it does not erase recorded overrides.

Legacy Euclid JSON version 3 has the same fields as version 1 plus `config.pitchMode`. Wire version 3 is the v1 Euclid payload followed by one mode byte (0 palette, 1 transpose). Compositions retain outer wire version 2; pitch contract 2 appends that mode byte to the track-pitch payload. Encoders canonicalize transpose back to v1 / pitch contract 1 for compatibility. Decoders accept and validate both mode bytes. Old modules/projects decode as transpose, new defaults as palette. The module extension persists the mode without moving existing project fields.

Live v5 reserves bytes 952–953 for the viewed recorded-override bitmask, 954 for pitch-controls capability (1), and 955 for mode. Old snapshots have zero capability and remain read-only. Palette slots are 255 on recorded overrides in palette mode. Command 18: object=current melodic Euclid track; payload `[0, mode]`, offset 0 changes mode, or payload `[1]`, offset=absolute step removes that step's recorded pitches. Validation rejects stale tracks, transfers, competing edits, invalid modes/steps, and insufficient split capacity before mutation. Returns status 0 success, 2 busy/context mismatch, 3 invalid input, 4 no split capacity. Playback stays running; the affected track’s sounding notes are released and accumulator runtime reset.

### Add/replace capture workflow

Live v5 byte 956 is a capture-feature bitmask: bit 0 supports choosing Add/Replace, bit 1 indicates Replace (otherwise Add), bit 2 reports a rejected extra note at full capacity. Byte 957 counts retained entries in the in-progress capture. Pending entries include the complete proposed palette. Old firmware has bit 0 clear and retains replacement capture. Command 18 payload `[2, replace]`, offset 0 changes the per-track runtime capture preference (0 Add, 1 Replace); context validation is shared with pitch edits, and an active capture rejects changes. Unlike pitch edits, this preference neither releases notes nor changes project dirty state. Add is the startup/reset default. Append freezes the root and saved entries when capture begins, skips duplicates, fills at most eight slots, and preserves pinning. Duplicate-only/full captures do not reset playback. Capture preferences are transient; committed notes retain normal project persistence. UI visibility follows live visual mode 3 and melodic view, independent of the installed module’s name or whether rhythm playback is running.


### One-capture Replace and starter provenance

Ctrl + Shift + Alt/Option + melodic note keys forces Replace at the first note, without changing the per-track UI capture preference. The capture choice stays fixed until modifier release. An untouched starter palette also forces replacement on the first committed capture. A committed or explicitly learned palette is authored, including an intentional single C. Empty/cancelled captures do not change provenance. Imported modules and legacy projects are treated as authored; their notes are never guessed to be defaults.

Live v5 byte 958 bit 0 advertises this shortcut/provenance support; bit 1 indicates an untouched starter palette. Byte 956 continues to report the active capture choice when capturing and the UI preference otherwise. UI help shows the starter replacement behavior when applicable.

Project track objects with untouched palettes prepend `P`, version 1, flags 0 to their trailing module extension. The marker requires a valid following module. Authored palettes retain the existing unframed module encoding. Readers accept legacy objects with or without module extensions, treating both as authored. Standalone module formats are unchanged. This costs three saved bytes per untouched track (up to 36 per project); older firmware cannot load tracks with this new marker.


### Empty palettes and Clear

Shift + Alt/Option + Delete clears a melodic Euclid palette immediately, including a pending capture; Ctrl may also be held. The shortcut consumes Delete without arming track deletion and rejects competing recording, step editing, chord capture and transfers. Sounding notes on the affected track are released; recorded events, rhythm settings and other tracks remain. Subsequent note keys can begin a new capture without releasing the modifiers.

Empty palettes have count 0 and pin 255, are authored, and persist through project/module codecs and on-board saves. Generated hits with no recorded source have no voices, in both pitch modes; recorded spans retain their pitches with a zero palette offset. Enabling/synchronizing Euclid does not reseed an authored melodic palette. Live byte 958 bit 2 advertises Clear support, and step palette indices are 255 when the pool is empty. Module JSON/schema and C/JS wire codecs accept 0–8 offsets. Older firmware rejects empty modules rather than inventing notes; use updated firmware to restore empty palettes.


### Direct live rhythm editing and ordered controls

Live rhythm byte 609 bit 0 advertises command 19; bit 1 advertises Q Steps, W Fills, E Rotation, R Probability, T Accent. The bracket keys now nudge rotation; F remains the drum ratchet selector. Palette capture intercepts melodic notes before parameter keys as before. The UI follows the connected firmware mapping and keeps older firmware read-only.

Command 19: object is the viewed track (0–11), offset is the child-view flag (0/1). Payload is exactly `[parameter, valueLo, valueHi, expectedLo, expectedHi]`, little endian. Parameters 0–5 are Steps, Fills, Rotation, Probability, Accent, Enabled. Expected value must still match hardware. Status 0 succeeds, 2 rejects stale context/value or competing edits, and 3 rejects malformed/out-of-range requests. Recording, held gestures, project transfers, and step/capture edits block writes.

Steps is 1..track length; Fills 0..steps; Rotation 0..steps-1; Probability 0..100; Accent 0..48 MIDI velocity points; Enabled 0/1. Reducing Steps clamps fills and ratchet threshold and wraps rotation; Steps/Fills changes clear captured-template mode. Enabled does not auto-seed fills. Edits preserve transport, palette and stored notes, and mark project state dirty. Both composed rhythm + track pitch and rhythm + chord bank are supported. Inputs retain their nodes/focus across snapshots, commit on Enter/change and cancel on Escape; displayed old values protect against edits made stale by hardware.


## Global musical context and scale.quantizer@1 foundation

`seq_scale.h` defines a pure bounded quantizer and root/mode contexts. It allocates no memory, emits no MIDI, and preserves source data. The host currently places it after melodic generation and the legacy accumulator; selectable arbitrary transform ordering is not yet part of the two-stage module wire format. Future harmony providers can consume the same root/mode context independently of quantization. Chord-bank and drum playback bypass this transform.

Contexts support Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian and Locrian (IDs 0–6), harmonic minor (7), harmonic major (8), major pentatonic (9) and minor pentatonic (10). Scale IDs are stable across projects. Per-track mode is Follow (0), Local (1), Off (2), with direction nearest/down/up (0/1/2). Nearest ties go down; boundary cases fall back to the nearest legal MIDI pitch. Scale changes release affected notes using existing ownership cleanup. Quantized notes are registered under their original base pitches, so many-to-one snapping preserves correct note-offs.

Live v4/v5 rhythm metadata at bytes 622–635: capability version 1, enabled, root, kind, track mode, local root, local kind, direction, keyboard editor active, pending enabled/root/kind, revision u16. Byte 636 advertises the supported scale count (11); zero on earlier firmware means 7. The UI disables unavailable scale choices. Visual mode 10 identifies Scale editor. Cmd 20 global payload `[0,enabled,root,kind,revisionLo,revisionHi]`, object/offset 0; track payload `[1,mode,root,kind,direction,revisionLo,revisionHi]`, object track and offset child flag. Reject stale revision/context or held edits with status 2, invalid values with 3. Version 1 global extension `G,1,enabled,root,kind` is omitted for defaults. Track extension `S,1,mode,root,kind,direction` precedes existing provenance/module trailers and is omitted for defaults. Legacy objects remain readable; nondefault scale extensions require updated firmware. Snapshot polling and unknown-mode fallback must use the updated UI.


### MIDI CC live dock

Rhythm metadata byte 637 = 1 advertises read-only command 21. It freezes a 192-byte CC snapshot using the same paging token/timeout as other visual commands (token byte 62, model byte 63). Header: byte 0 version 1; byte 1 flags active/latched/temporary/running/held-track-override (bits 0–4); byte 2 target track; byte 3 selected slot or 255; byte 4 slot count (up to 16). Starting at byte 64, each slot is 6 bytes: QMK keycode u16 LE, controller, resolved MIDI channel (0–15), current value, held flag. Values include automation playback and temporary restoration. Older firmware has no CC dock; unknown values are not guessed. UI fetches CC pages only while the visual mode is CC, and discards partial/superseded snapshots.


## Harmony authoring providers, first slice

`ui/harmony.mjs` exposes versioned `harmony.scale@1` and `harmony.bank@1` providers. Each supplies named bounded chord candidates; the host owns melody capture, harmonic rhythm, octave/inversion search, scoring, preview, event budgeting and installation. Adding another chord vocabulary should add a provider implementing that candidate contract, without coupling it to Euclidean timing. This is an authoring layer that renders normal recorded notes; these providers are not new firmware pitch components or arbitrary imported executable modules.

Read-only project objects 25–36 expose tracks 0–11 as melody source version 1. Header (8 bytes): version, source track, loop length u16 LE, global enabled/root/kind, MIDI channel. Each step has pitch (0–127 or 255 rest) and onset (0/1). The highest projected voice is used. Source rendering shares `seq_projected_step_notes` with the live orbit, and does not advance probability or accumulator state. Maximum size is 4104 bytes. Metadata byte 638 = 1 advertises support. Source reads use command 1 and the existing transfer lock/scratch; first offset 0 freezes the entire source, later offsets reuse it. Other object/transfer operations invalidate the source cache; writes to these objects are rejected.

The initial output codec creates a normal melodic track, stored rhythm + track pitch, explicit unconstrained scale setting, no drum layer or CC automation. Preview rejects over-budget results. Installation validates first, backs up the whole project, replaces only the chosen destination, verifies exact readback, and attempts destination rollback on failure.

Next: harmony recipe import/export and provenance, selectable chord qualities and melody-anchor strategies, per-chord overrides, additional provider vocabularies, and a separately versioned live harmony transform contract. Live source linking and automatic regeneration are not included in this slice.


### Harmony workshop: chord-first authoring and derived note patterns

`progressionCandidates` uses the existing versioned provider vocabulary. `buildProgression` applies explicit candidate selection, duration, inversion and octave per position, producing named timed chords. `deriveNotes` independently renders chord holds, up/down/bounce arpeggios, or lowest-voice bass, with note stride and output octave. The existing bounded track encoder and transactional backup/install path serve every output. No new firmware runtime engine or per-track allocation is needed for these browser authoring stages.

Live visual byte 638 is now capability bits: bit 0 supports melody capture; bit 1 supports the Ctrl+Shift+Space workshop request counter in byte 639. The counter increments once per eligible press, consumes Space through release and leaves transport untouched. The UI establishes a baseline on connection and reacts to subsequent changes, including wraparound; it does not replay stale requests or interrupt an existing dialog. Ctrl+Space scale editing is preserved.

Scale candidates, including inversions, can retain C/1 or C/2 hardware recipes when editable output is selected. Bank voicings with explicit roots can now retain custom recipes; unknown roots remain ordinary notes with an explicit editable count. Further work: broader provider identity and live dependencies.


### Planned chord track editing mode

The direct-step slice now provides a **Chord** editing mode alongside Drum and Melody, backed by the existing bounded polyphonic event storage. Internally it is still a melodic track with a persistent chord-editor flag. Workshop C/1 through C/3 recipe round-trip and explicit custom adoption are implemented. Additional executable providers remain outside this contract.

- A step contains a chord event with root and voicing intent, plus its rendered MIDI notes. Providers supply chord choices; rhythm generation remains independent.
- Hold a step + knob changes its root through the active scale. Explicit controls select chord quality/bank entry, inversion, spread and velocity. Highlight every chord tone on the physical and virtual keyboard and distinguish the root.
- Chord Bank, scale-degree chords and future providers share a versioned voicing contract. Quantization policy must be explicit: move the root while preserving provider intervals, or request a scale-derived voicing. Silently quantizing every voice can collapse notes and change chord identity.
- The workshop should write both rendered notes and a versioned chord recipe to the destination. Reading an existing track should recover the recipe when present; arbitrary recorded polyphony remains editable as custom voicings without guessing a chord identity.
- Melody/chord conversion preserves timing and velocity. Chord-to-melody chooses an explicit voice or arpeggio/bass pattern. Do not erase other voices just by switching editing mode; separate view changes from destructive conversion.
- Before adding persistent recipe fields, measure capacity, version project backups and define migration for existing melodic/chord-bank tracks. Legacy devices still play baked note events. Live cross-track dependencies remain a later feature.

The hold-step slice reuses the existing drum modifier and melodic event structures, with a single transient gesture state. Live visual byte 61 bit 4 distinguishes temporary hold editing from a latched editor. Quick taps on existing steps commit on release; encoder turns or the hold threshold enter the editor; release exits only the temporary editor. Source-changing transfers cancel pending holds. Drum modifier edits now mark the project dirty for saving.

Detailed interaction and implementation proposal: [Chord mode](CHORD_MODE.md), covering Root / Type / Length controls, grouped events, scale policies and workshop round-tripping.

### Chord editor protocol and persistence

Live capability byte 638 bit 2 enables Chord editor support (bit 4 adds inversion support; current combined capabilities = 63). Command 22 reads a frozen 192-byte version-2 snapshot, in 64-byte chunks using the token in byte 62 and model 1 in byte 63. Bytes: 0 version, 1 track, 2 enabled, 3 root, 4 type, 5–6 duration, 7 parameter (root/type/length/inversion), 8–9 selected onset or 65535, 10 error (range/capacity/foreign overlap), 11 recipe count, 12 voice count, 13 hold, 14–15 scale root/kind, 16–23 pitches, 24–25 viewed bar, 26–27 loop length, 28–29 chord-mode track mask, 30 current inversion. Bytes 64–191 hold sixteen 8-byte step records: flags (present/onset), root, type, start u16, length u16, inversion. Version 1 snapshots imply inversion zero and support parameters 0–2; the UI accepts both versions. Visual mode 11 is chord-step.

Command 23 selects editing mode: object=track, offset=0, one byte 0 Melody / 1 Chord / 2 Drum. It uses the existing confirmation flow; success acknowledges the request, not confirmation. It rejects busy/held/armed/inactive/wrong-track requests.

Track project objects gain optional C/1 before their module extension: enabled, count, current root/type/length-u16/velocity/scale-root/scale-kind, then count records of start-u16/length-u16/root/type/velocity/scale-root/scale-kind. Track ownership is implicit. Validate non-overlapping spans and exact baked-note membership before apply; total recipes may not exceed 64. Enabled recipes require a melodic non-wrapper track with a composed stored-rhythm/track-note module. Older objects clear chord mode on import. Unsupported C versions/types are rejected; backward baked-note export remains future work.

### Workshop recipe readback

`ui/chord-project.mjs` validates C/1 and C/2 roots, types, saved scale context, note membership, spans and the stored-note module. It reconstructs note/recipe blocks while retaining CC automation and other settings. `ui/chord-workshop.mjs` exposes explicit chord editing and independent companion derivation. A source snapshot comparison prevents stale edits from overwriting a changed source; installation keeps the existing backup, validation, readback and rollback flow. Shared recipe capacity is enforced by firmware before apply. Inversion support adds the backward-readable C/2 contract described below.

### Global tempo and chord conformity

Live capability byte 638 bit 3 advertises Ctrl + encoder tempo (bit 4 adds inversions, making combined bits 31). The tempo handler runs before both CC and normal encoder dispatch, so it cannot alter an active editor’s parameter. Shift accelerates to 5 BPM; external clock and transfers consume the gesture without changing internal tempo. No RAM allocation is added.

`conformRecipesToScale` is a pure browser recipe transform: nearest allowed root (ties down), three voices -> type 10, four voices -> type 11, saved global context, unchanged start/length/velocity/inversion. It validates the entire proposal before returning. The workshop holds a separate preview and only swaps it into the draft on explicit apply. Source-track and global-scale checks precede hardware writes. Ordinary events outside owned recipe spans remain unchanged.

### Inversion recipe extension

C/2 extends C/1 with an inversion byte after scale-kind in the current draft and each record (12-byte header, 10-byte records including all existing fields). The canonical writer uses C/2 only if a draft or record has nonzero inversion, otherwise C/1. Both versions are accepted; C/1 explicitly resets inversion to zero even over an existing C/2 draft. Unknown versions are rejected before apply. Inversion is 0–2 for triads, 0–3 for sevenths. Render the provider's pitches, repeatedly raise the lowest by 12 and sort, and reject MIDI overflow or duplicate pitches before editing.

Static recipe records grow from 10 to 12 bytes including alignment, for 152 extra bytes across 64 records and twelve drafts. Serialized C/2 adds one byte per record and draft. The pool and event limits are unchanged. Capability bit 4 gates inverted recipe writes in the workshop; older firmware still receives C/1 root-position recipes and ordinary baked notes for inverted output. New code never infers root identity from an inverted bass.

### Custom chord recipes and voice quantizer (C/3)

C/3 retains C/2 fields. After inversion in each draft/record: quantize u8 (0/1); when on, quantize-root u8 (0–11) and quantize-kind u8 (0–10). For type 12 only, follow with custom-count u8 (2–8), count signed int8 intervals (−127…127, ascending/unique), name-length u8 (1–12) and that many plain ASCII bytes. Root remains explicit; intervals may be negative. Provider scale and quantizer scale are independent. Rendering applies inversion first, then nearest-note quantization (ties lower) and duplicate elimination. An occupied inversion octave is skipped to keep voices distinct before quantization. Unknown versions/type IDs, malformed names, bounds, overlap and recipe/note mismatches reject restoration before mutation.

C/3 is canonical only if a draft/recipe is Custom or quantized. Otherwise C/1 or C/2 remains canonical. Older imports reset custom/quantizer fields. Capability byte 638 bit 5 advertises custom/quantized chords and ] + knob velocity; combined bits = 63. UI authoring capability-gates recipe output so older devices receive supported recipes plus ordinary note events for unsupported voicings.

Command 22 version 3 is 448 bytes: the first 64-byte header retains prior fields, adds 31 quantize, 32 source voice count, 33–40 signed custom intervals, 41–52 zero-padded name, 53 velocity, 54 quantize-root, 55 quantize-kind. Sixteen 24-byte cells follow: previous 8-byte root/type/span/inversion fields, 8 quantize, 9 source voice count, 10–21 name, 22–23 reserved. Unused cells are zero. Name/interval fields are meaningful for Custom. Token/model stay at header 62/63. UI accepts legacy 192-byte v1/v2 snapshots and fetches all seven pages atomically for v3.

Bank JSON slots accept optional root (MIDI 0–127 or null). Root/name are local authoring metadata, excluded from the bank playback wire format; chord recipe exports retain explicit identity. Bundled bank examples declare their known roots. `customRecipe` and `recipeNotes` define the common host contract for custom/bank/adopted groups; the existing firmware custom renderer executes the bounded interval data. Imported module JSON never adds executable code.

### Drum lane rhythm overrides

Each allocated drum lane can override its track Euclid generator. The lane owns rhythm settings and targets only its own drum note. Unconfigured lanes retain legacy track-generator routing for project compatibility. Module JSON continues to describe the track, while project objects preserve lane overrides. Importing a stored-rhythm or non-Euclid module bypasses those generators without deleting them.

Project tracks optionally include `L/1` after CC events and before `S`, `P`, `C` and module extensions: tag `L`, version `1`, little-endian 12-bit lane mask, then one 26-byte Euclid record per set bit in ascending lane order. Each record uses the existing 24-byte track Euclid encoding followed by pitch mode and palette-authored bytes. Pool count is one and its first value is the lane index. Generator-only lanes count toward the 48-lane limit during restore preflight. Older firmware rejects this extension; use the complete project backup for lane settings, not a module JSON export.

Live rhythm metadata byte 609 bit 2 announces lane context. Byte 619 contains the selected drum lane (0–11), or 255 for melodic track controls. Command 19 uses context `2 + 2*lane + child` for drums, or `child` for melody. A mismatched lane/context returns stale status 2, even when the old value happens to match. Existing metadata without that bit retains the original track-context interpretation in the browser.
