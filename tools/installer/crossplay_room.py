"""Own the local transport and platform services of a verified mixed room."""
from contextlib import ExitStack
from dataclasses import dataclass
from pathlib import Path
import secrets
import threading
import uuid

from crossplay_broker import Launch
from crossplay_content import Catalog
from crossplay_host import gold_service_class
from crossplay_room_broker import CrossplayRoomBroker
from crossplay_runtime import SessionPump
from crossplay_session import RoomBinding, profile_directory, native_identity
from history_session import HistorySessionService
import platform_standalone


@dataclass(frozen=True)
class Player:
    root: Path
    edition: int
    name: str


class LocalRoom:
    def __init__(self, players, catalog, *, host_slot, map_path, map_slots, capacity):
        if (type(catalog) is not Catalog or type(players) is not tuple
                or not 2 <= len(players) <= 8 or type(map_slots) is not tuple
                or len(players) != len(map_slots)
                or any(type(player) is not Player for player in players)
                or not 1 <= sum(player.edition == 1 for player in players) < len(players)):
            raise ValueError('crossplay_configuration_invalid')

        self.players = tuple(Player(profile_directory(p.root, p.edition).parents[2],
                                    p.edition, p.name) for p in players)
        if len({player.root for player in self.players}) != len(players):
            raise ValueError('crossplay_profile_invalid')
        self.catalog = catalog
        self.resources = ExitStack()
        self.runtime = None
        self.claimed_slots = set()
        self.lock = threading.Lock()
        self.closed = False

        session = secrets.token_bytes(16)
        launches = []
        for player in self.players:
            platform_standalone.load(player.root)
            digest = catalog.verify(player.root, player.edition)
            launches.append(Launch(player.edition, secrets.token_bytes(32),
                                   native_identity(player.edition), digest))

        with ExitStack() as resources:
            broker = resources.enter_context(CrossplayRoomBroker(tuple(launches),
                host_slot=host_slot, session=session))
            services, bindings = [], []
            for slot, (player, launch) in enumerate(zip(self.players, launches), 1):
                ports = {}
                if player.edition == 1:
                    auth = resources.enter_context(gold_service_class()(name=player.name,
                        client_key=launch.key, session=session, identity=launch.native_identity))
                    ports.update(gold_auth_port=auth.auth_port, gold_config_port=auth.config_port)
                    services.append(auth)
                binding = RoomBinding(edition=player.edition, port=broker.port, session=session,
                    key=launch.key, identity=launch.native_identity, content=launch.content,
                    name=player.name, slot=slot, host_slot=host_slot, generation=broker.generation,
                    map_path=map_path, map_slots=map_slots, capacity=capacity, **ports)
                bindings.append(binding)
                if player.edition != 1:
                    services.append(resources.enter_context(HistorySessionService(player.root,
                        profile_id=str(uuid.uuid4()), name=player.name)))
            self.runtime = SessionPump(broker=broker, services=services, game_limit=len(players)).start()
            self.bindings = tuple(bindings)
            self.resources = resources.pop_all()

    def launch(self, slot, *, environment=None, arguments=(), graphics_options=None, on_initialized=None):
        with self.lock:
            if self.closed or self.runtime.closed:
                raise ValueError('crossplay_service_closed')
            if type(slot) is not int or not 1 <= slot <= len(self.players) or slot in self.claimed_slots:
                raise ValueError('crossplay_configuration_invalid')
            self.runtime.check()
            self.claimed_slots.add(slot)

        def initialized(game):
            if on_initialized is not None:
                on_initialized(game)
            self.runtime.attach(game)

        return platform_standalone.launch(self.players[slot - 1].root, environment=environment,
            arguments=arguments, graphics_options=graphics_options, on_initialized=initialized,
            crossplay=self.bindings[slot - 1], crossplay_catalog=self.catalog)

    def close(self):
        if self.closed:
            return
        self.runtime.close()
        self.resources.close()
        self.closed = True
        self.runtime.check()

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()
