# Firmware management: first slice

The browser owns MIDI and project backup; a loopback Python service owns native USB DFU. Run `python3 tools/seq_server.py serve` from the keymap folder (Python 3.9 or newer). Identity-enabled firmware exposes a read-only MIDI identity command alongside the existing capability checks.

## Release boundary

Only a maintainer's local `register` command adds releases. No HTTP upload, shell command, source compilation or arbitrary file-path execution endpoint is exposed. Target is fixed to `keychron/v1_max/ansi_encoder:seq_test`, STM32 DFU 0483:df11, alternate 0, flash base 0x08000000, 256 KiB maximum image. Vector checks cannot establish board identity or publisher authenticity. The maintainer must build for the declared target.

Registration copies bytes and metadata to `.build/firmware/<sha256>.{bin,json}`. Release listing and flashing validate checksum/size/target. Flash start copies verified bytes into a private temporary directory; later registry edits cannot change the running job. Expected `--flags` (default 127) and `--snapshot` (default 4) describe the current chord-capability telemetry; set these explicitly when registering a compatible older release. Builds lacking command 17/22 cannot pass this slice's MIDI verifier.

## Service boundary

The service binds to 127.0.0.1. Every request validates Host against localhost/127.0.0.1 and the bound port. Mutation additionally requires matching Origin, JSON content type and a random service-session token. No CORS access is enabled. Hidden files/build directories and escaped symlink paths are not exposed by static serving. Downloads go through checksum validation.

One flash runs at a time. Serial must appear in fresh DFU discovery; multiple bootloaders require selection. STM32 serials are not matched to MIDI identities, so the UI identifies this limitation. A checked backup acknowledgement is a user workflow gate, not a server authentication mechanism. Flashing executes a fixed argv with no shell. Log/progress polling survives panel closure and page reload. The service must remain running. Timeout or nonzero return status produces failure; success additionally requires dfu-util's completed-download marker. Successful local history is separate from installed firmware identity.

## Checks

`PYTHONDONTWRITEBYTECODE=1 python3 tools/test_seq_server.py` covers malformed/changed images, target device parsing, detached serials, duplicate jobs, immutable flash input, process failure, release downloads, and HTTP origin/token/path boundaries with a simulated dfu-util. It needs loopback socket access but never accesses USB hardware.

`node ui/firmware-test.mjs` verifies expected capability bits, full snapshot pagination, older supported snapshots, short replies and disconnected MIDI. Open `ui/firmware-fixture.html` for the isolated browser workflow regression. It injects an in-memory service and simulated MIDI/backup callbacks; it cannot flash hardware. The main engine suite remains `sh tests/seq_engine/run.sh`.

## Next slices

- UI project restore with preflight compatibility, recovery copy and readback verification.
- Explicit user-triggered bootloader entry with project backup and transport handling.
- Build tooling that registers versioned artifacts and metadata automatically, then signed/distributed release catalogs.
- Additional board profiles and reliable MIDI-to-DFU identity matching before expanding supported devices.

## Installed firmware identity

`seq_firmware_identity.h` declares the release version and board target. `rules.mk` generates a fresh UUID (32 lowercase hex digits) per build and exports it to child makes. Update the release version when publishing; do not reuse an explicit `SEQ_BUILD_ID` for changed builds. Host engine tests use an all-zero placeholder that release registration rejects.

Command **24**, object 0, empty payload, returns immutable identity pages: offset 0 returns 64 bytes; offset 64 returns 49 bytes. Other arguments return status 3. It works without entering sequencer or backup mode and never modifies playback/project state. The 113-byte record is ASCII: magic `SEQFWID1` (8 bytes), version (24 bytes), board target (48 bytes), build ID (33 bytes). Text fields are NUL-terminated and zero-padded. The magic includes identity schema version 1.

The record lives in firmware read-only storage. Registration extracts it directly from the image, rejects invalid/duplicate records or a wrong board, and stores it in the release manifest. Listing/downloading/flashing rechecks metadata against the artifact. No caller-supplied identity is trusted. Artifacts without the marker remain legacy releases.

After a flash, the browser compares all three reported fields with the selected release. A missing command, malformed response or different identity fails exact verification. Legacy manifests retain capability-only verification with an explicit limitation. The artifact SHA-256 and reported build ID serve different purposes: the former validates stored/downloaded bytes, while the latter identifies the running build; this is not cryptographic flash readback or device attestation.
