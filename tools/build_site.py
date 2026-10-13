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
        if source.is_file() and source.suffix in ('.html', '.css', '.mjs') and 'fixture' not in source.name and 'test' not in source.name:
            shutil.copy2(source, ui / source.name)
    index = ui / 'index.html'
    index.write_text(index.read_text().replace('<head>', '<head><meta name="seq-hosting" content="static">', 1))
    shutil.copy2(ROOT / 'docs/manual.md', destination / 'MANUAL.md')
    shutil.copytree(ROOT / 'modules', destination / 'modules', ignore=shutil.ignore_patterns('__pycache__', '.DS_Store'))
    shutil.copy2(ROOT / 'LICENSE', destination / 'LICENSE')
    (destination / '.nojekyll').touch()
    (destination / 'index.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>SEQ Studio</title><meta http-equiv="refresh" content="0;url=ui/"><a href="ui/">Open SEQ Studio</a></html>\n')
    print(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/site')
    build(parser.parse_args().output)
