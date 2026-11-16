"""Construct one qualified Gold-host pair and its owned local services."""
from contextlib import ExitStack
import importlib
from pathlib import Path
import secrets
import sys

from crossplay_broker import CrossplayBroker, Launch
from crossplay_content import Catalog
from crossplay_invitation import Invitation
from crossplay_runtime import SessionPump
from crossplay_session import Binding, native_identity


def gold_service_class():
    # Installed launcher helpers are flat. Source and package tools retain
    # installer/ and gold/ siblings; both layouts use the same authored module.
    folder = Path(__file__).resolve().parent
    if not (folder / 'gold_session.py').is_file(): folder = folder.parent / 'gold'
    previous = sys.path[:]
    try:
        sys.path.insert(0, str(folder))
        # The legacy protocol constructs its profile lazily. Resolve that
        # dependency before restoring the scoped source/package search path.
        importlib.import_module('account_profile')
        return importlib.import_module('gold_session').GoldSessionService
    finally: sys.path[:] = previous


class Host:
    def __init__(self, root, catalog, *, history, name):
        if (type(catalog) is not Catalog or type(history) is not int or history not in (2, 3)
                or not isinstance(name, str) or not 1 <= len(name) <= 64
                or any(not 32 <= ord(c) <= 126 for c in name)):
            raise ValueError('crossplay_configuration_invalid')
        content = catalog.verify(root, 1)
        self.catalog = catalog; self.closed = False; self.runtime = None
        self.resources = ExitStack()
        session = secrets.token_bytes(16)
        launches = (Launch(1, secrets.token_bytes(32), native_identity(1), content),
                    Launch(history, secrets.token_bytes(32), native_identity(history), content))
        try:
            self.broker = self.resources.enter_context(CrossplayBroker(launches, session))
            self.gold = self.resources.enter_context(gold_service_class()(name=name, client_key=launches[0].key,
                session=session, identity=launches[0].native_identity))
            self.binding = Binding(1, history, 'host', self.broker.port, session, launches[0].key,
                launches[0].native_identity, content, name, self.gold.auth_port, self.gold.config_port)
            self._guest = launches[1]
        except BaseException:
            self.close(); raise

    def invitation(self, room):
        if self.closed or self.broker.retired or (self.runtime is not None and self.runtime.closed):
            raise ValueError('crossplay_service_closed')
        guest = self._guest
        return Invitation(room, guest.edition, self.broker.session, guest.key, guest.native_identity, guest.content)

    def start(self, *, game=None, gateway=None):
        if self.closed or self.runtime is not None: raise ValueError('crossplay_service_closed')
        self.runtime = SessionPump(broker=self.broker, gold=self.gold, gateway=gateway)
        if game is not None: self.runtime.attach(game)
        return self.runtime.start()

    def close(self):
        if self.closed: return
        if self.runtime is not None: self.runtime.close()
        self.resources.close(); self.closed = True

    def __enter__(self): return self

    def __exit__(self, *_args): self.close()
