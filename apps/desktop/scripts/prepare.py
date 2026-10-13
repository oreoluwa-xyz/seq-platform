"""Build native macOS resources. Run with the architecture's Python + PyInstaller."""
import hashlib
import importlib.util
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import sysconfig
import tarfile
import urllib.request

DESKTOP = Path(__file__).resolve().parents[1]
ROOT = DESKTOP.parent
BUILD = DESKTOP / 'build'
STAGE = BUILD / 'stage'
ARCH = {'arm64': 'arm64', 'x86_64': 'x64'}[platform.machine()]
SOURCES = [
    ('dfu-util-0.11.tar.gz', 'https://dfu-util.sourceforge.net/releases/dfu-util-0.11.tar.gz',
     'b4b53ba21a82ef7e3d4c47df2952adf5fa494f499b6b0b57c58c5d04ae8ff19e'),
    ('libusb-1.0.29.tar.bz2', 'https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2',
     '5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85')]


def run(*args, cwd=BUILD, env=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, env=env, check=True)


def main():
    if sys.platform != 'darwin':
        raise SystemExit('Build the macOS release on a Mac.')
    BUILD.mkdir(exist_ok=True)
    if STAGE.exists():
        shutil.rmtree(STAGE)
    site, runtime, licenses = [STAGE / name for name in ('site', 'runtime', 'licenses')]
    for folder in (site, runtime, licenses):
        folder.mkdir(parents=True)
    shutil.copytree(ROOT / 'ui', site / 'ui', ignore=shutil.ignore_patterns('*fixture*', '*test*'))
    shutil.copytree(ROOT / 'modules', site / 'modules')
    shutil.copy2(ROOT / 'MANUAL.md', site / 'MANUAL.md')

    spec = importlib.util.spec_from_file_location('server', ROOT / 'tools' / 'seq_server.py')
    server = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server)
    # Choose one validated release; never include local flash history or backups.
    candidates = [server.load_release(server.STORE, p.stem)[0] for p in server.STORE.glob('*.json') if server.ID.fullmatch(p.stem)]
    if not candidates:
        raise SystemExit('Register a firmware release before packaging.')
    release_id = os.environ.get('SEQ_FIRMWARE_ID')
    if not release_id:
        raise SystemExit('Set SEQ_FIRMWARE_ID to the exact registered firmware SHA-256 to bundle.')
    release, _ = server.load_release(server.STORE, release_id)
    (site / 'firmware').mkdir()
    for suffix in ('.json', '.bin'):
        shutil.copy2(server.STORE / (release['id'] + suffix), site / 'firmware')

    native = BUILD / ('native-' + ARCH)
    native.mkdir(exist_ok=True)
    (licenses / 'sources').mkdir()
    for filename, url, digest in SOURCES:
        archive = BUILD / filename
        if not archive.exists():
            urllib.request.urlretrieve(url, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Checksum mismatch: {archive}')
        shutil.copy2(archive, licenses / 'sources')
        folder = native / filename.split('.tar')[0]
        if folder.exists():
            shutil.rmtree(folder)
        with tarfile.open(archive) as tar:
            tar.extractall(native, filter='data')
    prefix = native / 'installed'
    env = {**os.environ, 'MACOSX_DEPLOYMENT_TARGET': '12.0', 'CC': '/usr/bin/clang',
           'CFLAGS': f'-O2 -arch {platform.machine()}', 'LDFLAGS': f'-arch {platform.machine()}'}
    usb, dfu = native / 'libusb-1.0.29', native / 'dfu-util-0.11'
    run('./configure', '--disable-shared', '--enable-static', f'--prefix={prefix}', cwd=usb, env=env)
    run('make', '-j4', cwd=usb, env=env)
    run('make', 'install', cwd=usb, env=env)
    dfu_env = {**env, 'USB_CFLAGS': f'-I{prefix}/include/libusb-1.0',
               'USB_LIBS': f'{prefix}/lib/libusb-1.0.a -framework IOKit -framework CoreFoundation -framework Security'}
    run('./configure', cwd=dfu, env=dfu_env)
    run('make', '-j4', cwd=dfu, env=dfu_env)
    (runtime / 'dfu').mkdir()
    shutil.copy2(dfu / 'src' / 'dfu-util', runtime / 'dfu' / 'dfu-util')
    for folder in (usb, dfu):
        shutil.copy2(folder / 'COPYING', licenses / (folder.name + '-COPYING'))
    shutil.copy2(__file__, licenses / 'sources' / 'prepare.py')

    run(sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir',
        '--name', 'seq-service', '--target-arch', platform.machine(),
        '--distpath', runtime, '--workpath', BUILD / 'pyinstaller', '--specpath', BUILD,
        ROOT / 'tools' / 'seq_server.py',
        env={**os.environ, 'PYINSTALLER_CONFIG_DIR': str(BUILD / 'pyinstaller-cache')})
    shutil.copy2(Path(sysconfig.get_path('stdlib')) / 'LICENSE.txt', licenses / 'Python-LICENSE.txt')
    pyinstaller = importlib.metadata.distribution('pyinstaller')
    for file in pyinstaller.files:
        if str(file).endswith('licenses/COPYING.txt'):
            shutil.copy2(pyinstaller.locate_file(file), licenses / 'PyInstaller-COPYING.txt')
    shutil.copy2(ROOT.parents[5] / 'LICENSE', licenses / 'QMK-LICENSE.txt')
    shutil.copy2(DESKTOP / 'THIRD-PARTY.md', licenses / 'README.md')
    source_hashes = {str(p.relative_to(site)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(site.rglob('*')) if p.is_file()}
    (STAGE / 'build-info.json').write_text(json.dumps({'appVersion': json.loads((DESKTOP / 'package.json').read_text())['version'], 'sourceHashes': source_hashes, 'arch': ARCH, 'python': platform.python_version(), 'firmware': release['identity'], 'firmwareSHA256': release['id']}, indent=2))
    print(f'Prepared {ARCH} resources at {STAGE}')


if __name__ == '__main__':
    main()
