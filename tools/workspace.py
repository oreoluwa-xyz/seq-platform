#!/usr/bin/env python3
"""Assemble the current QMK backend from canonical platform sources."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
QMK = ROOT / '.work/qmk'
LOCK = json.loads((ROOT / 'firmware/qmk/dependency.json').read_text())
TARGET = json.loads((ROOT / 'firmware/targets/keychron-v1-max/target.json').read_text())
KEYMAP = QMK / 'keyboards' / TARGET['keyboard'] / 'keymaps' / TARGET['keymap']
IGNORES = shutil.ignore_patterns('node_modules', 'build', 'dist', '__pycache__', '.DS_Store')


def run(*args, cwd=ROOT, env=None):
    subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True)


def output(*args, cwd=ROOT):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def stage():
    if not (QMK / '.git').exists():
        raise SystemExit('Run python3 tools/workspace.py prepare first.')
    if output('git', 'rev-parse', 'HEAD', cwd=QMK) != LOCK['revision']:
        raise SystemExit('QMK revision differs from dependency.json; refusing to stage.')
    KEYMAP.mkdir(parents=True, exist_ok=True)
    # Preserve build caches while removing stale managed source files.
    marker = ROOT / '.work/staged-files.json'
    if marker.exists():
        for name in json.loads(marker.read_text()):
            p = KEYMAP / name
            if not p.resolve().is_relative_to(KEYMAP.resolve()):
                raise SystemExit('Invalid staged path')
            if p.is_file():
                p.unlink()
    copied = []
    def copy(source, destination):
        if source.is_dir():
            for p in source.rglob('*'):
                if p.is_file() and not any(x in ('node_modules','build','dist','__pycache__','.DS_Store') for x in p.relative_to(source).parts):
                    copy(p, destination / p.relative_to(source))
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            copied.append(str(destination.relative_to(KEYMAP)))
    copy(ROOT / 'firmware/qmk/keymap', KEYMAP)
    for name in ('config.h', 'keymap.c', 'rules.mk'):
        copy(ROOT / 'firmware/targets/keychron-v1-max' / name, KEYMAP / name)
    for source, dest in [('apps/studio','ui'),('apps/desktop','desktop'),('modules','modules'),
                         ('tools/device','tools'),('tests/firmware','tests'),
                         ('docs/manual.md','MANUAL.md'),('docs/keymap-reference.md','README.MD')]:
        copy(ROOT / source, KEYMAP / dest)
    marker.write_text(json.dumps(copied, indent=2)+'\n')
    print(f'Staged {len(copied)} source files at {KEYMAP}', flush=True)


def prepare(source=None):
    QMK.parent.mkdir(parents=True, exist_ok=True)
    if not QMK.exists():
        run('git', 'clone', '--no-checkout', source or LOCK['repository'], QMK)
        run('git', 'checkout', '--detach', LOCK['revision'], cwd=QMK)
    if output('git', 'rev-parse', 'HEAD', cwd=QMK) != LOCK['revision']:
        raise SystemExit('Existing QMK checkout is not the locked revision.')
    for patch in LOCK['patches']:
        applied = subprocess.run(['git','apply','--reverse','--check',str(ROOT / patch)], cwd=QMK, capture_output=True).returncode == 0
        if not applied:
            run('git','apply','--check',ROOT / patch,cwd=QMK)
            run('git','apply',ROOT / patch,cwd=QMK)
    stage()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare','stage','test','firmware','register','studio','desktop','package'])
    parser.add_argument('--qmk-source', help='Optional existing local QMK clone; locked commit is still required')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--firmware-id')
    parser.add_argument('--signed', action='store_true')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.qmk_source)
        return
    stage()
    if args.command == 'stage':
        return
    if args.command == 'test':
        run('sh',KEYMAP / 'tests/seq_engine/run.sh')
        run(sys.executable,'-m','unittest','discover','-s','tools','-p','test_*.py',cwd=KEYMAP)
        run('npm','ci',cwd=KEYMAP/'desktop')
        run('npm','test',cwd=KEYMAP/'desktop')
    elif args.command == 'firmware':
        run('git','submodule','update','--init',*LOCK['submodules'],cwd=QMK)
        run('qmk','compile','-j','4','-kb',TARGET['keyboard'],'-km',TARGET['keymap'],cwd=QMK)
        image = QMK / (TARGET['keyboard'].replace('/','_')+'_'+TARGET['keymap']+'.bin')
        (ROOT/'dist').mkdir(exist_ok=True)
        shutil.copy2(image,ROOT/'dist/firmware.bin')
    elif args.command == 'register':
        if not args.image:
            parser.error('register requires --image')
        run(sys.executable,KEYMAP/'tools/seq_server.py','register',args.image.resolve(),
            '--target',TARGET['protocolTarget'],'--label','SEQ platform firmware')
    elif args.command == 'studio':
        run(sys.executable,KEYMAP/'tools/seq_server.py','serve')
    elif args.command in ('desktop','package'):
        run('npm','ci',cwd=KEYMAP/'desktop')
        if args.command == 'desktop':
            run('npm','start',cwd=KEYMAP/'desktop')
        else:
            if not args.firmware_id:
                parser.error('package requires --firmware-id')
            env = {**os.environ,'SEQ_FIRMWARE_ID':args.firmware_id}
            run('npm','run','dist:mac' if args.signed else 'dist:local',cwd=KEYMAP/'desktop',env=env)
            (ROOT/'dist').mkdir(exist_ok=True)
            for p in (KEYMAP/'desktop/dist').glob('*.dmg*'):
                shutil.copy2(p,ROOT/'dist'/p.name)


if __name__ == '__main__':
    main()
