# Contributing to SEQ

A manual correction, a small Studio fix, or a regression test is a good first contribution. You can work on docs, the web UI, and automated checks without a Keychron or MIDI hardware.

## Start with one change

1. Search the [open issues](https://github.com/oreoluwa-xyz/seq-platform/issues) and [pull requests](https://github.com/oreoluwa-xyz/seq-platform/pulls) for related work. Open an issue first if you want to add a feature, change the wire protocol, or support another board.
2. Fork `oreoluwa-xyz/seq-platform`, clone your fork, and create a branch from `main` with `git switch -c describe-your-change`. Read [AGENTS.md](AGENTS.md) and, for changes across components, the [architecture notes](docs/architecture.md).
3. Keep the patch focused. For a bug, record what you expected and what happened, and add a focused regression when practical. Run the checks below that cover your change.

## Find the source and choose an environment

Run the commands in this guide from the repository root. Use Python 3.13 for the workspace and Python checks and Node 22.12+ for JavaScript. Linux or a cloud checkout works for the web, host tests, and target compilation. Use macOS for native desktop behavior, and Apple Silicon for validating the packaged app. See the [cloud setup](docs/cloud-handoff.md) for build dependencies.

| Area | Edit here | Start with |
| --- | --- | --- |
| Docs, landing page, module examples | [docs](docs/), [apps/site](apps/site/), [modules](modules/) | Read the rendered change and check links. Run the static site tests when editing published content or its build. |
| Browser Studio | [apps/studio](apps/studio/) | Run the Node checks and preview the affected UI in a browser. |
| Desktop companion | [apps/desktop](apps/desktop/) | Run the assembled desktop tests. Use native macOS/Electron for window, lifecycle, or permission changes. |
| Device service and backup tooling | [tools/device](tools/device/) | Run the Python service tests, which simulate DFU. Run assembled regressions for changes to shared behavior. |
| Firmware | [QMK keymap](firmware/qmk/keymap/), [V1 Max target](firmware/targets/keychron-v1-max/), [host tests](tests/firmware/) | Run assembled regressions and compile the pinned target. Report physical-device checks separately. |

Edit source in this repository. The workspace stages disposable copies in `.work/qmk`; leave those copies and other generated outputs such as `dist/` and `node_modules/` out of the PR. The [Studio publication workflow](.github/workflows/publish-studio.yml) builds the public landing page, Studio, manual, and module examples from eligible changes on `main` and syncs them to the generated `oreoluwa-xyz/seq-studio` mirror. Send changes upstream to `seq-platform`. Ableton and VCV Rack adapters and the shared packages are still planned.

## Run checks for your change

### Lightweight checks

These commands work directly from the checkout without preparing QMK. Pick the ones that cover your change.

```sh
# Static site build and publication tests
python3 -m unittest discover -s tools -p 'test_site.py'

# Studio logic and mocked firmware responses
node apps/studio/test.mjs
node apps/studio/firmware-test.mjs

# Device service tests, including simulated DFU
python3 -m unittest discover -s tools/device -p 'test_*.py'
```

For a browser preview without QMK, [build and serve the static site locally](apps/site/README.md#seq-landing-page). Use the disconnected Studio for layout, navigation, and local editor changes. Live MIDI, on-device behavior, and local service features need separate checks.

### Assembled checks and local apps

```sh
python3 tools/workspace.py prepare
python3 tools/workspace.py test
```

`prepare` clones the revision in [the QMK lock file](firmware/qmk/dependency.json), applies the checked-in patch, and stages source. If you already have a clone containing that revision, `prepare --qmk-source /path/to/qmk` can use it as the clone source. The full suite requires `clang`, Python, Node, npm, and access to install the desktop dependencies. It runs host C regressions, Studio-to-C protocol checks, staged Python device-service tests, and desktop tests. A QMK compiler toolchain and MIDI hardware are unnecessary for this suite.

After `prepare`, use the following for a narrower desktop check. The tests start the real Python helper from the staged layout and run in Node without opening Electron.

```sh
python3 tools/workspace.py stage
desktop=.work/qmk/keyboards/keychron/v1_max/ansi_encoder/keymaps/seq_test/desktop
npm --prefix "$desktop" test
```

For manual checks after `prepare`, `python3 tools/workspace.py studio` starts the local service and prints the Studio URL. On a Mac, `python3 tools/workspace.py desktop` installs the staged desktop dependencies and starts Electron. For native UI changes, exercise the affected behavior on macOS and record the OS version and scenario, such as relaunching a moved window or granting MIDI permission. The [desktop release guide](apps/desktop/RELEASE.md#validate-the-artifact) covers the separate packaged-app smoke test.

### Firmware compilation and hardware

After `prepare` and the [QMK toolchain setup](docs/cloud-handoff.md#prepare-the-cloud-environment), run the target build for firmware changes.

```sh
python3 tools/workspace.py firmware
```

This initializes the required pinned submodules, compiles the Keychron V1 Max ANSI encoder `seq_test` target, and writes `dist/firmware.bin`. Target compilation, local image registration, installer packaging, signing/notarization, flashing, and hardware validation are separate stages. Use the [target manifest](firmware/targets/keychron-v1-max/target.json) to identify the board. Generic STM32 DFU identity alone cannot establish compatibility. If hardware is unavailable, report the build and leave USB reconnect, MIDI behavior, and other device checks pending for a maintainer.

## Submit your pull request

Push your branch to your fork and open a PR against upstream `main`. Explain what changed and why, link any issue, and give the exact commands and outcomes. Include a redacted screenshot for visible changes when helpful. Call out which build, native macOS, or hardware checks still need coverage. Ordinary PRs can stop at the applicable checks above. Leave release tags, signing, and publication to the release workflow.

SEQ uses the [GNU GPL v2 license](LICENSE). Keep copyright and license notices, and include only code, assets, and fixtures you have the right to contribute under compatible terms. If you distribute a modified firmware binary, follow the GPL v2 corresponding-source requirements and retain applicable third-party notices. See the [release guide](apps/desktop/RELEASE.md#validate-the-artifact) for source requirements specific to SEQ builds.

Use synthetic fixtures or a small redacted log excerpt to reproduce device bugs. Keep device backups, flash history, bootloader serial numbers, private MIDI port names, credentials, and signing keys out of commits, issues, and PR attachments. Review screenshots and logs for these details before posting. The keyboard model and variant, firmware version or build ID, app version, OS, and browser version are useful for diagnosing compatibility.
