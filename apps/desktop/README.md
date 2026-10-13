# SEQ desktop

A local Electron wrapper around the existing SEQ web UI. The browser UI and keyboard firmware remain usable independently.

## Run

In the platform repository use `python3 tools/workspace.py desktop` from the root. Commands below describe the assembled development workspace, not the canonical source folder.

For an installed app, open **SEQ.app** from Applications. See [macOS release](RELEASE.md) for building the self-contained DMG and setting up Developer ID signing/notarization. The commands below run from the source checkout.

Requires Node.js 22.12+ and Python 3. Firmware flashing still uses QMK Toolbox or `dfu-util` on PATH.

```sh
cd desktop
npm ci
npm start
```

On this Mac, dependencies are already installed. Double-click **Start SEQ.command** in the keymap directory to open it again. The launcher starts the installed Electron application directly; no terminal server command is needed. It is tied to this checkout, so keep the directory in place.

## Window controls

- **Pin on top** keeps the window above other apps. Default: on. macOS also enables visibility across Spaces and full-screen windows.
- **Performance view** hides the module workshop and track rack, retaining the live hardware panel, connection controls, manual and firmware manager. Open workshop restores the full editor.
- **Cmd/Ctrl + Shift + P** toggles pinning; **Cmd/Ctrl + Shift + M** toggles Performance view. These shortcuts operate only while SEQ has focus, so they do not take over DAW shortcuts.
- Position and size are saved separately for both views. If a monitor disappears, the window is moved back onto an available display.
- Live polling continues while another app has focus. Hardware updates never call `focus()` or bring the window to the front.
- On macOS, closing the window hides it and preserves its connection. Click the Dock icon to show it; **Cmd + Q** quits.

Connect the keyboard using its MIDI input and output as in the browser. Use only one connected SEQ client at a time: disconnect the browser UI before connecting the desktop UI, since simultaneous clients can interfere with paged hardware snapshots. Your DAW can continue using musical MIDI alongside SEQ.

Electron has its own local drafts and preferences. Browser-local drafts are not automatically migrated; export/import module JSON to move them. Saved keyboard projects are unchanged.

## Service ownership

The app reuses a compatible service already listening at `127.0.0.1:8765` and leaves it running on quit. Otherwise, it starts `tools/seq_server.py` automatically and sends SIGINT to that owned process when quitting. The server's existing graceful shutdown waits for an in-flight firmware worker to finish; no force-kill is used. A different service occupying the port produces an error instead of being terminated.

`SEQ_PYTHON=/absolute/path/to/python3` overrides the interpreter. `SEQ_DESKTOP_PORT=18765` selects another stable local port (changing ports also changes the UI storage origin). Service output is recorded under Electron's user-data directory as `service.log`; window preferences are in `window-state.json` there.

The renderer is sandboxed, has no Node access, and uses a narrow IPC bridge for window controls only. MIDI/SysEx permissions are restricted to the app's main document. External HTTPS links open in the default browser; untrusted navigation and embedded windows are blocked.

## Validation

```sh
npm test
SEQ_DESKTOP_PORT=18765 npm run test:smoke
```

The smoke run uses an isolated temporary profile, opens Electron, tests pin/unpin, switches views, checks IPC isolation and persisted state, and enumerates MIDI ports with SysEx access. It sends no musical MIDI and does not flash hardware. It saves a screenshot and reports the temporary profile path before exiting.

The macOS packaging workflow bundles Python, native DFU tooling, the manual, and a firmware release. `npm run dist:local` creates an explicitly unnotarized preview; `npm run dist:mac` requires Developer ID and notarization credentials and verifies both app and DMG. See [release instructions](RELEASE.md). Auto-updates and testing with the DAW in full screen remain separate work.
