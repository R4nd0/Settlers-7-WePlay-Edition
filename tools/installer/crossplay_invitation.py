"""Private guest-only mixed invitation; never contains the Gold host key."""
import base64
from dataclasses import dataclass, field
import json
import re

import crossplay_content
import crossplay_session
from internet_room import Room, parse as parse_room

PREFIX = 'weplayx1.'


@dataclass(frozen=True)
class Invitation:
    room: Room = field(repr=False)
    edition: int
    session: bytes = field(repr=False)
    key: bytes = field(repr=False)
    identity: bytes = field(repr=False)
    content: bytes = field(repr=False)

    def __post_init__(self):
        if (type(self.room) is not Room or type(self.edition) is not int or self.edition not in (2, 3)
                or not all(crossplay_session.nonzero(value, size) for value, size in
                           ((self.session, 16), (self.key, 32), (self.identity, 16), (self.content, 32)))):
            raise ValueError('crossplay_invitation_invalid')

    def token(self):
        value = dict(v=1, room=self.room.token(), edition=self.edition, session=self.session.hex(),
                     key=self.key.hex(), identity=self.identity.hex(), content=self.content.hex())
        raw = json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode('ascii')
        token = PREFIX + base64.urlsafe_b64encode(raw).decode('ascii').rstrip('=')
        if len(token) > 2048: raise ValueError('crossplay_invitation_invalid')
        return token

    def binding(self, root, catalog, *, port, name):
        if type(catalog) is not crossplay_content.Catalog:
            raise ValueError('crossplay_catalog_invalid')
        digest = catalog.verify(root, self.edition)
        if digest != self.content: raise ValueError('crossplay_content_changed')
        return crossplay_session.Binding(self.edition, 1, 'guest', port, self.session, self.key,
                                         self.identity, digest, name)


def parse(token):
    if not isinstance(token, str) or not token.startswith(PREFIX) or not len(PREFIX) < len(token) <= 2048:
        raise ValueError('crossplay_invitation_invalid')
    body = token[len(PREFIX):]
    if not re.fullmatch('[A-Za-z0-9_-]+', body): raise ValueError('crossplay_invitation_invalid')
    try:
        data = json.loads(base64.b64decode(body + '=' * (-len(body) % 4), altchars=b'-_', validate=True),
                          object_pairs_hook=crossplay_content.unique)
        if (not isinstance(data, dict) or set(data) != {'v', 'room', 'edition', 'session', 'key', 'identity', 'content'}
                or type(data['v']) is not int or data['v'] != 1):
            raise ValueError()
        values = []
        for name, size in (('session', 16), ('key', 32), ('identity', 16), ('content', 32)):
            value = data[name]
            if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{' + str(2 * size) + '}', value):
                raise ValueError()
            values.append(bytes.fromhex(value))
        result = Invitation(parse_room(data['room']), data['edition'], *values)
        if result.token() != token: raise ValueError()
        return result
    except (ValueError, TypeError, KeyError, UnicodeError):
        raise ValueError('crossplay_invitation_invalid') from None
