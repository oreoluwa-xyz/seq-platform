#!/usr/bin/env python3
"""Replace only the generated paths in the public Studio checkout."""
import argparse
from pathlib import Path
import shutil

MANAGED = ('manual', 'assets', 'ui', 'modules', 'MANUAL.md', 'LICENSE', 'index.html', '.nojekyll')


def sync(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Build and checkout must be separate directories')
    if set(p.name for p in source.iterdir()) != set(MANAGED):
        raise ValueError('Unexpected or missing paths in static build')
    if any(p.is_symlink() for p in source.rglob('*')):
        raise ValueError('Static build must not contain symlinks')
    for name in MANAGED:
        target = destination / name
        if target.is_symlink() or target.is_file():
            target.unlink()
        elif target.exists():
            shutil.rmtree(target)
        origin = source / name
        if origin.is_dir():
            shutil.copytree(origin, target)
        else:
            shutil.copy2(origin, target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    sync(args.source, args.destination)
