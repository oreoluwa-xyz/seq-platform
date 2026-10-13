#!/usr/bin/env python3
"""Serve Module Studio and its loopback-only V1 Max firmware manager (stdlib only)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import struct
import subprocess
import tempfile
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, unquote

ROOT = Path(__file__).resolve().parents[1]
TARGET = 'keychron/v1_max/ansi_encoder:seq_test'
STORE = ROOT / '.build' / 'firmware'
MAX_IMAGE = 256 * 1024
ID = re.compile(r'^[a-f0-9]{64}$')
SERIAL = re.compile(r'^[A-Za-z0-9]{1,64}$')


def validate_image(data):
    if not 32768 < len(data) <= MAX_IMAGE:
        raise ValueError('Image must fit the V1 Max 256 KiB flash.')
    stack, reset = struct.unpack_from('<II', data)
    if not 0x20000000 < stack <= 0x20010000 or stack % 4:
        raise ValueError('Image has an invalid V1 Max stack vector.')
    if not reset & 1 or not 0x08000000 <= (reset & ~1) < 0x08000000 + len(data):
        raise ValueError('Image has an invalid flash reset vector.')
    return hashlib.sha256(data).hexdigest()


IDENTITY_MAGIC = b'SEQFWID1'
IDENTITY_SIZE = 113


def image_identity(data):
    start = data.find(IDENTITY_MAGIC)
    if start < 0:
        return None  # Legacy builds predate the identity command.
    if data.find(IDENTITY_MAGIC, start + 1) >= 0 or start + IDENTITY_SIZE > len(data):
        raise ValueError('Ambiguous or truncated firmware identity.')
    raw = data[start:start + IDENTITY_SIZE]
    fields = []
    for begin, end in ((8, 32), (32, 80), (80, 113)):
        value, separator, padding = raw[begin:end].partition(b'\0')
        if not separator or any(padding):
            raise ValueError('Malformed firmware identity field.')
        try:
            fields.append(value.decode('ascii'))
        except UnicodeDecodeError as error:
            raise ValueError('Malformed firmware identity text.') from error
    version, target, build_id = fields
    if target != TARGET or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+_-]{0,22}', version):
        raise ValueError('Firmware identity has an invalid version or board target.')
    if not re.fullmatch(r'[0-9a-f]{32}', build_id) or build_id == '0' * 32:
        raise ValueError('Firmware has no valid build ID. Rebuild with the seq_test rules.mk.')
    return dict(version=version, target=target, buildId=build_id)


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile('w', dir=path.parent, delete=False) as out:
        json.dump(value, out, indent=2)
        temp = out.name
    os.replace(temp, path)


def register(store, image, label, notes, flags=127, snapshot=4):
    if image.stat().st_size > MAX_IMAGE:
        raise ValueError('Image exceeds V1 Max flash capacity.')
    data = image.read_bytes()
    digest = validate_image(data)
    identity = image_identity(data)
    if not 1 <= len(label.strip()) <= 100 or len(notes) > 4000:
        raise ValueError('Use a release label of 1–100 characters and notes up to 4000 characters.')
    if not 0 <= flags <= 255 or snapshot not in (1, 2, 3, 4):
        raise ValueError('Invalid expected firmware capabilities.')
    store.mkdir(parents=True, exist_ok=True)
    # Only local maintainers register builds. Vector checks cannot identify a board or authenticate a publisher.
    (store / f'{digest}.bin').write_bytes(data)
    manifest = dict(id=digest, sha256=digest, target=TARGET, label=label.strip(), notes=notes,
                    size=len(data), registeredAt=time.time(), flags=flags, snapshot=snapshot, identity=identity)
    atomic_json(store / f'{digest}.json', manifest)
    return manifest


def load_release(store, release_id):
    if not ID.fullmatch(release_id):
        raise ValueError('Unknown release.')
    manifest = json.loads((store / f'{release_id}.json').read_text())
    image = store / f'{release_id}.bin'
    if image.stat().st_size > MAX_IMAGE:
        raise ValueError('Image exceeds V1 Max flash capacity.')
    data = image.read_bytes()
    if manifest['target'] != TARGET or manifest['id'] != release_id or manifest['sha256'] != release_id:
        raise ValueError('Release target or identity does not match.')
    if validate_image(data) != release_id or len(data) != manifest['size']:
        raise ValueError('Release checksum does not match. Register the original build again.')
    if manifest.get('identity') != image_identity(data):
        raise ValueError('Release metadata does not match embedded firmware identity.')
    return manifest, data


def parse_devices(output):
    found = {}
    for line in output.splitlines():
        if not re.search(r'Found DFU:\s*\[0483:df11\]', line, re.IGNORECASE):
            continue
        if not re.search(r'\balt\s*=\s*0\s*,', line):
            continue
        if not re.search(r'\bname="@Internal\s+Flash\s*/0x08000000/', line, re.IGNORECASE):
            continue
        match = re.search(r'serial="([A-Za-z0-9]{1,64})"', line)
        if match:
            serial = match.group(1)
            found[serial] = dict(serial=serial, label=f'STM32 bootloader · {serial}')
    return list(found.values())


def find_dfu():
    bundled = Path('/Applications/QMK Toolbox.app/Contents/Resources/dfu-util')
    return str(bundled) if bundled.is_file() else shutil.which('dfu-util')


class FirmwareManager:
    def __init__(self, store=STORE, dfu=None):
        self.store, self.dfu = store, dfu or find_dfu()
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.Lock()
        self.job = None
        self.worker = None

    def releases(self):
        items = []
        for path in self.store.glob('*.json'):
            if not ID.fullmatch(path.stem):
                continue
            try:
                item, _ = load_release(self.store, path.stem)
                items.append(item)
            except (ValueError, KeyError, OSError):
                continue
        return sorted(items, key=lambda item: item['registeredAt'], reverse=True)

    def devices(self):
        if not self.dfu:
            return [], 'Install QMK Toolbox (macOS) or dfu-util, then restart this service.'
        result = subprocess.run([self.dfu, '-l'], capture_output=True, text=True, timeout=8)
        devices = parse_devices(result.stdout + result.stderr)
        return devices, None if result.returncode == 0 else 'USB discovery failed. Check local USB permissions and QMK Toolbox.'

    def status(self):
        with self.lock:
            job = dict(self.job) if self.job else None
            devices, error = ([], None) if job and job['state'] == 'flashing' else self.devices()
        try:
            history = json.loads((self.store / 'last-flash.json').read_text())
        except (OSError, ValueError):
            history = None
        return dict(apiVersion=1, target=TARGET, token=self.token, releases=self.releases(), devices=devices,
                    toolAvailable=bool(self.dfu), discoveryError=error, job=job, lastFlash=history)

    def start(self, release_id, serial):
        if not isinstance(serial, str) or not SERIAL.fullmatch(serial):
            raise ValueError('Choose a detected bootloader serial.')
        with self.lock:
            if self.job and self.job['state'] == 'flashing':
                raise ValueError('A flash is already in progress. Wait for it to finish.')
            manifest, data = load_release(self.store, release_id)
            devices, error = self.devices()
            if error or serial not in {d['serial'] for d in devices}:
                raise ValueError(error or 'That bootloader is no longer connected. Detect it again.')
            self.job = dict(id=secrets.token_hex(12), release=manifest, serial=serial, state='flashing',
                            phase='Preparing', progress=0, startedAt=time.time(), log='')
            # Snapshot verified bytes so changing a registered file cannot change this flash.
            self.worker = threading.Thread(target=self._flash, args=(data,), daemon=False)
            self.worker.start()
            return dict(self.job)

    def _update(self, **fields):
        with self.lock:
            self.job.update(fields)

    def _flash(self, data):
        process = None
        try:
            with tempfile.TemporaryDirectory(prefix='seq-flash-') as temp:
                image = Path(temp) / 'firmware.bin'
                image.write_bytes(data)
                with self.lock:
                    serial = self.job['serial']
                with (Path(temp) / 'flash.log').open('w+') as log:
                    process = subprocess.Popen([self.dfu, '-a', '0', '-d', '0483:df11', '-S', serial,
                                                '-s', '0x08000000:leave', '-D', str(image)],
                                               stdout=log, stderr=subprocess.STDOUT)
                    deadline = time.monotonic() + 180
                    while True:
                        time.sleep(.2)
                        log.seek(0)
                        output = log.read()
                        clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', output).replace('\r', '\n')[-12000:]
                        matches = list(re.finditer(r'(Erase|Download)\s+\[[^\]]*\]\s+(\d+)%', output))
                        fields = {'log': clean}
                        if matches:
                            match = matches[-1]
                            fields.update(phase='Erasing' if match[1] == 'Erase' else 'Writing', progress=int(match[2]))
                        self._update(**fields)
                        if process.poll() is not None:
                            break
                        if time.monotonic() > deadline:
                            raise TimeoutError('Flash timed out. Re-enter bootloader mode and retry with the same release.')
                    if process.returncode != 0 or 'File downloaded successfully' not in output:
                        raise RuntimeError('Flashing was not confirmed. Keep the USB cable connected; inspect the log and retry in bootloader mode.')
            self._update(state='completed', phase='Flash completed · reconnect to verify', progress=100, finishedAt=time.time())
            with self.lock:
                history = {k: self.job[k] for k in ('id', 'release', 'serial', 'finishedAt')}
            try:
                atomic_json(self.store / 'last-flash.json', history)
            except OSError:
                self._update(historyWarning='Flashed successfully, but local history could not be saved.')
        except Exception as error:
            if process and process.poll() is None:
                process.kill()
                process.wait()
            self._update(state='failed', phase='Needs attention', error=str(error), finishedAt=time.time())


class Handler(SimpleHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "frame-ancestors 'none'")
        super().end_headers()

    def trusted(self):
        port = self.server.server_port
        hosts = {f'localhost:{port}', f'127.0.0.1:{port}'}
        host = self.headers.get('Host', '')
        origin = self.headers.get('Origin')
        if host not in hosts or (origin and origin != f'http://{host}'):
            self.send_error(403, 'Use the local Module Studio page.')
            return False
        return True

    def json_response(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if not self.trusted():
            return
        path = urlsplit(self.path).path
        manager = self.server.manager
        try:
            if path == '/api/firmware/status':
                self.json_response(manager.status())
            elif re.fullmatch(r'/api/firmware/releases/[a-f0-9]{64}/bin', path):
                manifest, data = load_release(manager.store, path.split('/')[4])
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Disposition', f'attachment; filename="seq-v1-max-{manifest["id"][:12]}.bin"')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            elif path.startswith('/api/'):
                self.send_error(404)
            else:
                self.serve_static()
        except (ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
            self.json_response({'error': str(error)}, 400)

    def serve_static(self):
        path = unquote(urlsplit(self.path).path)
        resolved = (ROOT / path.lstrip('/')).resolve()
        # Never expose build products, git metadata or arbitrary symlink targets.
        if not resolved.is_relative_to(ROOT) or any(part.startswith('.') for part in resolved.relative_to(ROOT).parts):
            self.send_error(404)
            return
        if path == '/':
            self.send_response(302)
            self.send_header('Location', '/ui/')
            self.end_headers()
            return
        if resolved.is_dir() and (not (resolved / 'index.html').is_file() or not (resolved / 'index.html').resolve().is_relative_to(ROOT)):
            self.send_error(404)
            return
        super().do_GET()

    def do_HEAD(self):
        if self.trusted():
            self.send_error(405)

    def do_POST(self):
        if not self.trusted():
            return
        manager = self.server.manager
        if (self.headers.get('Origin') != f'http://{self.headers.get("Host")}' or
                self.headers.get('Content-Type') != 'application/json' or
                not secrets.compare_digest(self.headers.get('X-SEQ-Token', ''), manager.token)):
            self.send_error(403, 'Open Firmware in the local UI and retry.')
            return
        try:
            if self.path != '/api/firmware/flash':
                self.send_error(404)
                return
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 4096:
                raise ValueError('Invalid request size.')
            body = json.loads(self.rfile.read(size))
            if not isinstance(body, dict) or set(body) != {'release', 'serial'} or not isinstance(body['release'], str):
                raise ValueError('Select a registered release and bootloader.')
            self.json_response(manager.start(body['release'], body['serial']), 202)
        except (ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
            self.json_response({'error': str(error)}, 400)


def main():
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    serve = sub.add_parser('serve')
    serve.add_argument('--port', type=int, default=8765)
    serve.add_argument('--root', type=Path, default=ROOT)
    serve.add_argument('--store', type=Path, default=STORE)
    serve.add_argument('--seed-store', type=Path)
    serve.add_argument('--dfu', type=Path)
    add = sub.add_parser('register', help='Register a locally built, trusted seq_test V1 Max image')
    add.add_argument('image', type=Path)
    add.add_argument('--target', required=True, choices=[TARGET])
    add.add_argument('--label', required=True)
    add.add_argument('--notes', default='')
    add.add_argument('--flags', type=int, default=127, help='Expected live chord capability bits')
    add.add_argument('--snapshot', type=int, choices=(1, 2, 3, 4), default=4)
    args = parser.parse_args()
    if args.command == 'register':
        print(json.dumps(register(STORE, args.image, args.label, args.notes, args.flags, args.snapshot), indent=2))
        return
    ROOT = args.root.resolve(strict=True)
    if args.seed_store:
        seed_releases(args.seed_store, args.store)
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.manager = FirmwareManager(args.store, str(args.dfu.resolve(strict=True)) if args.dfu else None)
    print(f'Module Studio + firmware manager: http://localhost:{server.server_port}/ui/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        # A flash runs independently of HTTP clients and must finish before process exit.
        server.server_close()
        if server.manager.worker:
            server.manager.worker.join()


def seed_releases(source, destination):
    """Copy validated bundled releases into writable storage without resetting history."""
    destination.mkdir(parents=True, exist_ok=True)
    for entry in source.glob('*.json'):
        if not ID.fullmatch(entry.stem):
            continue
        manifest, data = load_release(source, entry.stem)
        image = destination / f'{entry.stem}.bin'
        metadata = destination / entry.name
        if not image.exists():
            with tempfile.NamedTemporaryFile(dir=destination, delete=False) as out:
                out.write(data)
                temp = out.name
            os.replace(temp, image)
        if not metadata.exists():
            atomic_json(metadata, manifest)


if __name__ == '__main__':
    main()
