import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('seq_backup', Path(__file__).parents[2] / 'tools/seq_backup.py')
backup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backup)


class BackupTests(unittest.TestCase):
    def test_midi_payload_roundtrip(self):
        data = bytes(range(256))
        encoded = backup.encode_payload(data)
        self.assertTrue(all(byte < 16 for byte in encoded))
        self.assertEqual(backup.decode_payload(encoded), data)
        for invalid in ([16, 0], [0]):
            with self.assertRaises(ValueError):
                backup.decode_payload(invalid)

    def test_checksum(self):
        self.assertEqual(backup.checksum(b'hello').hex(), 'ab2c9f4f')

    def test_file_roundtrip_and_corruption(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'project.json'
            objects = [bytes(range(256)) * 20] * 13
            backup.save_file(path, objects)
            self.assertEqual(backup.load_file(path), objects)
            with self.assertRaises(FileExistsError):
                backup.save_file(path, objects)
            document = json.loads(path.read_text())
            document['objects'][12]['data'] = '00'
            path.write_text(json.dumps(document))
            with self.assertRaises(ValueError):
                backup.load_file(path)

    def test_capacity_validation_precedes_clear(self):
        class Fake(backup.Connection):
            def __init__(self):
                self.commands = []
            def upload(self, obj, data, validate=False):
                self.commands.append(('validate', obj))
                return bytes((0 if obj == 0 else 5, 0, 0))
            def request(self, command, *args, **kwargs):
                self.commands.append(command)
                return bytes((0, 0, 0, 48, 0, 2))
        connection = Fake()
        with self.assertRaises(ValueError):
            connection.validate([b'object'] * 13)
        self.assertNotIn(6, connection.commands)


if __name__ == '__main__':
    unittest.main()
