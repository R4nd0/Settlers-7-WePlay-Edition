"""Gold-specific local profile UI; no earned titles or Ubisoft login is created."""
import ast
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'lan'))
from bba import Archive

SOURCE = '5f335b7ebdf0707079009abda7334ecf5d83cfd3039776d2e775f39718255215'
MEMBER = 'Script/local/menu/panel/menu/customize/profileManager.lua'
TITLE_ANCHOR = 'e["titleData"]=l["Game"]["PlayerProfileGetTitles"]();'
NO_TITLE = '''
-- A disconnected Gold profile has no downloaded title catalog. The native
-- UI already defines type 28 as "No Title". Offer only that neutral choice;
-- never synthesize earned ranks, progression, purchases or achievements.
if _G.type(e.titleData) ~= "table" or _G.next(e.titleData) == nil then
 e.titleData = {{TitleType=28,CurrentRank=0,CurrentTitleValue=0,NextRankAt=0}}
end
'''
ACCOUNT_LABEL = '"{left}{color:#FFFFFFFF}Ubi.com Account:"'
LOCAL_LABEL = '"{left}{color:#FFFFFFFF}WePlay profile:"'
GAME_MEMBER = 'Script/local/menu/game.lua'
DISCONNECT_ANCHOR = 'if not l and not e["game"]["isDisconnected"]then'
# A local world needs no obsolete Orbit connection. Preserve the original
# connection value and every multiplayer disconnect/desync path.
LOCAL_DISCONNECT = ('if not l and not e["game"]["isDisconnected"] and '
                    '(e["game"]["isMultiplayerGame"] or e["Network"]["InNetworkMode"]())then')


def decoded_view(raw):
    """Gold uses the second header variable for its decoded string getter."""
    source = raw.decode('utf-8-sig')
    names = re.match(r'local (\w+),(\w+),(\w+),(\w+)=_G', source)
    table = re.search(r'=((?:\{\{)[\d.,{}]+\}),\{\},', source)
    if not names or not table:
        raise ValueError('Unknown Gold Lua string-table header.')
    rows = ast.literal_eval(table[1].replace('{', '[').replace('}', ']'))
    seed, strings = len(rows) % 256, []
    for row in rows:
        if len(row) == 1 and row[0] >= 555:
            strings.append(row[0] - seed - 555)
            continue
        previous, chars = seed, []
        for number in row:
            char = number - previous
            chars.append(chr(char % 256))
            previous = char
        strings.append(''.join(chars))
    start = source.find('\nend\nend\n', table.end())
    if start < 0:
        raise ValueError('Unknown Gold Lua string-table footer.')
    start += 9
    # Confirm the getter/global resolver roles rather than trusting a HE layout.
    header = source[:start]
    if not re.search(r'\b' + names[2] + ',' + names[3] + ',' + names[4] + r'=function\(', header):
        raise ValueError('Unknown Gold Lua decoder roles.')
    pattern = re.compile(r'\b(' + names[2] + '|' + names[3] + r')\((\d+)\)')
    pieces, offsets, position = [], [], start
    for match in pattern.finditer(source, start):
        index = int(match[2]) - 1
        if not 0 <= index < len(strings):
            raise ValueError('Gold Lua string index out of bounds.')
        pieces.append(source[position:match.start()])
        offsets.extend(range(position, match.start()))
        value = json.dumps(strings[index], ensure_ascii=True)
        if match[1] == names[3]:
            value = '_G[' + value + ']'
        pieces.append(value)
        offsets.extend([match.start()] * (len(value) - 1) + [match.end() - 1])
        position = match.end()
    pieces.append(source[position:])
    offsets.extend(range(position, len(source)))
    offsets.append(len(source))
    return source, ''.join(pieces), offsets


def profile_script(raw):
    for before, after in ((TITLE_ANCHOR, TITLE_ANCHOR + NO_TITLE), (ACCOUNT_LABEL, LOCAL_LABEL)):
        source, body, offsets = decoded_view(raw)
        if body.count(before) != 1:
            raise ValueError('Gold profile UI anchor does not match exactly once.')
        at = body.index(before)
        raw = (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf-8')
    return raw


def game_script(raw):
    source, body, offsets = decoded_view(raw)
    if body.count(DISCONNECT_ANCHOR) != 1:
        raise ValueError('Gold local-game disconnect anchor does not match exactly once.')
    at = body.index(DISCONNECT_ANCHOR)
    return (source[:offsets[at]] + LOCAL_DISCONNECT +
            source[offsets[at + len(DISCONNECT_ANCHOR)]:]).encode('utf-8')


def build(raw):
    if hashlib.sha256(raw).hexdigest() != SOURCE:
        raise ValueError('Gold profile UI requires the exact original Gold archive.')
    original = Archive(raw)
    replacements = {MEMBER: profile_script(original.read(MEMBER)),
                    GAME_MEMBER: game_script(original.read(GAME_MEMBER))}
    result = original.replace(replacements)
    verified = Archive(result)
    if original.entries.keys() != verified.entries.keys():
        raise ValueError('Gold profile UI changed the archive member set.')
    for name, entry in original.entries.items():
        if entry.values[4] == 256:
            continue
        expected = replacements.get(name, original.read(name))
        if verified.read(name) != expected:
            raise ValueError('Gold profile UI archive verification failed.')
    return result
