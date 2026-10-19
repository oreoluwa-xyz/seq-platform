import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import build_site
from sync_site import sync


class SiteTests(unittest.TestCase):
    def test_public_files_and_sync(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'source'
            (root / 'apps/studio').mkdir(parents=True)
            (root / 'apps/site').mkdir(parents=True)
            (root / 'docs').mkdir()
            (root / 'modules/examples').mkdir(parents=True)
            for name, content in {
                'apps/site/index.html': '<title>SEQ</title><a href="ui/">Studio</a>',
                'apps/site/site.css': 'body {}',
                'apps/site/site.mjs': 'export {};',
                'apps/site/private.txt': 'private',
                'apps/studio/index.html': '<head></head>',
                'apps/studio/app.mjs': 'export {};',
                'apps/studio/fixture.html': 'private fixture',
                'apps/studio/app-test.mjs': 'test',
                'apps/studio/server.py': 'server',
                'docs/manual.md': 'Manual',
                'modules/README.md': 'Modules',
                'modules/examples/example.json': '{}',
                'modules/private.bin': 'firmware',
                'LICENSE': 'License',
            }.items():
                (root / name).write_text(content)
            output, checkout = Path(tmp) / 'output', Path(tmp) / 'checkout'
            with patch.object(build_site, 'ROOT', root):
                build_site.build(output)
            self.assertEqual(set(p.name for p in (output / 'ui').iterdir()), {'index.html', 'app.mjs'})
            self.assertEqual((output / 'index.html').read_text(), (root / 'apps/site/index.html').read_text())
            self.assertEqual({p.name for p in (output / 'assets').iterdir()}, {'site.css', 'site.mjs'})
            self.assertFalse((output / 'private.txt').exists())
            self.assertIn('seq-hosting', (output / 'ui/index.html').read_text())
            self.assertFalse((output / 'modules/private.bin').exists())
            checkout.mkdir()
            (checkout / 'README.md').write_text('Keep me')
            (checkout / 'ui').mkdir()
            (checkout / 'ui/stale.mjs').touch()
            sync(output, checkout)
            self.assertFalse((checkout / 'ui/stale.mjs').exists())
            self.assertEqual((checkout / 'README.md').read_text(), 'Keep me')
            before = {str(p.relative_to(checkout)): p.read_bytes() for p in checkout.rglob('*') if p.is_file()}
            sync(output, checkout)
            self.assertEqual(before, {str(p.relative_to(checkout)): p.read_bytes() for p in checkout.rglob('*') if p.is_file()})
            (output / 'secret').touch()
            with self.assertRaises(ValueError):
                sync(output, checkout)
            (output / 'secret').unlink()
            (output / 'ui/leak').symlink_to(root / 'LICENSE')
            with self.assertRaises(ValueError):
                sync(output, checkout)

    def test_overlapping_paths_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                sync(tmp, tmp)
