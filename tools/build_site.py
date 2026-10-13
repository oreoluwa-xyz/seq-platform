#!/usr/bin/env python3
"""Build a public static Studio artifact without firmware, server code, or device data."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def build(destination):
    destination.mkdir(parents=True, exist_ok=False)
    ui = destination / 'ui'
    ui.mkdir()
    for source in (ROOT / 'apps/studio').iterdir():
        if source.is_symlink():
            raise ValueError(f'Symlink cannot be published: {source.name}')
        if source.is_file() and source.suffix in ('.html', '.css', '.mjs') and 'fixture' not in source.name and 'test' not in source.name:
            shutil.copy2(source, ui / source.name)
    index = ui / 'index.html'
    index.write_text(index.read_text().replace('<head>', '<head><meta name="seq-hosting" content="static">', 1))
    shutil.copy2(ROOT / 'docs/manual.md', destination / 'MANUAL.md')
    # Publish only authoring documentation, schemas, and example module definitions.
    modules = ROOT / 'modules'
    for source in modules.rglob('*'):
        relative = source.relative_to(modules)
        allowed = (len(relative.parts) == 1 and source.suffix in ('.md', '.json')) or (len(relative.parts) == 2 and relative.parts[0] == 'examples' and source.suffix == '.json')
        if source.is_symlink():
            raise ValueError(f'Symlink cannot be published: {relative}')
        if source.is_file() and allowed:
            target = destination / 'modules' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    shutil.copy2(ROOT / 'LICENSE', destination / 'LICENSE')
    (destination / '.nojekyll').touch()
    (destination / 'index.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>SEQ Studio</title><meta http-equiv="refresh" content="0;url=ui/"><a href="ui/">Open SEQ Studio</a></html>\n')
    print(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/site')
    build(parser.parse_args().output)
