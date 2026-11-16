"""Process-local launcher handoff for the version 1 native mixed-session contract.

This supplies no native Ready/start/save authority and enables no UI capability.
Content agreement belongs to admission; an arbitrary/generated digest is not a
substitute for that agreement. Authentication material is never written here.
"""
from dataclasses import dataclass, field
import re
import secrets

import compatibility

PREFIX = 'WEPLAY_CROSSPLAY_'
EXECUTABLE_EDITIONS = {
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab': 1,
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f': 2,
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410': 3,
}
# Filled only for production adapters that implement the contract. Previous
# solo/History-only fingerprints never become mixed-capable by migration.
ADAPTER_PINS = {}


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
                ((1, 2, 'host'), (1, 3, 'host'), (2, 1, 'guest'), (3, 1, 'guest'))
                or type(self.port) is not int or not 1 <= self.port <= 65535
                or not all(nonzero(value, size) for value, size in
                           ((self.session, 16), (self.key, 32), (self.identity, 16), (self.content, 32)))
                or not isinstance(self.name, str) or not 1 <= len(self.name) <= 64
                or any(not 32 <= ord(c) <= 126 for c in self.name)):
            raise ValueError('crossplay_configuration_invalid')
        if self.edition == 1 and (not 0 < int.from_bytes(self.identity[:4], 'little') <= 0x7fffffff
                                  or any(self.identity[4:])):
            raise ValueError('crossplay_configuration_invalid')
        if self.edition == 1:
            if not all(type(value) is int and 1024 <= value <= 65535 for value in
                       (self.gold_auth_port, self.gold_config_port)):
                raise ValueError('crossplay_configuration_invalid')
        elif self.gold_auth_port is not None or self.gold_config_port is not None:
            raise ValueError('crossplay_configuration_invalid')

    def environment(self, root, executable, inherited):
        root = compatibility.plain(root)
        if EXECUTABLE_EDITIONS.get(compatibility.digest(compatibility.plain(executable))) != self.edition:
            raise ValueError('crossplay_edition_mismatch')
        relative = 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe' if self.edition == 1 else 'Base/Bin/Settlers7R.exe'
        if compatibility.plain(executable) != compatibility.plain(root / relative):
            raise ValueError('crossplay_profile_invalid')
        profile = profile_directory(root, self.edition)
        result = clean_environment(inherited)
        result['WEPLAY_CROSSPLAY_V1'] = '1'
        fields = {'ROLE': self.role, 'PEER': str(self.peer), 'PORT': str(self.port),
                  'SESSION': self.session.hex(), 'KEY': self.key.hex(),
                  'IDENTITY': self.identity.hex(), 'CONTENT': self.content.hex(),
                  'NAME': self.name, 'PROFILE': str(profile)}
        if self.edition == 1:
            fields.update(GOLD_AUTH_PORT=str(self.gold_auth_port), GOLD_CONFIG_PORT=str(self.gold_config_port))
        result.update(('WEPLAY_CROSSPLAY_V1_' + key, value) for key, value in fields.items())
        return result
