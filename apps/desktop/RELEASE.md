# macOS release

The first installer targets Apple Silicon and macOS 13+. It includes the UI, manual, Python service, a native dfu-util with libusb, and one validated firmware release. It does not require Node, Python, QMK Toolbox, or this checkout on the destination Mac.

## Build prerequisites

Use a Mac with Xcode command-line tools, Node 22.12+, and a native Python 3.13. Install desktop dependencies with `npm ci`. Create an isolated packaging environment (the Python architecture must match Electron):

```sh
python3.13 -m venv build/venv
build/venv/bin/python -m pip install pyinstaller==6.22.3
```

Or set `SEQ_BUILD_PYTHON` to an existing Python with PyInstaller installed. The preparation script downloads checksum-pinned dfu-util and libusb sources, builds them locally, and freezes the service. Build outputs and caches stay in ignored `build/` and `dist/` directories. Set `SEQ_FIRMWARE_ID` to the exact registered SHA-256 under `../.build/firmware`. Packaging refuses an unspecified firmware; inspect `build/stage/build-info.json` before distributing.

## Local preview

```sh
export SEQ_FIRMWARE_ID=5fb5f279c495f991d3cbd95d308f9f6ea615df706da0198d78890e0264eb475d
npm test
npm run dist:local
```

Produces `dist/SEQ-0.1.1-arm64-local-preview.dmg`. The app has an ad-hoc signature for local testing. **This is not a Developer ID signed or notarized distribution.** The filename and adjacent JSON explicitly record that distinction. Gatekeeper may block a downloaded copy.

## Signed and notarized release

1. Install your **Developer ID Application** certificate and its private key into the login Keychain. Confirm it appears in `security find-identity -v -p codesigning`.
2. Store notarization credentials interactively; do not put passwords or private keys in source control:

```sh
xcrun notarytool store-credentials SEQ-notary
```

3. Build with your exact certificate name and the Keychain profile name:

```sh
export SEQ_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export SEQ_NOTARY_PROFILE='SEQ-notary'
npm run dist:mac
```

The command refuses to continue without a valid identity and profile. It signs the app and all native helpers with Hardened Runtime, submits the app to Apple, checks acceptance, staples and assesses it, creates and signs the DMG, submits that DMG to Apple, then staples and verifies it. It writes a SHA-256 checksum and release metadata only after completion. No signing passwords are stored in the repository.

Apple reference: [Notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

## Validate the artifact

Mount the DMG and drag SEQ into Applications. Before releasing, run the packaged executable from outside the checkout, with an isolated profile and port:

```sh
cd /tmp
PATH=/usr/bin:/bin SEQ_DESKTOP_PORT=18766 /Applications/SEQ.app/Contents/MacOS/SEQ --smoke-test
```

This checks the bundled service, firmware catalog, DFU discovery, manual, window controls, renderer isolation, and Web MIDI/SysEx permission. It does not flash a keyboard or send notes. Test actual MIDI operation with a keyboard and your DAW separately.

Packaged SEQ uses `127.0.0.1:8766` and stores releases, flash history, window preferences, and service logs under `~/Library/Application Support/SEQ`. Browser drafts and development-wrapper drafts use different origins; export/import module JSON to migrate them. An occupied packaged-service port causes an explicit error rather than serving another checkout's UI.

Keep the exact firmware source checkout, including modifications and QMK submodules, available alongside public firmware distribution. DFU/libusb complete source archives and licenses are already bundled. Auto-updates and Intel/universal releases are not included in this slice.

## Platform repository workflow

Use the root `tools/workspace.py` commands documented in the repository README. The packager runs from an assembled QMK workspace so legacy relative paths remain valid. Edit `apps/desktop` and `apps/studio`, not the assembled copy.

`.github/workflows/preview.yml` is the active manual pipeline: Linux builds firmware from the same source commit; macOS verifies the transferred SHA-256 and commit, tests and packages the UI, then smoke-tests the standalone app. Outputs are retained for three days. It neither flashes devices nor publishes releases automatically. Triggering hosted CI can consume private-repository Actions quota.

Package versions, exact firmware hashes/identities, and a file inventory are recorded beside each DMG. Signed release automation remains pending Apple credentials and a protected GitHub environment. The local `--signed` path remains available and fails closed without credentials.
