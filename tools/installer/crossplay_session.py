"""Process-local launcher handoff for native mixed pairs and rooms.

This supplies no native Ready/start/save authority and enables no UI capability.
Content agreement belongs to admission; an arbitrary/generated digest is not a
substitute for that agreement. Authentication material is never written here.
"""
from dataclasses import dataclass, field
import re
import secrets

import compatibility
from platform_standalone import (GOLD_LOD400,GOLD_RC403,GOLD_RC405,GOLD_RC409,
                                 HISTORY_LOD400,HISTORY_RC405,HISTORY_RC409,LOD_GOLD,LOD_HISTORY)

PREFIX = 'WEPLAY_CROSSPLAY_'
EXECUTABLE_EDITIONS = {
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab': 1,
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f': 2,
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410': 3,
}
# Filled only for production adapters that implement the contract. Previous
# solo/History-only fingerprints never become mixed-capable by migration.
ADAPTER_PINS = {
    1: (GOLD_RC403,GOLD_RC405,LOD_GOLD,GOLD_RC409,GOLD_LOD400),
    2: (HISTORY_RC405,LOD_HISTORY,HISTORY_RC409,HISTORY_LOD400),
    3: (HISTORY_RC405,LOD_HISTORY,HISTORY_RC409,HISTORY_LOD400),
}


def clean_environment(inherited):
    """Never inherit activation, stale epochs or keys from the launcher's parent."""
    return {key: value for key, value in inherited.items()
            if not key.upper().startswith(PREFIX)}


def nonzero(value, size):
    return type(value) is bytes and len(value) == size and any(value)


def native_identity(edition):
    if type(edition) is not int or edition not in (1, 2, 3):
        raise ValueError('crossplay_configuration_invalid')
    if edition == 1:
        # Gold owns a numeric namespace; never truncate a History GUID into it.
        return (secrets.randbelow(0x7fffffff) + 1).to_bytes(4, 'little') + bytes(12)
    while True:
        value = secrets.token_bytes(16)
        if any(value):
            return value


def profile_directory(root, edition):
    if type(edition) is not int or edition not in (1, 2, 3):
        raise ValueError('crossplay_configuration_invalid')
    root = compatibility.plain(root)
    suffix = 'Profiles' if edition == 1 else 'THE SETTLERS 7 - History Edition'
    profile = compatibility.plain(root / 'LocalData/ProfileRoot' / suffix)
    text = str(profile)
    # Match the portable-profile native boundary, including MAX_PATH headroom.
    if (not profile.is_dir() or not re.match(r'^[A-Za-z]:\\', text)
            or len(text.encode('utf-16-le')) // 2 + 64 >= 260
            or any(ord(c) < 32 or c in '/:*?"<>|' for c in text[2:])
            or any(part in ('.', '..') or part.endswith(('.', ' ')) for part in profile.parts[1:])):
        raise ValueError('crossplay_profile_invalid')
    return profile


def _validate_local(binding):
    if (type(binding.edition) is not int or binding.edition not in (1, 2, 3)
            or type(binding.port) is not int or not 1 <= binding.port <= 65535
            or not all(nonzero(value, size) for value, size in
                       ((binding.session, 16), (binding.key, 32), (binding.identity, 16), (binding.content, 32)))
            or not isinstance(binding.name, str) or not 1 <= len(binding.name) <= 64
            or any(not 32 <= ord(c) <= 126 for c in binding.name)):
        raise ValueError('crossplay_configuration_invalid')
    if binding.edition == 1:
        if (not 0 < int.from_bytes(binding.identity[:4], 'little') <= 0x7fffffff
                or any(binding.identity[4:])
                or not all(type(value) is int and 1024 <= value <= 65535 for value in
                           (binding.gold_auth_port, binding.gold_config_port))):
            raise ValueError('crossplay_configuration_invalid')
    elif binding.gold_auth_port is not None or binding.gold_config_port is not None:
        raise ValueError('crossplay_configuration_invalid')


