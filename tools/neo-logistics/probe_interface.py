"""Build a private SP lifecycle probe from a fingerprinted WePlay interface.

No game assets are distributed. Use only an isolated, backed-up test copy and
the owning bounded process/memory runner. This is not a release transformation.
"""
from pathlib import Path
import argparse
import hashlib
import interface_patch as neo

HERE = Path(__file__).resolve().parent


def build(source, *, enabled=True):
    if type(enabled) is not bool:
        raise ValueError('Invalid probe mode')
    archive = neo.Archive(neo.build(source))
    changes = {}

    def edit(name, before, after):
        text, body, offsets = neo.decoder.decoded_view(changes.get(name, archive.read(name)))
        if body.count(before) != 1:
            raise ValueError('Probe anchor mismatch: ' + name)
        at = body.index(before)
        changes[name] = (text[:offsets[at]] + after + text[offsets[at+len(before)]:]).encode('utf8')

    edit('Script/local/start.lua', 'local l=e["require"]("local.tools.boot")',
         'e.Opts.map="Maps/Skirmish/s02_m06_Grassland_Mesa";e.Opts.noMapInMainMenu=true\n'
         'local l=e["require"]("local.tools.boot")')
    setup = '''
e.Network.SetGameMode(1)
e.map.loadSetupMapConfig(l.currentCommandLineMap())
local players=e.MapSetup.GetActivePlayers(true)
local ais=e.MapSetup.GetAINamesListSorted()
assert(#ais>=3 and e.MapSetup.GetAvailableSlots()==4)
for slot=2,4 do
 e.MapSetup.SetPlayerTypeAI(slot)
 e.MapSetup.SetProfileID(slot,ais[slot-1].profileID)
 e.MapSetup.SetAIDifficultyLevelByPlayerID(players[slot],2)
 e.MapSetup.SetAIPeacefulFlagByPlayerID(players[slot],false)
 e.MapSetup.SetTeamAffiliation(players[slot],slot)
end
assert(e.MapSetup.IsValid())
assert(e.require("local.tools.settings").weplayNeoLogistics.select(ENABLED)==ENABLED)
e.UI.WriteToConsole("Attempt to NEO-PROBE setup three-hard-AI")
'''.replace('ENABLED', 'true' if enabled else 'false')
    edit('Script/local/start.lua', 'e["map"]["loadSetupMapConfig"](l["currentCommandLineMap"]())', setup)
    probe = (HERE/'lifecycle_probe.lua').read_text(encoding='utf8')
    edit('Script/local/menu/game.lua', 'e["game"]={["callbacks"]=',
         'local weplayNeoProbe=(function(...)\n'+probe+'\nend)(e,'+('true' if enabled else 'false')+')\n'
         'e["game"]={["callbacks"]=')
    edit('Script/local/menu/game.lua', 'e["game"]["updateDefaultSystems"]()',
         'e["game"]["updateDefaultSystems"]();weplayNeoProbe();')
    return archive.replace(changes)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--off', action='store_true')
    args = parser.parse_args()
    result = build(args.source.read_bytes(), enabled=not args.off)
    with args.output.open('xb') as stream:
        stream.write(result)
    print('Private probe SHA256:', hashlib.sha256(result).hexdigest())
