# SEQ cloud development handoff

## Start here

Use `oreoluwa-xyz/seq-platform`, branch `main`. The feature baseline is v0.1.11
(commit `e4877a2e4e7acebdb9053e0d0f08dc6953a330f4`). Read `AGENTS.md`,
`README.md`, and `docs/architecture.md` before changing code.
`oreoluwa-xyz/seq-studio` is a generated public website mirror; develop here.
No local Mac checkout is required for cloud development.

## Prepare the cloud environment

Select this repository when creating the Codex Cloud environment. Ask the setup
agent to install and verify the following, then review its report and publish the
environment. These instructions are a setup recipe, not evidence that a cloud
instance has already been tested.

- Python 3.13 and Node 22.12+ (Node 22 is used in CI), npm, Git, make, and clang.
- On Ubuntu, `gcc-arm-none-eabi`, `binutils-arm-none-eabi`,
  `libnewlib-arm-none-eabi`, and `dfu-util` for QMK compilation.
- Prepare the locked backend before installing its Python dependencies:

```sh
python3 tools/workspace.py prepare
python3 -m venv .work/cloud-venv
. .work/cloud-venv/bin/activate
python -m pip install qmk==1.1.8 -r .work/qmk/requirements.txt
python tools/workspace.py test
python -m unittest discover -s tools -p 'test_*.py'
python tools/workspace.py firmware
```

Ensure subsequent tasks activate `.work/cloud-venv/bin/activate` before QMK
commands. Installation requires network access to GitHub (locked QMK source and
submodules), Python/npm registries, and the configured OS package repositories.
The desktop dependency installation may also download Electron from GitHub.
No device backups, signing secrets, or production credentials are needed for
ordinary feature work. Keep dependencies and build products under ignored paths.
Use `.github/workflows/preview.yml` as the authoritative build reference.

For a browser preview, run `python tools/workspace.py studio`; use the service's
reported address and the cloud environment's preview mechanism. The cloud server
has no connection to the user's USB keyboard. Do not mistake a disconnected
preview for a hardware regression. macOS packaging and standalone app smoke tests
remain in the existing GitHub Actions macOS job.

## Current product behavior

- Euclidean melodic rhythms cycle a saved palette of notes or chords across hits.
  Each palette slot supports its own chord quality, with fixed qualities and
  scale-aware Auto triads/sevenths.
- The compact desktop performance strip keeps rhythm visible beside palette
  editing. Selecting a palette entry highlights its source hits and count.
  Recorded overrides are excluded; highlighting describes assignments before
  variation, not a guarantee of the final varied playback pitch.
- Palette capture/editing opens Palette automatically. Live polling must preserve
  explicit tab choices, focus, and in-progress clicks.
- Reversible melodic variations preserve original material. They support ordering,
  random pitch, rotation, mutation, chord voicing, reroll/lock, and immediate or
  next-bar application to one or all melodic tracks.
- Accumulator offsets act on chords as a group. Temporary global loop-length
  changes, project capacity, save status, and TEMP readiness are implemented.
- Stored-rhythm restore and TEMP stack-pressure fixes have regression coverage.

See `docs/releases/v0.1.11.md`, `docs/manual.md`, and
`docs/keymap-reference.md` for the released behavior and controls.

## User priorities and unresolved observations

The companion is used over Ableton. Keep it short and wide so Ableton's mute
buttons and track activity remain visible. “Mute button” and “active tracks” in
this design discussion referred to Ableton, not extra controls in SEQ.
Keep rhythm and palette visible together; avoid requiring tab switching to relate
notes/chords to Euclidean hits. Preserve stable controls during live polling.

The local desktop window was observed being clamped upward on relaunch despite
being placed at the bottom of the screen. Exact bottom-edge placement persistence
has not been established. Any fix needs a real macOS/Ableton check; do not claim
that a cloud test verifies it. No specific next feature has been selected: ask
for the next feature rather than inventing a redesign.

## Editing and verification

Edit canonical sources only: `apps/studio`, `apps/desktop`, `firmware/qmk/keymap`,
`firmware/targets`, `tools/device`, and `tests/firmware`. Never edit assembled
`.work/qmk` sources; workspace commands regenerate them. Keep protocol identity
and capabilities stable. `packages` and `integrations` contain plans, not working
shared libraries or DAW integrations.

Run the assembled suite with `python tools/workspace.py test`. Run a target
firmware compile for firmware changes. Add focused regressions for complex state
changes. Host tests, target compilation, packaged smoke tests, actual flashing,
USB reconnection, and musical hardware behavior are separate evidence stages.
Report which actually ran. Do not flash or claim hardware validation from cloud.

The v0.1.11 release passed local regression tests, CI target compilation, macOS
packaging and standalone smoke, and public web hash verification. Those results
apply to that release, not automatically to future edits. No new flash was part
of that release operation.

## Branches, builds, and publication

Use a `codex/` feature branch and a reviewable PR for new work. Do not publish a
release merely because a feature task completes.

- `preview.yml` can be manually dispatched to build firmware and macOS artifacts.
  It currently has no pull-request trigger. Temporary artifacts last three days.
- A `vMAJOR.MINOR.PATCH` tag matching desktop package and lockfile versions runs
  the build and publishes a prerelease after successful checks. Release assets
  persist. Current macOS distribution is Apple Silicon, ad-hoc signed and
  unnotarized; never describe it as notarized.
- `publish-studio.yml` publishes eligible main-branch UI/docs/module changes to
  the generated `seq-studio` repository using its existing deploy key. GitHub
  Pages serves https://oreoluwa-xyz.github.io/seq-studio/ui/ .
- A separate versioned web release was also published for v0.1.11; do not assume
  the Pages workflow creates web release archives automatically.
- Read `apps/desktop/RELEASE.md` before release work. Keep secrets in existing CI
  configuration. No workflow flashes the user's keyboard.

## First feature-task prompt

> Continue SEQ development in this repository. Read AGENTS.md and
> docs/cloud-handoff.md. Verify the checked-out revision and environment, activate
> the prepared Python environment, and work on a codex/ branch. Preserve the
> compact Ableton-friendly layout and stable Euclid/palette interactions. Implement
> the feature I specify, run relevant regressions and the required build, and open
> a PR with exact validation results. Keep physical-device checks explicit and
> do not release or deploy unless requested.
