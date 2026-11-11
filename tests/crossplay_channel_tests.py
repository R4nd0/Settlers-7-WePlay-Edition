"""Integration of native x86 channel and independent Python loopback broker.

This exercises real TCP with synthetic launch metadata, not game crossplay.
"""
import argparse
import os
from pathlib import Path
import secrets
import subprocess
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/gold'))
from crossplay_server import CrossplayBroker, Launch, PINS


class AlteredBroker(CrossplayBroker):
    def __init__(self, launches, mutation):
        super().__init__(launches)
        self.mutation = mutation

    def _queue(self, connection, kind, source, body):
        start = len(connection.output)
        if kind == 2 and self.mutation == 'identity':
            body = body[:20]+body[4:20]+body[36:]  # Same canonical local/remote identity.
        if kind == 2 and self.mutation == 'projection':
            body = body[:52]+self.launches[connection.slot-1].native_identity+body[68:]
        if kind == 2 and self.mutation == 'edition':
            body = bytes((connection.slot,))+body[1:]
        super()._queue(connection, kind, source, body)
        if kind == 2 and self.mutation == 'mac':
            connection.output[-1] ^= 1
        if kind == 2 and self.mutation in ('sequence', 'session', 'source'):
            import hmac
            frame = bytearray(connection.output[start:-32])
            if self.mutation == 'sequence':
                frame[24:32] = struct.pack('<Q', 2)
            elif self.mutation == 'session':
                frame[8] ^= 0x80
            else:
                frame[32] = 3-connection.slot
            connection.output[start:] = frame+hmac.digest(self.launches[connection.slot-1].key, frame, 'sha256')


def run(executable, edition, mode='exchange', mutation=None):
    launches = (Launch(1, secrets.token_bytes(32), bytes((42,))+bytes(15), bytes((0x51,))*32),
                Launch(edition, secrets.token_bytes(32), bytes((0x22,))*16, bytes((0x51,))*32))
    broker = AlteredBroker(launches, mutation) if mutation else CrossplayBroker(launches)
    with broker:
        environment = os.environ.copy()
        environment.update(WEPLAY_CROSSPLAY_TEST_SESSION=broker.session.hex(),
            WEPLAY_CROSSPLAY_TEST_KEY_A=launches[0].key.hex(), WEPLAY_CROSSPLAY_TEST_KEY_B=launches[1].key.hex(),
            WEPLAY_CROSSPLAY_TEST_EXE_A=PINS[1].hex(), WEPLAY_CROSSPLAY_TEST_EXE_B=PINS[edition].hex())
        child = subprocess.Popen([str(executable), str(broker.port), str(edition), mode],
                                 env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic()+20
        try:
            while child.poll() is None and time.monotonic() < deadline:
                broker.pump()
            if child.poll() is None:
                child.terminate(); raise RuntimeError('Own native channel test exceeded its bounded lifetime')
            output, error = child.communicate(timeout=1)
            if child.returncode:
                raise RuntimeError('Native channel test failed: '+error.decode('utf-8', errors='replace'))
            if mode == 'exchange' and broker.relayed != 65:
                raise RuntimeError('Unexpected number of authenticated relay events')
            if mode == 'burst' and broker.relayed != 1024:
                raise RuntimeError('Burst did not drain every authenticated event')
            if mode == 'bad-key' and not broker.rejections:
                raise RuntimeError('Wrong launch key was not rejected')
            print(output.decode('utf-8').strip())
        finally:
            if child.poll() is None:
                child.terminate(); child.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native_test_executable', type=Path)
    args = parser.parse_args()
    executable = args.native_test_executable.resolve()
    for edition in (2, 3):
        run(executable, edition)
        run(executable, edition, 'burst')
        run(executable, edition, 'bad-key')
        for mutation in ('mac', 'identity', 'projection', 'edition', 'sequence', 'session', 'source'):
            run(executable, edition, 'reject', mutation)
    print('PASS 20 native/broker integration scenarios; actual game crossplay not tested.')


if __name__ == '__main__':
    main()
