#!/usr/bin/env python3
"""Back up and restore the seq_test keyboard over USB MIDI.
Install dependencies with: python3 -m pip install mido python-rtmidi
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

PREFIX = (0x7D, ord('S'), ord('E'), ord('Q'), 1)


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return struct.pack('<I', value)


def encode_payload(data):
    return [nibble for byte in data for nibble in (byte & 15, byte >> 4)]


def decode_payload(data):
    if len(data) % 2 or any(byte > 15 for byte in data):
        raise ValueError('Invalid response encoding')
    return bytes(data[i] | data[i + 1] << 4 for i in range(0, len(data), 2))


def save_file(path, objects):
    document = {'format': 'seq_test_project', 'version': 1, 'objects': [
        {'data': item.hex(), 'sha256': hashlib.sha256(item).hexdigest()} for item in objects]}
    # Exclusive creation protects previous backups from accidental overwrite.
    with Path(path).open('x') as output:
        json.dump(document, output, indent=2)
        output.write('\n')


def load_file(path):
    document = json.loads(Path(path).read_text())
    if document.get('format') != 'seq_test_project' or document.get('version') != 1 or len(document.get('objects', [])) != 13:
        raise ValueError('Unsupported or incomplete project')
    objects = []
    for item in document['objects']:
        data = bytes.fromhex(item['data'])
        if not 0 < len(data) <= 6144 or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Corrupt project object')
        objects.append(data)
    return objects


class Connection:
    def __init__(self, midi, source, destination):
        self.midi, self.source, self.destination = midi, source, destination

    def request(self, command, obj=0, offset=0, data=b''):
        packet = (*PREFIX, command, obj, offset & 127, offset >> 7, *encode_payload(data))
        self.destination.send(self.midi.Message('sysex', data=packet))
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            for message in self.source.iter_pending():
                raw = tuple(message.data) if message.type == 'sysex' else ()
                if len(raw) < 10 or raw[:5] != PREFIX or raw[5:9] != (command | 64, obj, offset & 127, offset >> 7):
                    continue
                if raw[9]:
                    raise RuntimeError(f'Keyboard rejected command {command}, object {obj}: status {raw[9]}')
                return decode_payload(raw[10:])
            time.sleep(0.005)
        raise TimeoutError('No keyboard response; check USB MIDI ports and firmware')

    def snapshot(self):
        objects = []
        for obj in range(13):
            data, expected = bytearray(), None
            while expected is None or len(data) < expected:
                reply = self.request(1, obj, len(data))
                if len(reply) < 2:
                    raise ValueError('Short snapshot response')
                length = int.from_bytes(reply[:2], 'little')
                if not 0 < length <= 6144 or (expected is not None and length != expected):
                    raise ValueError('Inconsistent snapshot length')
                expected = length
                if not reply[2:] or len(data) + len(reply[2:]) > expected:
                    raise ValueError('Invalid snapshot chunk')
                data.extend(reply[2:])
            objects.append(bytes(data))
        return objects

    def upload(self, obj, data, validate=False):
        self.request(2, obj, len(data))
        for offset in range(0, len(data), 32):
            self.request(3, obj, offset, data[offset:offset + 32])
        return self.request(5 if validate else 4, obj, 1 if validate else 0, data=checksum(data))

    def validate(self, objects):
        lanes = events = chords = 0
        chord_capacity = None
        for obj, data in enumerate(objects):
            counts = self.upload(obj, data, validate=True)
            if len(counts) == 3:
                raise ValueError('Update keyboard firmware before restoring: complete chord-capacity preflight is unavailable')
            if len(counts) != 5 or not counts[4] or (chord_capacity is not None and counts[4] != chord_capacity):
                raise ValueError('Invalid validation response')
            lanes += counts[0]
            events += int.from_bytes(counts[1:3], 'little')
            chords += counts[3]
            chord_capacity = counts[4]
        capacity = self.request(9)
        if len(capacity) != 6:
            raise ValueError('Invalid capacity response')
        if lanes > capacity[3] or events > int.from_bytes(capacity[4:6], 'little') or chords > chord_capacity:
            raise ValueError('Project exceeds this firmware’s shared capacity')

    def restore(self, objects):
        self.request(6, data=b'SEQ!')
        for obj, data in enumerate(objects):
            self.upload(obj, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['ports', 'backup', 'restore', 'save', 'status'])
    parser.add_argument('file', nargs='?')
    parser.add_argument('--input', help='Exact MIDI input port name')
    parser.add_argument('--output', help='Exact MIDI output port name')
    args = parser.parse_args()
    import mido
    if args.action == 'ports':
        print('Inputs:', *mido.get_input_names(), sep='\n  ')
        print('Outputs:', *mido.get_output_names(), sep='\n  ')
        return
    if not args.input or not args.output:
        parser.error('--input and --output are required; use ports to list them')
    if args.action in ('backup', 'restore') and not args.file:
        parser.error('A project file is required')
    objects = load_file(args.file) if args.action == 'restore' else None
    if args.action == 'backup' and Path(args.file).exists():
        raise FileExistsError('Backup destination already exists')
    with mido.open_input(args.input) as source, mido.open_output(args.output) as destination:
        connection = Connection(mido, source, destination)
        try:
            if args.action == 'backup':
                save_file(args.file, connection.snapshot())
                print(f'Full project saved to {args.file}')
            elif args.action == 'restore':
                connection.validate(objects)
                previous = connection.snapshot()
                recovery = f'{args.file}.before-restore-{time.time_ns()}.json'
                save_file(recovery, previous)
                print(f'Previous project backed up to {recovery}')
                try:
                    connection.restore(objects)
                    if connection.snapshot() != objects:
                        raise RuntimeError('Restored project did not match backup')
                except Exception:
                    print('Restore failed; attempting to recover the previous project.')
                    connection.restore(previous)
                    raise
                print('Project restored in RAM. Use save to store it on board if it fits.')
            elif args.action == 'save':
                status = connection.request(8)[0]
                if status:
                    raise RuntimeError('On-board save did not fit; previous save preserved. Use backup.')
                print('Project saved on board.')
            else:
                status = connection.request(9)
                print(f'Save status: {status[0]} (0=saved, 2=too large, 3=invalid); unsaved changes: {bool(status[1])}; lanes: {status[2]}/{status[3]}')
        finally:
            try:
                connection.request(7)
            except (RuntimeError, TimeoutError):
                print("Could not release transfer lock; it expires after 10 seconds.")


if __name__ == '__main__':
    main()
