# Repository guidance

- This is the platform repository. Edit canonical sources, never generated `.work/`, `dist/`, `node_modules/`, or build outputs.
- Preserve current firmware behavior during structural migrations. QMK and the C engine are not yet portable shared libraries.
- Use `python3 tools/workspace.py prepare` and `test` for the assembled regression suite. Run the target firmware build for firmware changes; use packaged smoke tests for desktop releases.
- Keep target-specific logic in `firmware/targets` where practical. Shared QMK changes must be checked-in patches against the locked upstream revision.
- Retain QMK C style: four spaces, snake_case, static local helpers, existing feature flags.
- Distinguish tests, firmware compilation, packaging, signing, registration, flashing, and hardware validation in reports.
- Never commit device backups, flash history, signing keys, credentials, or build artifacts. Publish installers as release assets.
- Keep protocol identity/capabilities stable across product/repository renames.
- Planned integrations and reserved packages are not supported products. Do not create fake implementations merely to fill directories.
