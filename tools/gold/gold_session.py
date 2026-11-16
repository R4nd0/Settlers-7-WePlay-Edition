"""One owned, in-memory local Gold service for a version 1 mixed session.

No vendor account data, phantom peer, native Ready or world state is supplied.
The caller pumps this service and closes it with the owned game/session.
"""
from contextlib import ExitStack
import hmac
import select
import socket
import time

from account_config import ConfigService
from local_gatherings import LocalGatherings
from login_server import GoldLoginAdapter, MAX_DATAGRAM
from nat_traversal import ProbeRelay


def local_secret(client_key, session):
    if (type(client_key) is not bytes or len(client_key) != 32 or not any(client_key)
            or type(session) is not bytes or len(session) != 16 or not any(session)):
        raise ValueError('crossplay_configuration_invalid')
    return hmac.digest(client_key, b'S7X-GOLD-LOCAL-AUTH-1' + session, 'sha256').hex()


class GoldSessionService:
    def __init__(self, *, name, client_key, session, identity):
        if (not isinstance(name, str) or not 1 <= len(name) <= 64
                or any(not 32 <= ord(c) <= 126 for c in name)
                or type(identity) is not bytes or len(identity) != 16
                or not 1 <= int.from_bytes(identity[:4], 'little') <= 0x7fffffff
                or any(identity[4:])):
            raise ValueError('crossplay_configuration_invalid')
        password = local_secret(client_key, session)
        self.resources = ExitStack(); self.closed = False
        self.gatherings = LocalGatherings(discovery=True)
        self.adapter = self.probes = self.config = None
        try:
            self.auth, self.backend = [self.resources.enter_context(socket.socket(
                socket.AF_INET, socket.SOCK_DGRAM)) for _ in range(2)]
            for sock in (self.auth, self.backend):
                sock.bind(('127.0.0.1', 0)); sock.setblocking(False)
            self.auth_port = self.auth.getsockname()[1]
            self.backend_port = self.backend.getsockname()[1]
            self.adapter = GoldLoginAdapter(name, password, self.backend_port,
                account_mode=True, session_mode=True, gatherings=self.gatherings,
                loopback_slot=2, principal=int.from_bytes(identity[:4], 'little'))
            self.config = self.resources.enter_context(ConfigService(self.auth_port, loopback_slot=2))
            self.config_port = self.config.server_port
            self.probes = ProbeRelay((self.adapter,), self.gatherings)
            self.adapter.probe_relay = self.probes
            self.routes = {self.auth: False, self.backend: True}
        except BaseException:
            self.close(); raise

    def __enter__(self): return self

    def __exit__(self, *_args): self.close()

    def close(self):
        if self.closed: return
        self.closed = True
        self.resources.close()
        self.gatherings.records.clear()
        if self.probes is not None: self.probes.jobs.clear()
        if self.adapter is not None:
            self.adapter.connections.clear()
            self.adapter.profile = None

    def pump(self, timeout=.01):
        if self.closed or type(timeout) not in (int, float) or not 0 <= timeout <= 1:
            raise ValueError('crossplay_service_closed')
        configs = self.config.socket
        for sock in select.select([*self.routes, configs], (), (), timeout)[0]:
            if sock is configs:
                self.config.handle_request()
                continue
            backend = self.routes[sock]
            for _ in range(64):
                try: data, peer = sock.recvfrom(MAX_DATAGRAM + 1)
                except BlockingIOError: break
                for reply in self.adapter.handle(backend, peer, data, time.monotonic()):
                    sock.sendto(reply, peer)
        now = time.monotonic(); self.probes.pump(now)
        for backend, peer, reply in self.adapter.retransmissions(now):
            (self.backend if backend else self.auth).sendto(reply, peer)

    def summary(self):
        return {'loopback_only': True, 'configured_gold_identities': 1,
                'local_registrations': len(self.gatherings.records),
                'events': dict(self.adapter.events), 'input_rejections': dict(self.adapter.input_rejections),
                'config_responses': self.config.responses, 'closed': self.closed,
                'raw_payload_saved': False, 'game_session_admission': False}
