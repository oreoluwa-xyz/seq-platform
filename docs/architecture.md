# Platform boundaries

Applications present instrument state and editing workflows. Firmware and host integrations execute or translate those operations. The shared contract is versioned independently of applications and transports.

A device/session will identify its backend, protocol version, and supported capabilities. UI features must be gated by reported capabilities rather than assuming a keyboard model. The existing firmware target ID and SysEx protocol remain unchanged in this migration. The current device service remains V1 Max-specific; the directory layout alone does not make it a generic flasher.

## Extraction order

1. Preserve behavior and reproduce the existing firmware, UI, and macOS build from this repository.
2. Extract protocol codecs, capability schemas, and contract fixtures from `apps/studio` and matching firmware code into `packages/protocol`. Maintain old wire compatibility.
3. Separate board layout/device descriptors from musical state. Add a target registry and explicit supported image validators/flashers; never infer flash compatibility from generic STM32 USB identity.
4. Extract portable music functions with cross-language test vectors. Embedded C and JavaScript can be separate implementations of one documented behavior; no forced single-language runtime.
5. Define a module SDK with stable schemas, component capabilities, resource limits, and backend support. Module definitions are portable only to backends that implement their components.
6. Implement Ableton control-surface integration and VCV Rack plugin as native host adapters with their own lifecycles, versions, packaging, and tests.

## Versioning and releases

Keep app, firmware target, host integration, and protocol versions independent. Release manifests identify source commit, target, artifact SHA-256, embedded build ID, and compatible capabilities. A future product rename must not rename established wire identifiers.

Use Linux CI for firmware/tests and macOS CI for DMGs. Transfer immutable firmware artifacts between jobs. Preview jobs use no signing secrets; signed releases should use a protected environment, temporary Keychain, verified notarization, and draft publication before promotion.

## Migration implementation

The source folders are canonical. `tools/workspace.py` creates a disposable compatibility workspace inside pinned QMK and overlays source copies. This preserves existing relative paths in the server, desktop packaging, tests, and manual without requiring simultaneous rewrites. The browser's JS music/protocol helpers remain in Studio until extracted and tested. Reserved package folders document direction only.

QMK revision is pinned in `firmware/qmk/dependency.json`. The wireless report queue patch (256 to 64 entries) preserves the working checkout's RAM-saving customization. QMK submodules use upstream gitlink revisions; no upstream `.DS_Store` files or local build artifacts are included.

### Live mute summary

Palette snapshot command 17 advertises mute summaries with bit 7 of byte 958. When set, Studio reads the first page of command 21 even outside CC mode; CC pages containing mapping values are still fetched only in CC mode. Header byte 5 is summary version 1; little-endian 12-track masks at bytes 6–7 and 8–9 represent fully muted and partially muted tracks respectively. A wrapped melody/drum track is fully muted only when both layers are muted; any suppressed lane or layer otherwise marks it partial. These are live mute flags, not inferred from note activity or RGB. Existing header token and pagination remain unchanged. Older firmware has no capability bit, incurs no extra read, and displays mute state as unavailable.
