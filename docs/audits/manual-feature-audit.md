# Manual feature audit — 2026-10-19

## Baselines and publication gap

The public manual was published from `7b9f4ea`, whose firmware baseline was
`d39eaa8` / v0.1.11. Current sequencer development includes `fde36b7` (Space
navigation/length) and `ae17744` (whole-track mute), targeting firmware
2026.10.16.2 for the prepared v0.1.12 preview. Those feature commits were not
ancestors of the published manual. Sharing components prevents duplicate text;
it does not bring unmerged features into a publication.

This audit branch combines those feature commits with the web manual. Merging
it will also integrate the pending sequencer features; it is not a docs-only
branch. No firmware release or installation is implied.

## Findings and corrections

| Area | Evidence | Correction |
| --- | --- | --- |
| Follow and duplication | `sequencer.c` Space root handler; `test_navigation_double_and_follow` | F is Follow; = repeats contents, either Shift creates blank space. |
| Actual track length | resize guards, `seq_track_set_steps`, navigation length/guard tests | Ctrl+Space+knob changes 1 step, Shift 16; shrink trims content; scale/harmony wait for release without turning. |
| Whole-track mute | `seq_track_toggle_mute`; gesture, playback and storage tests | Shift+Space+F1–F12 gates all lanes/layer, retains individual mutes and persists in saves/TEMP. |
| Save and clock source | Space Enter/Home branches in `sequencer.c` stop transport | Keyboard help now states the stop behavior. |
| Chord banks | `seq_module.c` rejects new banks; `discard_bank` only supports legacy migration | Removed instructions to install/perform hardware banks. Retained local bank authoring in Harmony workshop. |
| Chord palette reference | `controlsFor` required a live palette object to expose C, slots and qualities | Added explicit offline Notes/Chords/Held-quality reference states and URL persistence. Live state still supplies its own palette. |
| Recipe help | Current manual's custom recipes and inversion support | Removed earlier keymap-reference claim that all bank/inverted voicings remain ordinary notes. |
| Version ambiguity | Published/main and feature branches differed | Added firmware target and v0.1.11 shortcut differences to page and source manual. |

Existing host coverage also exercises TEMP/global loops, chord and scale
editing, variations, palette capture and printing, CC/automation, backup
protocols and MIDI timing. Those tests are evidence for the implementation,
not an exhaustive proof that every sentence in the manual is correct.

## Verification

- Assembled regression suite: passed, including current navigation, mute,
  palette/chord, storage, timing and UI checks.
- Tools tests: 8 passed.
- Chromium desktop/mobile: map controls, reference quality selection, reloadable
  URLs, written section/search links, empty searches, version guide and no
  horizontal page overflow passed. Existing Studio modal still renders.
- No firmware source changed in this audit. Prior firmware builds remain separate
  evidence; no new hardware flashing, macOS packaging or physical verification.

Publication should build from this integrated source revision, not cherry-pick
only the page shell again. Confirm the firmware version on the physical keyboard
before treating the development shortcuts as installed behavior.
