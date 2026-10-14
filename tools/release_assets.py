#!/usr/bin/env python3
"""Validate a tagged build and prepare immutable GitHub release assets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/device'))
from seq_server import image_identity, validate_image


def prepare(tag, commit, artifacts, destination):
    version = json.loads((ROOT / 'apps/desktop/package.json').read_text())['version']
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', tag) or tag != 'v' + version:
        raise ValueError('Release tag must match apps/desktop/package.json version.')
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or (artifacts / 'source-commit.txt').read_text().strip() != commit:
        raise ValueError('Artifacts do not match the tagged source commit.')
    dmg_name = f'SEQ-{version}-arm64-local-preview.dmg'
    dmg = artifacts / dmg_name
    metadata = json.loads((artifacts / (dmg_name + '.json')).read_text())
    firmware = (artifacts / 'firmware.bin').read_bytes()
    firmware_hash = validate_image(firmware)
    identity = image_identity(firmware)
    if not identity or identity != metadata['firmware'] or firmware_hash != metadata['firmwareSHA256']:
        raise ValueError('Firmware identity or checksum differs from bundled firmware.')
    if metadata.get('appVersion') != version or metadata.get('arch') != 'arm64':
        raise ValueError('Unexpected application version or architecture.')
    if metadata.get('notarized') is not False or metadata.get('signing') != 'ad-hoc preview only':
        raise ValueError('This pipeline publishes explicitly unnotarized previews only.')
    if hashlib.sha256(dmg.read_bytes()).hexdigest() != metadata['sha256']:
        raise ValueError('DMG checksum mismatch.')
    destination.mkdir(parents=True, exist_ok=False)
    firmware_name = f"SEQ-keychron-v1-max-{identity['version']}.bin"
    shutil.copy2(dmg, destination / dmg_name)
    shutil.copy2(artifacts / (dmg_name + '.json'), destination / (dmg_name + '.json'))
    (destination / firmware_name).write_bytes(firmware)
    manifest = dict(tag=tag, sourceCommit=commit, appVersion=version, firmware=identity,
                    firmwareSHA256=firmware_hash, dmgSHA256=metadata['sha256'], notarized=False)
    (destination / 'release.json').write_text(json.dumps(manifest, indent=2) + '\n')
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(destination.iterdir()))
    (destination / 'SHA256SUMS').write_text(sums)
    changes_path = ROOT / "docs/releases" / (tag + ".md")
    changes = changes_path.read_text() if changes_path.exists() else ""
    return f'''SEQ {version} preview

{changes}

- Apple Silicon macOS app, with the Studio UI and manual.
- Keychron V1 Max ANSI encoder firmware {identity['version']}.
- Firmware build ID: {identity['buildId']}.
- Source commit: {commit}.

The DMG has an ad-hoc signature and is **not Developer ID signed or notarized**. This is a prerelease. Firmware is only for the Keychron V1 Max ANSI encoder target; installation is a separate action.

Firmware regression tests, desktop tests, and the packaged-app smoke test passed before publication. CI does not verify physical keyboard behavior. The DMG bundles exactly the firmware binary provided here. Verify downloads using SHA256SUMS; release.json records source and firmware identity.
'''


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tag', required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--artifacts', required=True, type=Path)
    p.add_argument('--destination', required=True, type=Path)
    p.add_argument('--notes', required=True, type=Path)
    args = p.parse_args()
    args.notes.write_text(prepare(args.tag, args.commit, args.artifacts, args.destination))