def _environment(binding, root, executable, inherited, protocol, fields):
    root = compatibility.plain(root)
    executable = compatibility.plain(executable)
    if EXECUTABLE_EDITIONS.get(compatibility.digest(executable)) != binding.edition:
        raise ValueError('crossplay_edition_mismatch')
    relative = ('Data/Base/_Dbg/Bin/Release/Settlers7R.exe' if binding.edition == 1
                else 'Base/Bin/Settlers7R.exe')
    if executable != compatibility.plain(root / relative):
        raise ValueError('crossplay_profile_invalid')
    profile = profile_directory(root, binding.edition)
    result = clean_environment(inherited)
    result[protocol] = '1'
    fields.update(PORT=str(binding.port), SESSION=binding.session.hex(), KEY=binding.key.hex(),
                  IDENTITY=binding.identity.hex(), CONTENT=binding.content.hex(),
                  NAME=binding.name, PROFILE=str(profile))
    if binding.edition == 1:
        fields.update(GOLD_AUTH_PORT=str(binding.gold_auth_port), GOLD_CONFIG_PORT=str(binding.gold_config_port))
    result.update((protocol + '_' + key, value) for key, value in fields.items())
    return result


@dataclass(frozen=True)
class Binding:
    edition: int
    peer: int
    role: str
    port: int
    session: bytes = field(repr=False)
    key: bytes = field(repr=False)
    identity: bytes = field(repr=False)
    content: bytes = field(repr=False)
    name: str
    gold_auth_port: int | None = None
    gold_config_port: int | None = None

    def __post_init__(self):
        if (type(self.edition) is not int or type(self.peer) is not int
                or (self.edition, self.peer, self.role) not in
                ((1, 2, 'host'), (1, 3, 'host'), (2, 1, 'guest'), (3, 1, 'guest'))):
            raise ValueError('crossplay_configuration_invalid')
        _validate_local(self)

    def environment(self, root, executable, inherited):
        return _environment(self, root, executable, inherited, 'WEPLAY_CROSSPLAY_V1',
                            {'ROLE': self.role, 'PEER': str(self.peer)})


@dataclass(frozen=True)
class RoomBinding:
    edition: int
    port: int
    session: bytes = field(repr=False)
    key: bytes = field(repr=False)
    identity: bytes = field(repr=False)
    content: bytes = field(repr=False)
    name: str
    slot: int
    host_slot: int
    generation: int
    map_path: str
    map_slots: tuple[int, ...]
    capacity: int
    gold_auth_port: int | None = None
    gold_config_port: int | None = None

    def __post_init__(self):
        _validate_local(self)
        if (type(self.capacity) is not int or not 2 <= self.capacity <= 8
                or type(self.map_slots) is not tuple or not 2 <= len(self.map_slots) <= self.capacity
                or any(type(slot) is not int or not 1 <= slot <= self.capacity for slot in self.map_slots)
                or len(set(self.map_slots)) != len(self.map_slots)
                or any(type(slot) is not int or not 1 <= slot <= len(self.map_slots)
                       for slot in (self.slot, self.host_slot))
                or type(self.generation) is not int or not 1 <= self.generation <= 0xffffffff
                or not isinstance(self.map_path, str) or not 1 <= len(self.map_path) <= 255
                or ':' in self.map_path or any(not 32 <= ord(char) <= 126 for char in self.map_path)
                or any(part in ('', '.', '..') for part in re.split(r'[/\\]', self.map_path))):
            raise ValueError('crossplay_configuration_invalid')

    def environment(self, root, executable, inherited):
        # The room adapter compares this handoff with the original map path verbatim.
        map_path = self.map_path.replace('/', '\\')
        return _environment(self, root, executable, inherited, 'WEPLAY_CROSSPLAY_ROOM_V2',
                            {'SLOT': str(self.slot), 'HOST_SLOT': str(self.host_slot),
                             'GENERATION': str(self.generation), 'MAP': map_path, 'CAPACITY': str(self.capacity),
                             'MAP_SLOTS': ''.join(str(slot) for slot in self.map_slots)})
