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
from crossplay_room_broker import CrossplayRoomBroker


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

class AlteredRoomBroker(CrossplayRoomBroker):
    def __init__(self, launches, host_slot, mutation):
        super().__init__(launches, host_slot=host_slot, generation=7)
        self.mutation=mutation

    def _connected(self, viewer):
        body=bytearray(super()._connected(viewer))
        if viewer==1:
            if self.mutation=='generation':
                body[4]^=1
            elif self.mutation=='edition':
                body[40]=0
            elif self.mutation=='projection':
                body[76:92]=bytes(16)
            elif self.mutation=='duplicate':
                body[96:112]=body[44:60]
            elif self.mutation=='slot':
                body[42]=0
            else:
                raise ValueError('Unknown mutation')
        return bytes(body)

def run_room(executable, editions, host_slot, mutation=None, *, admission=False):
    launches=tuple(Launch(int(edition), secrets.token_bytes(32),
                          bytes([42+i])+bytes(15) if edition=='1' else bytes([42+i])*16,
                          bytes([0x51])*32)
                   for i,edition in enumerate(editions))
    broker=AlteredRoomBroker(launches,host_slot,mutation) if mutation else CrossplayRoomBroker(
        launches,host_slot=host_slot,generation=7)
    with broker:
        environment=os.environ.copy()
        environment['WEPLAY_CROSSPLAY_TEST_SESSION']=broker.session.hex()
        for slot,launch in enumerate(launches,1):
            environment[f'WEPLAY_CROSSPLAY_ROOM_KEY_{slot}']=launch.key.hex()
            environment[f'WEPLAY_CROSSPLAY_ROOM_EXE_{slot}']=PINS[launch.edition].hex()
        with subprocess.Popen([str(executable),str(broker.port),editions,str(host_slot),
                               'room-reject' if mutation else 'room'],
                              env=environment,stderr=subprocess.PIPE) as child:
            try:
                deadline=time.monotonic()+15
                while child.poll() is None and time.monotonic()<deadline:
                    broker.pump(.01)
                if child.poll() is None:
                    raise RuntimeError('Native room test exceeded its deadline')
                _,errors=child.communicate(timeout=1)
                if child.returncode:
                    raise RuntimeError('Native room test failed: '+errors.decode('utf-8',errors='replace'))
                relay_count=len(editions)*(len(editions)-1)
                expected=2*relay_count if admission else relay_count+1
                if mutation is None and (broker.relayed!=expected or broker.rejections):
                    raise RuntimeError('Unexpected room relay or rejection count')
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=3)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native_test_executable', type=Path)
    parser.add_argument('--room-admission', type=Path)
    args = parser.parse_args()
    executable = args.native_test_executable.resolve()
    for edition in (2, 3):
        run(executable, edition)
        run(executable, edition, 'burst')
        run(executable, edition, 'bad-key')
        for mutation in ('mac', 'identity', 'projection', 'edition', 'sequence', 'session', 'source'):
            run(executable, edition, 'reject', mutation)
    cases=[('13',2),('22',1),('123',1),('123',2),('123',3),
           ('331',2),('1123',4),('11111',5),('332211',4),
           ('3122311',7),('12312312',8)]
    for editions,host in cases:
        run_room(executable,editions,host)
    mutations=['generation','edition','projection','duplicate','slot']
    for mutation in mutations:
        run_room(executable,'123',2,mutation)
    print(f'PASS 20 pair and {len(cases)+len(mutations)} room native/broker scenarios; no game executed.')
    if args.room_admission:
        for editions,host in cases:
            run_room(args.room_admission.resolve(),editions,host,admission=True)
        print(f'PASS {len(cases)} complete room admissions through WPX2 with modeled game callbacks.')


if __name__ == '__main__':
    main()
