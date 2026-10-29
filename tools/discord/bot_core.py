"""Public configuration and bounded, secret-free lobby status projection."""
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import time
import unicodedata

SETTINGS_FORMAT = 'weplay-discord-bot-settings-v1'
FEED_FORMAT = 'weplay-discord-public-status-v1'
TOKEN_ENVIRONMENT = 'WEPLAY_DISCORD_BOT_TOKEN'
COMMUNITY_URL = 'https://discord.gg/j2eQgVzssc'


class BotError(ValueError):
    pass


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise BotError('path_invalid')
    return path.resolve(strict=False)


def record(path, limit):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise BotError('record_invalid')
            result[key] = value
        return result
    path = plain(path)
    with path.open('rb') as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit: raise BotError('record_invalid')
    try:
        return json.loads(raw, object_pairs_hook=unique)
    except (ValueError, UnicodeError):
        raise BotError('record_invalid') from None


def snowflake(value):
    if (not isinstance(value, str) or not re.fullmatch(r'[1-9][0-9]{16,19}', value)
            or int(value) > 2**64 - 1):
        raise BotError('settings_invalid')
    return int(value)


@dataclass(frozen=True)
class Settings:
    application_id: int
    guild_id: int
    channel_id: int


def settings(path):
    value = record(path, 8192)
    if (not isinstance(value, dict) or set(value) != {'format','application_id','guild_id',
            'channel_id','channel_name','transport','token_environment'} or
            value['format'] != SETTINGS_FORMAT or value['transport'] != 'gateway' or
            value['token_environment'] != TOKEN_ENVIRONMENT or
            not isinstance(value['channel_name'], str) or len(value['channel_name']) > 100):
        raise BotError('settings_invalid')
    return Settings(*(snowflake(value[key]) for key in ('application_id','guild_id','channel_id')))


def title(value):
    if (not isinstance(value, str) or not 1 <= len(value) <= 80 or value != value.strip()
            or any(unicodedata.category(c).startswith('C') for c in value) or
            any(word in value.lower() for word in ('weplay1.', '://', 'discord.gg', '@'))):
        raise BotError('feed_invalid')
    return value


def public_status(path, *, now=None):
    """No guessed match state. Counts are leases/session records, not matches."""
    if path is None: raise BotError('feed_missing')
    try: value = record(path, 65536)
    except FileNotFoundError: raise BotError('feed_missing') from None
    if (not isinstance(value, dict) or set(value) != {'format','observed_at','rooms'} or
            value['format'] != FEED_FORMAT):
        raise BotError('feed_invalid')
    stamp = value['observed_at']; current = time.time() if now is None else now
    if type(stamp) not in (int,float) or not math.isfinite(stamp): raise BotError('feed_invalid')
    if not -5 <= current - stamp <= 90: raise BotError('feed_stale')
    rooms = value['rooms']
    if not isinstance(rooms, list) or len(rooms) > 16: raise BotError('feed_invalid')
    result = []; seen = set()
    for room in rooms:
        if (not isinstance(room, dict) or set(room) != {'public_id','title','players','capacity','sessions'} or
                not isinstance(room['public_id'], str) or not re.fullmatch('[0-9a-f]{32}', room['public_id']) or
                room['public_id'] in seen):
            raise BotError('feed_invalid')
        seen.add(room['public_id'])
        if (any(type(room[key]) is not int for key in ('players','capacity','sessions')) or
                not 1 <= room['capacity'] <= 8 or not 0 <= room['players'] <= room['capacity'] or
                not 0 <= room['sessions'] <= 8):
            raise BotError('feed_invalid')
        result.append(dict(title=title(room['title']), players=room['players'],
                           capacity=room['capacity'], sessions=room['sessions']))
    return result


def listing(path):
    try: rooms = public_status(path)
    except (BotError, OSError):
        return {'title':'WePlay · Raumübersicht',
                'description':'Die Lobby-Datenquelle fehlt, ist ungültig oder veraltet. '
                    'Daraus lässt sich keine leere Spieleliste ableiten. '
                    'Private Einladungen können weiterhin direkt zwischen Mitspielern geteilt werden.',
                'color':0xB7791F}
    fields = []
    for room in rooms:
        # No identifiers, addresses, invitation secrets or arbitrary URLs leave
        # this projection. Discord mentions are separately disabled by the client.
        safe_title = re.sub(r'([\\`*_~|>\[\]])', r'\\\1', room['title'])
        fields.append({'name':safe_title, 'value':
            f"Angemeldete Spielclients: {room['players']}/{room['capacity']}\n"
            f"Sitzungsdatensätze: {room['sessions']}\nSpielzustand und Beitritt nicht verifiziert.",
            'inline':False})
    return {'title':'WePlay · Öffentlich gemeldete Räume',
            'description':'Nur ausdrücklich veröffentlichte Räume. Angemeldete Clients und '
                'Sitzungsdatensätze sind kein Nachweis einer laufenden Partie.' if rooms else
                'Die aktuelle Datenquelle meldet keine öffentlichen Räume. Private Räume sind nicht enthalten.',
            'fields':fields,'color':0x1E5BA4}
