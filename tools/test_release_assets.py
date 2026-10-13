import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import release_assets as r


class ReleaseAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'apps/desktop').mkdir(parents=True)
        (self.root / 'apps/desktop/package.json').write_text('{"version":"0.1.1"}')
        self.root_patch = patch.object(r, 'ROOT', self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.artifacts = self.root / 'artifacts'
        self.artifacts.mkdir()
        self.dmg = 'SEQ-0.1.1-arm64-local-preview.dmg'
        data = bytearray(struct.pack('<II', 0x20000400, 0x08008001) + bytes(40000 - 8))
        identity = b'SEQFWID1' + b'2026.10.12.1'.ljust(24, b'\0') + b'keychron/v1_max/ansi_encoder:seq_test'.ljust(48, b'\0') + (b'a' * 32).ljust(33, b'\0')
        data[100:100+len(identity)] = identity
        (self.artifacts / 'firmware.bin').write_bytes(data)
        (self.artifacts / self.dmg).write_bytes(b'test DMG')
        (self.artifacts / 'source-commit.txt').write_text('b' * 40)
        self.meta = dict(appVersion='0.1.1', arch='arm64', notarized=False, signing='ad-hoc preview only',
                         firmware=r.image_identity(data), firmwareSHA256=hashlib.sha256(data).hexdigest(),
                         sha256=hashlib.sha256(b'test DMG').hexdigest())
        self.save_meta()

    def save_meta(self):
        (self.artifacts / (self.dmg + '.json')).write_text(json.dumps(self.meta))

    def prepare(self, tag='v0.1.1', commit='b' * 40):
        return r.prepare(tag, commit, self.artifacts, self.root / 'release')

    def test_verified_assets_have_checksums_and_provenance(self):
        self.assertIn('not Developer ID signed', self.prepare())
        destination = self.root / 'release'
        manifest = json.loads((destination / 'release.json').read_text())
        self.assertEqual(manifest['sourceCommit'], 'b' * 40)
        for line in (destination / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ')
            self.assertEqual(hashlib.sha256((destination / name).read_bytes()).hexdigest(), digest)

    def test_wrong_version_rejected(self):
        with self.assertRaisesRegex(ValueError, 'tag must match'):
            self.prepare(tag='v0.2.0')

    def test_wrong_commit_rejected(self):
        with self.assertRaisesRegex(ValueError, 'source commit'):
            self.prepare(commit='c' * 40)

    def test_tampered_dmg_rejected(self):
        (self.artifacts / self.dmg).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'DMG checksum'):
            self.prepare()

    def test_wrong_bundled_firmware_rejected(self):
        self.meta['firmwareSHA256'] = '0' * 64
        self.save_meta()
        with self.assertRaisesRegex(ValueError, 'Firmware identity or checksum'):
            self.prepare()

    def test_false_signing_claim_rejected(self):
        self.meta['notarized'] = True
        self.save_meta()
        with self.assertRaisesRegex(ValueError, 'previews only'):
            self.prepare()


if __name__ == '__main__':
    unittest.main()
