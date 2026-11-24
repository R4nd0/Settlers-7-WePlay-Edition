"""Own one qualified mixed pair's transport and local services on Windows."""
import importlib
from pathlib import Path
import sys
import uuid

from crossplay_content import Catalog
from crossplay_host import Host
from crossplay_invitation import parse
from crossplay_runtime import SessionPump
from easytier_elevation import open_overlay
from internet_room import Room
from history_session import HistorySessionService


def gateway_classes():
    folder = Path(__file__).resolve().parent
    if not (folder / 'easytier_gateway.py').is_file(): folder = folder.parent / 'gold'
    previous = sys.path[:]
    try:
        sys.path.insert(0, str(folder))
        module = importlib.import_module('easytier_gateway')
        return module.HostGateway, module.EasyTierGateway
    finally: sys.path[:] = previous


class NetworkSession:
    """The launcher closes this owner after game exit or a cancelled launch.

    Invitations travel through the existing private channel. Neither this owner
    nor its transport publishes a room, logs a key, or owns the game process.
    """
    def __init__(self):
        self.overlay = self.pair = self.gateway = self.runtime = self.binding = None
        self.history=None
        self.closed = False

    @staticmethod
    def validate(catalog, consent):
        if type(consent) is not int or consent != 1:
            raise ValueError('internet_consent_required')
        if type(catalog) is not Catalog: raise ValueError('crossplay_catalog_invalid')
        if sys.platform != 'win32': raise ValueError('internet_windows_required')

    @classmethod
    def host(cls, root, catalog, *, room, history, name, consent,
             cancel_check=None, progress=None):
        cls.validate(catalog, consent)
        if type(room) is not Room: raise ValueError('crossplay_configuration_invalid')
        result = cls()
        try:
            # Host verifies the catalog and adapter before any network/UAC work.
            result.pair = Host(root, catalog, history=history, name=name)
            result.binding = result.pair.binding
            result.overlay = open_overlay(root, room, host=True, crossplay=True,
                cancel_check=cancel_check, progress=progress)
            result.overlay.start()
            host_gateway, _ = gateway_classes()
            result.gateway = host_gateway(result.overlay, broker_port=result.pair.broker.port)
            result.runtime = result.pair.start(gateway=result.gateway)
            result.check()
            return result
        except BaseException:
            result.close(); raise

    @classmethod
    def guest(cls, root, catalog, *, invitation, name, consent,
              cancel_check=None, progress=None):
        cls.validate(catalog, consent)
        invite = parse(invitation)
        # Fail before opening a tunnel for another edition or changed content.
        if catalog.verify(root, invite.edition) != invite.content:
            raise ValueError('crossplay_content_changed')
        if (not isinstance(name, str) or not 1 <= len(name) <= 64
                or any(not 32 <= ord(c) <= 126 for c in name)):
            raise ValueError('crossplay_configuration_invalid')
        result = cls()
        try:
            result.overlay = open_overlay(root, invite.room, host=False, crossplay=True,
                cancel_check=cancel_check, progress=progress)
            result.overlay.start()
            _, guest_gateway = gateway_classes()
            result.gateway = guest_gateway(result.overlay, peer=invite.room.coordinator)
            result.binding = invite.binding(root, catalog, port=result.gateway.port, name=name)
            # The local platform profile, opaque channel identity and Storm
            # principal are separate. Bind a fresh profile to this same owner;
            # never reinterpret channel bytes as an unqualified GUID layout.
            result.history=HistorySessionService(root,profile_id=str(uuid.uuid4()),name=name,
                                                 cancel_check=cancel_check)
            result.history.start()
            result.runtime = SessionPump(gateway=result.gateway,history=result.history).start()
            result.check()
            return result
        except BaseException:
            result.close(); raise

    def invitation(self):
        self.check()
        if self.pair is None: raise ValueError('crossplay_configuration_invalid')
        return self.pair.invitation(self.overlay.room).token()

    def attach(self, game):
        self.check()
        self.runtime.attach(game)
        return self

    def check(self):
        if self.closed or self.runtime is None:
            raise ValueError('crossplay_service_closed')
        self.runtime.check()
        if self.runtime.closed:
            raise ValueError('crossplay_service_closed')

    def close(self):
        if self.closed: return
        # Never remove the tunnel while its service thread can still use it.
        if self.runtime is not None: self.runtime.close()
        failed = False
        for owner in (self.pair, self.history, self.gateway, self.overlay):
            if owner is not None:
                try: owner.close()
                except Exception: failed = True
        if failed: raise RuntimeError('crossplay_service_cleanup_failed') from None
        self.closed = True

    def __enter__(self): return self

    def __exit__(self, *_args): self.close()
