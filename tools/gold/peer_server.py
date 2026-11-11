"""Independent disposable Gold logins sharing one loopback registration service.

Each client has its own launch secret, principal, ticket, profile and endpoints.
This is transport infrastructure, not peer/game admission or a History bridge.
Never print the launch configuration or save packets and account data.
"""
from contextlib import ExitStack
import argparse
import json
import os
from pathlib import Path
import select
import socket
import time

from account_config import ConfigService
from local_gatherings import LocalGatherings
from login_server import GoldLoginAdapter, MAX_DATAGRAM


def validate_peers(peers):
    if not isinstance(peers, (list, tuple)) or not 2 <= len(peers) <= 8:
        raise ValueError("Expected two to eight independent local launches")
    names, secrets = set(), set()
    for peer in peers:
        if not isinstance(peer, dict) or set(peer) != {"name", "secret"}:
            raise ValueError("Invalid local launch configuration")
        name, secret = peer["name"], peer["secret"]
        if (not isinstance(name, str) or not 1 <= len(name) <= 64
                or any(not 32 <= ord(c) <= 126 for c in name)
                or not isinstance(secret, str) or len(secret) != 64
                or any(c not in "0123456789abcdef" for c in secret)
                or name.casefold() in names or secret in secrets):
            raise ValueError("Local launches require distinct names and fresh secrets")
        names.add(name.casefold())
        secrets.add(secret)


class PeerLoginService:
    """Single-threaded bounded service; each socket routes to exactly one identity."""

    def __init__(self, peers):
        validate_peers(peers)
        self.resources = ExitStack()
        self.gatherings = LocalGatherings(discovery=True)
        self.adapters = []
        self.configs = []
        self.routes = {}
        self.endpoints = []
        self.closed = False
        try:
            principals, connection_ids = set(), set()
            for peer in peers:
                auth, backend = [self.resources.enter_context(socket.socket(
                    socket.AF_INET, socket.SOCK_DGRAM)) for _ in range(2)]
                for sock in (auth, backend):
                    sock.bind(("127.0.0.1", 0))
                    sock.setblocking(False)
                adapter = GoldLoginAdapter(peer["name"], peer["secret"], backend.getsockname()[1],
                    account_mode=True, session_mode=True, gatherings=self.gatherings)
                # No authenticated identifier may alias another configured peer.
                if adapter.principal in principals:
                    raise ValueError("Local principal collision; start a fresh service")
                if adapter.connection_id in connection_ids:
                    raise ValueError("Local connection collision; start a fresh service")
                principals.add(adapter.principal)
                connection_ids.add(adapter.connection_id)
                config = self.resources.enter_context(ConfigService(auth.getsockname()[1]))
                self.adapters.append(adapter)
                self.configs.append(config)
                self.routes[auth] = adapter, False
                self.routes[backend] = adapter, True
                self.endpoints.append({"port": auth.getsockname()[1],
                    "backend_port": backend.getsockname()[1], "config_port": config.server_port})
        except BaseException:
            self.resources.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.closed = True
        self.resources.close()
        # Every owner belongs to this disposable service. Release its records
        # without serializing profiles, launch secrets or station URLs.
        self.gatherings.records.clear()

    def pump(self, timeout=.1):
        if self.closed or not 0 <= timeout <= 1:
            raise ValueError("Invalid service pump")
        configs = {config.socket: config for config in self.configs}
        for sock in select.select(list(self.routes) + list(configs), (), (), timeout)[0]:
            if sock in configs:
                configs[sock].handle_request()
                continue
            data, peer = sock.recvfrom(MAX_DATAGRAM + 1)
            adapter, backend = self.routes[sock]
            for reply in adapter.handle(backend, peer, data, time.monotonic()):
                sock.sendto(reply, peer)
        now = time.monotonic()
        for adapter in self.adapters:
            for backend, peer, reply in adapter.retransmissions(now):
                sock = next(sock for sock, route in self.routes.items()
                    if route[0] is adapter and route[1] == backend)
                sock.sendto(reply, peer)

    def summary(self):
        return {"loopback_only": True, "local_registrations": len(self.gatherings.records),
            "peers": [{"registered": adapter.registered, "events": dict(adapter.events),
                "input_rejections": dict(adapter.input_rejections),
                "unsupported_rpcs": [{"protocol": p, "method": m, "argument_bytes": n, "count": c}
                    for (p, m, n), c in sorted(adapter.unsupported_rpcs.items())],
                "config_responses": config.responses}
                for adapter, config in zip(self.adapters, self.configs)],
            "raw_payload_saved": False, "game_session_admission": False}


def serve(peers, ready, report, duration):
    if type(duration) is not int or not 1 <= duration <= 600:
        raise ValueError("Duration must be bounded to 1..600 seconds")
    with PeerLoginService(peers) as service:
        ready.write_text(json.dumps({"loopback_only": True, "peers": service.endpoints}), encoding="utf-8")
        started = time.monotonic()
        while time.monotonic() - started < duration:
            service.pump()
            report.write_text(json.dumps(service.summary()), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ready-file", required=True, type=Path)
    parser.add_argument("--report-file", required=True, type=Path)
    parser.add_argument("--duration", type=int, default=120)
    args = parser.parse_args()
    # Environment-only disposable configuration; no secret-bearing CLI/file.
    value = os.environ.pop("WEPLAY_GOLD_LOCAL_PEERS", "")
    if not 1 <= len(value) <= 4096:
        parser.error("Expected bounded local launch configuration in the environment")
    try:
        peers = json.loads(value)
        validate_peers(peers)
    except (ValueError, TypeError):
        parser.error("Invalid independent local launch configuration")
    serve(peers, args.ready_file, args.report_file, args.duration)


if __name__ == "__main__":
    main()
