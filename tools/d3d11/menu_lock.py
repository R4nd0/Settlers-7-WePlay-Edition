"""Hash-guarded graphics-panel lock; preserve audio/gameplay/key settings."""
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'lan'))
from bba import Archive

NAME = 'Script/local/menu/panel/menu/options/settings.lua'
SOURCE = '41b5934276c80910a0254c9ea54bfe0fda3133ada3850bb3362fd540fa9297d7'
ARCHIVE = '2b1ad77ab1afda98e9d9aab8bbc91279607e1bbbac506d2b52de675550de08ad'
PATCHED = 'bac2f40fed02109aa607c5ce904db7cb56fcae6a4b88b05880cf0537c210a656'
ANCHOR = b'function r.create(i)l[n(123)]=i\nu()e[n(349)]'
FRAME_ANCHOR = b'local function W()l[n(420)]=l[n(125)][n(126)](n(54))'
FRAME_INSERT = (b'local function W()'
                b'l["starterGraphicsTab"]=l[n(125)][n(126)](n(54))'
                b'l[n(420)]=e["frame"]["create"]("starterGraphicsOriginal",'
                b'l["starterGraphicsTab"],0,0,l[n(125)]["getContentWidth"](),'
                b'l[n(125)]["getContentHeight"]())')
INSERT = (b'function r.create(i)l[n(123)]=i\nu()'
          b'e["ui"]["enable"](l["graphicsOptionsFrame"],false)'
          b'e["ui"]["hide"](l["graphicsOptionsFrame"])'
          b'e["label"]["create"](l["starterGraphicsTab"],{x=40,y=40},'
          b'"Grafikeinstellungen werden im Startdialog gewaehlt.",'
          b'e["font"]["normal"],e["UI"]["Anchor"]["Top"],e["UI"]["Anchor"]["Left"],700)'
          b'e["label"]["create"](l["starterGraphicsTab"],{x=40,y=100},'
          b'"Bitte das Spiel schliessen und die Grafik vor dem naechsten Start einstellen.",'
          b'e["font"]["normal"],e["UI"]["Anchor"]["Top"],e["UI"]["Anchor"]["Left"],500)'
          b'e[n(349)]')


def patch_script(source):
    if (hashlib.sha256(source).hexdigest() != SOURCE or source.count(ANCHOR) != 1
            or source.count(FRAME_ANCHOR) != 1):
        raise ValueError('Unbekanntes S7-Grafikmenue; keine Aenderung vorgenommen.')
    return source.replace(FRAME_ANCHOR, FRAME_INSERT, 1).replace(ANCHOR, INSERT, 1)


def build(raw):
    if hashlib.sha256(raw).hexdigest() != ARCHIVE:
        raise ValueError('Die Menuesperre benoetigt das bekannte lokale Interface-Archiv.')
    archive = Archive(raw)
    script = patch_script(archive.read(NAME))
    result = archive.replace({NAME: script})
    verified = Archive(result)
    for name, entry in archive.entries.items():
        if entry.values[4] != 256:
            expected = script if name == NAME else archive.read(name)
            if verified.read(name) != expected:
                raise ValueError('Vollstaendige Menuearchiv-Pruefung fehlgeschlagen.')
    return result
