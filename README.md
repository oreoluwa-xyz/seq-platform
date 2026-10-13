# SEQ platform

A modular music composition platform. Today: a Keychron V1 Max sequencer, browser studio, and floating macOS desktop companion. Ableton and VCV Rack integrations are planned, not implemented.

## Source layout

- `apps/studio` — browser application, including UI and currently coupled musical/protocol helpers.
- `apps/desktop` — Electron shell and macOS packaging.
- `firmware/qmk/keymap` — current embedded sequencer implementation; still QMK-dependent.
- `firmware/targets/keychron-v1-max` — board configuration and target manifest.
- `firmware/qmk/dependency.json` and `patches` — exact upstream revision and required changes.
- `packages` — future shared protocol, music, UI, and module SDK boundaries. These are extraction plans, not existing libraries.
- `integrations` — future DAW and modular-host adapters.
- `modules` — module schema, examples, and component design.
- `tools/device` — local device service, firmware catalog, and backup utilities.
- `tests/firmware` — host regression tests.
- `docs` — architecture, migration, and user manual.

## Development

Requires Python 3.13, Node 22.12+, Git, and a C compiler. QMK compilation additionally requires the QMK CLI/toolchain. macOS packaging requires native ARM64 Python with PyInstaller 6.22.3, Xcode command-line tools, and native Node/Electron.

```sh
python3 tools/workspace.py prepare
python3 tools/workspace.py test
python3 tools/workspace.py studio
# Or the floating desktop app:
python3 tools/workspace.py desktop
```

`prepare` clones the locked QMK revision under ignored `.work/qmk`, applies the checked-in patch, and assembles sources into its keymap directory. Edit only canonical sources above, never `.work`. Subsequent commands restage sources automatically. Existing local QMK clones can accelerate preparation with `prepare --qmk-source /path/to/qmk`; the exact locked revision is still required and the original checkout is not modified.

This compatibility assembly preserves existing relative paths while we extract shared packages incrementally. It is not a requirement that future Ableton/Rack backends use QMK.

## Firmware and desktop releases

```sh
python3 tools/workspace.py firmware
python3 tools/workspace.py register --image dist/firmware.bin
# Use the SHA-256 returned by register:
python3 tools/workspace.py package --firmware-id <sha256>
```

The package command creates an explicitly unnotarized preview unless `--signed` is supplied. Signed builds require `SEQ_SIGN_IDENTITY` and `SEQ_NOTARY_PROFILE`. Set `SEQ_BUILD_PYTHON` to a native packaging environment. See [release instructions](apps/desktop/RELEASE.md).

GitHub Actions builds firmware and the macOS preview from the same checked-out commit, runs tests, verifies artifact hashes, and retains temporary build artifacts for three days. A manual run builds only. Pushing a `vMAJOR.MINOR.PATCH` tag matching the desktop package version builds and automatically publishes a GitHub prerelease after all jobs pass. Release assets remain available independently of temporary artifact retention. No workflow flashes devices. See [release instructions](apps/desktop/RELEASE.md).

Each desktop release bundles a pinned firmware image and records its identity and file hashes. The desktop version and firmware version are independent; protocol capabilities determine compatibility. See [architecture](docs/architecture.md).

## License

This initial migration preserves the QMK project's GNU GPL v2 license and upstream notices. Third-party runtime/source licenses are included during packaging. Future host integrations need their own SDK compatibility review before distribution.
