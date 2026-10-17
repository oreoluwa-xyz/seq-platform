# Compact window placement

## Trace and correction

`window-state.json` stores independent performance/workshop bounds in Electron
screen coordinates (DIPs). `normalizeState` retains layout version 3. `fitBounds`
selects the display and fits the saved rectangle before BrowserWindow creation.
Movement/resize events debounce saves by 300 ms; close and before-quit save
synchronously. Pin/mode publication also saves.

Previously, startup pin publication saved `getBounds()` before `show()`, and the
requested rectangle was never reapplied after showing. A native creation/show
adjustment could therefore replace the saved target. Startup now suppresses
geometry capture and move/resize saves until the fitted target has been reapplied
after showing. Subsequent user movement, close and quit still save actual bounds;
minimized, maximized and full-screen windows do not replace normal placement.

Display selection now uses greatest physical overlap, instead of the first work
area overlap. Compact windows retain valid coordinates over bottom and side Dock
reservations, while keeping their title bar below the menu bar and their rectangle
within a connected display. Workshop windows continue to fit within the work area.
Disconnected/off-screen and oversized rectangles are recovered; display geometry
changes trigger recovery without snapping already valid positions to an edge.
Mode changes use minimum dimensions appropriate to the destination rectangle.

These are application-side corrections, not evidence that the original observed
movement was caused by Electron or macOS. If macOS rejects the post-show position,
the smoke test reports requested versus actual bounds rather than claiming success.

## Automated evidence (2026-10-17)

- Source baseline: `d39eaa8a6d88ebb1b71bfa545df4f363e705f044`.
- `python3 tools/workspace.py prepare`: passed with the locked QMK revision.
- Assembled desktop `npm test`: 9 passed, including actual local helper lifecycle,
  bottom/side Dock cases, negative monitor coordinates, greatest-overlap selection,
  small displays, off-screen recovery and malformed sizes. A mocked native window
  exercises the real main-process creation/save path with creation/show clamping,
  disk-state roundtrip, mode changes, minimized saving and display shrink recovery.
- Root tools Python suite: 8 passed. Assembled device-service Python suite: 9 passed.
- Main-process JavaScript syntax and diff whitespace checks passed.
- Full `python3 tools/workspace.py test`: blocked at the host C suite because
  `clang` is absent. Installing it was blocked by container OS permissions.
- `npm ci` succeeded. Electron smoke execution was blocked before app startup by
  prohibited native socket operations (process singleton, SIGTRAP).
- Runtime here: Node 24.19.0, Python 3.12.14; not the handoff's Node 22/Python 3.13.

The extended Electron smoke asserts physical-bottom placement, saves to disk,
destroys/recreates BrowserWindow, reloads state and checks unchanged bounds after
settling. This is not a fresh-process relaunch. It remains unexecuted here.

## Required native verification before calling the observation resolved

1. Run the assembled suite and Electron smoke on the supported macOS environment.
2. With Ableton open, place the compact strip at the physical bottom; also try an
   arbitrary non-bottom position. Wait for the save, then Cmd+Q and launch again.
   Compare the performance rectangle in `~/Library/Application Support/SEQ/window-state.json`
   before quit and after launch, and visually check Ableton track activity/mutes.
   Repeat quit/relaunch at least three times to detect cumulative upward drift.
3. Repeat with Dock at bottom, left and right, with auto-hide on and off. Confirm
   manually selected positions survive; note any position macOS refuses to accept.
4. Repeat on an external monitor, including one arranged left/above the primary.
   Quit, disconnect that monitor and relaunch; verify the title bar is accessible.
   Also disconnect while running, change resolution/scaling, and minimize/restore.
5. Toggle workshop/performance, pin/unpin and hide/show. Confirm separate mode
   positions and unchanged compact layout. Verify live MIDI behavior with the
   existing rig; no firmware update or flashing is needed for this check.

No release, firmware modification or physical-device validation is part of this change.
