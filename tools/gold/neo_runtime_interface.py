"""Guarded Gold NEO lifecycle/host option; no native authority or game assets."""
import hashlib
import re
from pathlib import Path
from bba import Archive
from profile_interface import decoded_view

HERE = Path(__file__).resolve().parent
GAME = 'Script/local/menu/game.lua'
SETTINGS = 'Script/local/tools/settings.lua'
HOST = 'Script/local/menu/panel/menu/play/multiplayer/hostLobby.lua'
MEMBER_PINS = {
    GAME: '7d50ced912915730ce63cbb73f07ce1dbd9c3d4f3cb70bcd9c729bb9169943e8',
    SETTINGS: '44d47f2daf6d7d6044ab56980f73779f4f06faa9551489289ee6c9b8707e9124',
}


def replacements(archive):
    """Require the exact supported Gold lifecycle; preserve every other member."""
    for name, pin in MEMBER_PINS.items():
        if hashlib.sha256(archive.read(name)).hexdigest() != pin:
            raise ValueError('Unsupported Gold NEO lifecycle member: ' + name)
    settings = archive.read(SETTINGS).decode('utf-8-sig')
    if not settings.endswith('return n\n'):
        raise ValueError('Gold NEO settings return mismatch')
    runtime = (HERE.parent / 'neo-logistics/runtime.lua').read_text(encoding='utf-8')
    settings = settings[:-len('return n\n')] + (
        'n.weplayNeoLogistics=(function(...)\n' + runtime +
        '\nend)(e,l,n.store,nil)\nreturn n\n')
    game, body, _ = decoded_view(archive.read(GAME))
    if body.count('e["game"]={') != 1 or not re.search(r'local m=e\["require"\]\("local.tools.pickleJar"\)', body):
        raise ValueError('Gold NEO game/real pickle jar mismatch')
    game += '\ne.require("local.tools.settings").weplayNeoLogistics.hookGame(e.game,m)\n'
    host, body, _ = decoded_view(archive.read(HOST))
    if not host.endswith('return d\n') or body.count('function d.create(') != 1:
        raise ValueError('Gold NEO original host panel mismatch')
    host = host[:-len('return d\n')] + (
        ';(function(...)\n' + (HERE / 'neo_lobby.lua').read_text(encoding='utf-8') +
        '\nend)(n,d,function() return e end,n.UI.WriteToConsole("weplay-crossplay-v1:mode")=="host")\nreturn d\n')
    return {SETTINGS: settings.encode(), GAME: game.encode(), HOST: host.encode()}


def build(raw):
    archive = Archive(raw)
    changed = replacements(archive)
    result = archive.replace(changed)
    check = Archive(result)
    for name, entry in archive.entries.items():
        if entry.values[4] != 256 and check.read(name) != changed.get(name, archive.read(name)):
            raise ValueError('Unrelated Gold NEO archive member changed')
    return result
