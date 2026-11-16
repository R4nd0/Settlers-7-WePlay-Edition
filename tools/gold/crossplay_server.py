"""Disposable command-line entry point for the shared loopback broker.

This command remains a development service, not native session admission.
The reusable implementation is packaged beside the ordinary launcher helpers.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'installer'))
from crossplay_broker import CrossplayBroker, Connection, HEADER, Launch, MAX_BUFFERED, MAX_PAYLOAD, PINS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ready-file', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=120)
    args = parser.parse_args()
    args.ready_file = args.ready_file.resolve()
    private_root = Path(__file__).resolve().parents[2]/'work'
    if (not 1 <= args.duration <= 600 or args.ready_file.exists()
            or not args.ready_file.is_relative_to(private_root.resolve())
            or not args.ready_file.parent.is_dir()):
        parser.error('Use a new ready file in an existing work directory and a 1..600 second lifetime')
    raw = os.environ.pop('WEPLAY_CROSSPLAY_LAUNCHES', '')
    if not 1 <= len(raw) <= 2048:
        parser.error('Expected disposable environment-only launch bindings')
    try:
        rows = json.loads(raw)
        launches = tuple(Launch(row['edition'], bytes.fromhex(row['key']),
            bytes.fromhex(row['native_identity']), bytes.fromhex(row['content'])) for row in rows)
        broker = CrossplayBroker(launches)
    except (ValueError, TypeError, KeyError):
        parser.error('Invalid launch bindings')
    with broker:
        with args.ready_file.open('x', encoding='utf-8') as stream:
            json.dump({'loopback_only': True, 'port': broker.port, 'session': broker.session.hex(),
                       'native_session_admission': False}, stream)
        deadline = time.monotonic()+args.duration
        while time.monotonic() < deadline and not broker.retired:
            broker.pump()
        while broker.connections and time.monotonic() < deadline:
            broker.pump()


if __name__ == '__main__':
    main()
