import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

spec = importlib.util.spec_from_file_location('seq_server', Path(__file__).with_name('seq_server.py'))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)


class FirmwareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.store = self.root / 'releases'
        self.image = self.root / 'image.bin'
        self.image.write_bytes(struct.pack('<II', 0x20000400, 0x08008001) + bytes(40000 - 8))
        self.release = s.register(self.store, self.image, 'Test release', 'Test notes')
        self.dfu = self.root / 'dfu-util'
        self.dfu.write_text('''#!/usr/bin/env python3
import sys,time
if '-l' in sys.argv:
 print('Found DFU: [0483:df11] ver=2200, alt=0, name="@Internal Flash /0x08000000/04*016Kg", serial="TEST123"')
else:
 assert sys.argv[1:]==['-a','0','-d','0483:df11','-S','TEST123','-s','0x08000000:leave','-D',sys.argv[-1]]
 time.sleep(.3)
 print('Download [=========================] 100% 40000 bytes')
 print('File downloaded successfully')
''')
        self.dfu.chmod(0o700)
        self.manager = s.FirmwareManager(self.store, str(self.dfu))

    def tearDown(self):
        if self.manager.worker:
            self.manager.worker.join()
        self.temp.cleanup()

    def test_bundled_releases_seed_writable_store_without_resetting_history(self):
        destination = self.root / 'user-data'
        s.seed_releases(self.store, destination)
        self.assertEqual(s.load_release(destination, self.release['id']), s.load_release(self.store, self.release['id']))
        history = destination / 'last-flash.json'
        history.write_text('{"serial":"KEEP"}')
        s.seed_releases(self.store, destination)
        self.assertEqual(history.read_text(), '{"serial":"KEEP"}')
        (self.store / f'{self.release["id"]}.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            s.seed_releases(self.store, self.root / 'rejected')

    def test_integrity_and_vectors(self):
        manifest, data = s.load_release(self.store, self.release['id'])
        self.assertEqual(manifest['target'], s.TARGET)
        with self.assertRaises(ValueError):
            s.load_release(self.store, '../image')
        altered = bytearray(data)
        altered[-1] = 1
        (self.store / f'{self.release["id"]}.bin').write_bytes(altered)
        with self.assertRaises(ValueError):
            s.load_release(self.store, self.release['id'])
        self.assertEqual(self.manager.releases(), [])
        for invalid in (bytes(40000), struct.pack('<II', 0x20000400, 0x08008000) + bytes(40000),
                        data + bytes(s.MAX_IMAGE)):
            with self.assertRaises(ValueError):
                s.validate_image(invalid)

    def test_embedded_identity_registration(self):
        def field(value, size):
            return value.encode('ascii').ljust(size, b'\0')
        identity = s.IDENTITY_MAGIC + field('2026.09.17.1', 24) + field(s.TARGET, 48) + field('a' * 32, 33)
        data = self.image.read_bytes() + identity
        self.image.write_bytes(data)
        manifest = s.register(self.store, self.image, 'Identity build', '')
        expected = dict(version='2026.09.17.1', target=s.TARGET, buildId='a' * 32)
        self.assertEqual(manifest['identity'], expected)
        self.assertEqual(s.load_release(self.store, manifest['id'])[0]['identity'], expected)
        self.assertIsNone(s.image_identity(bytes(40000)))
        for bad in (data[:-1], data + identity, data.replace(b'a' * 32, b'0' * 32),
                    data.replace(s.TARGET.encode(), b'wrong'.ljust(len(s.TARGET), b'x')),
                    data.replace(b'2026.09.17.1', b'bad version')):
            with self.assertRaises(ValueError):
                s.image_identity(bad)
        manifest['identity']['buildId'] = 'b' * 32
        s.atomic_json(self.store / (manifest['id'] + '.json'), manifest)
        with self.assertRaises(ValueError):
            s.load_release(self.store, manifest['id'])

    def test_only_internal_stm32_devices(self):
        line = 'Found DFU: [0483:df11] alt=0, name="@Internal Flash /0x08000000/04*016Kg", serial="ABC"'
        output = '\n'.join([line, line, line.replace('alt=0', 'alt=1'), line.replace('0483:df11', '1234:5678')])
        self.assertEqual(s.parse_devices(output), [{'serial': 'ABC', 'label': 'STM32 bootloader · ABC'}])

    def test_v1_max_bootloader_descriptor_spacing(self):
        line = 'Found DFU: [0483:df11] ver=2200, devnum=1, cfg=1, intf=0, path="2-1", alt=0, name="@Internal Flash  /0x08000000/04*016Kg,01*064Kg,01*128Kg", serial="34AA366E3233"'
        expected = [{'serial': '34AA366E3233', 'label': 'STM32 bootloader · 34AA366E3233'}]
        for spacing in ('', ' ', '  ', '\t'):
            with self.subTest(spacing=spacing):
                self.assertEqual(s.parse_devices(line.replace('Flash  /', 'Flash' + spacing + '/')), expected)
        for invalid in (line.replace('alt=0', 'alt=1'), line.replace('0483:df11', '1234:5678'),
                        line.replace('0x08000000', '0x1FFF7800'), line.replace('@Internal Flash', '@Option Bytes'),
                        line.replace('34AA366E3233', '')):
            self.assertEqual(s.parse_devices(invalid), [])
        with patch.object(s.subprocess, 'run', return_value=s.subprocess.CompletedProcess([], 0, line, '')):
            self.assertEqual(self.manager.status()['devices'], expected)

    def test_flash_completion_duplicate_and_disconnect(self):
        with self.assertRaises(ValueError):
            self.manager.start(self.release['id'], 'NOPE')
        with self.assertRaises(ValueError):
            self.manager.start(self.release['id'], 'TEST;123')
        self.manager.start(self.release['id'], 'TEST123')
        with self.assertRaises(ValueError):
            self.manager.start(self.release['id'], 'TEST123')
        # Changing registered bytes after start cannot change the worker's verified snapshot.
        (self.store / f'{self.release["id"]}.bin').write_bytes(b'changed')
        self.manager.worker.join(5)
        self.assertEqual(self.manager.job['state'], 'completed')
        self.assertEqual(self.manager.job['progress'], 100)
        self.assertEqual(json.loads((self.store / 'last-flash.json').read_text())['serial'], 'TEST123')

    def test_flash_failure_never_claims_success(self):
        self.dfu.write_text(self.dfu.read_text().replace("print('File downloaded successfully')", "sys.exit(1)"))
        self.manager.start(self.release['id'], 'TEST123')
        self.manager.worker.join(5)
        self.assertEqual(self.manager.job['state'], 'failed')
        self.assertFalse((self.store / 'last-flash.json').exists())

    def test_loopback_http_boundary(self):
        server = s.ThreadingHTTPServer(('127.0.0.1', 0), s.Handler)
        server.manager = self.manager
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        def request(path, body=None, headers=None):
            return urlopen(Request(base + path, data=None if body is None else json.dumps(body).encode(), headers=headers or {}), timeout=5)
        try:
            with request('/api/firmware/status') as response:
                status = json.load(response)
            self.assertEqual(status['devices'][0]['serial'], 'TEST123')
            good = {'Origin': base, 'Content-Type': 'application/json', 'X-SEQ-Token': status['token']}
            body = {'release': self.release['id'], 'serial': 'TEST123'}
            for headers in ({}, dict(good, Origin='http://evil.example'), dict(good, Host='evil.example'),
                            {k: v for k, v in good.items() if k != 'X-SEQ-Token'}):
                with self.assertRaises(HTTPError) as error:
                    request('/api/firmware/flash', body, headers)
                self.assertEqual(error.exception.code, 403)
            for path in ('/.git/config', '/.build/firmware/last-flash.json', '/%2e%2e/etc/passwd'):
                with self.assertRaises(HTTPError) as error:
                    request(path)
                self.assertEqual(error.exception.code, 404)
            with self.assertRaises(HTTPError):
                request('/api/firmware/flash', dict(body, command='rm'), good)
            with request('/api/firmware/releases/' + self.release['id'] + '/bin') as response:
                self.assertEqual(response.read(), self.image.read_bytes())
            with request('/api/firmware/flash', body, good) as response:
                self.assertEqual(response.status, 202)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()
