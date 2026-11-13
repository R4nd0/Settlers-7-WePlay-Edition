"""Stage opt-in, fingerprinted pair automation; never deploy to a game.

Keep the quiet Gold menu and the native History guest/map-reload integration.
Only the three lobby members, frame watchdog and Gold's existing camera fix
change. Every other archive payload is checked.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / 'lan')]
from bba import Archive
from profile_interface import decoded_view as gold_decode
import graphics_assets

spec = importlib.util.spec_from_file_location('pair_history_interface',
    HERE.parent / 'multiplayer-resume/interface_patch.py')
history = importlib.util.module_from_spec(spec)
spec.loader.exec_module(history)

PINS = {
    'gold': '183aae77273ae9a89929fc459ee3465dedb372e34c6e1e32e653b948557054c5',
    'steam': 'bb536da72b54138c3a14229734f5873284a1f7c3ea7b6b5a7d1dee9074d563c8',
    'ubisoft': '9bf48a7f08632edbc88311b0b7f696b1d2cb4c0f3958686fd08f5df00852226b',
}
MAIN = 'Script/local/menu/panel/menu/control/mainState.lua'
HOST = 'Script/local/menu/panel/menu/play/multiplayer/hostLobby.lua'
GUEST = 'Script/local/menu/panel/menu/play/multiplayer/joinLobby.lua'
CORE = 'Script/local/core.lua'
DRIVER = HERE / 'native_pair_driver.lua'
WATCHDOG = HERE / 'pair_watchdog.lua'
FRAME_LOOP = HERE / 'pair_frame_loop.lua'


def insert(raw, decode, anchor, fragment, after=False):
    source, body, offsets = decode(raw)
    if body.count(anchor) != 1:
        raise ValueError('Expected one automation anchor')
    position = body.index(anchor) + (len(anchor) if after else 0)
    offset = offsets[position]
    return (source[:offset] + fragment + source[offset:]).encode('utf-8')


def build(raw, edition, duration=180000):
    if edition not in PINS or hashlib.sha256(raw).hexdigest() != PINS[edition]:
        raise ValueError('Unrecognized automation input archive')
    if not isinstance(duration, int) or not 15000 <= duration <= 600000:
        raise ValueError('Automation deadline outside 15–600 seconds')
    decode = gold_decode if edition == 'gold' else history.decoded_view
    archive = Archive(raw)
    gold = 'true' if edition == 'gold' else 'false'
    driver = DRIVER.read_text(encoding='utf-8')
    main = '''
if not l.weplayPairDriver then
 local function page(name) return e.require("local.menu.panel.menu."..name) end
 local pages={Main=page("sectionMenus.mainMenu"),Profile=page("customize.profileManager"),
  Browser=page("play.multiplayer.sessionList"),Host=page("play.multiplayer.hostLobby"),
  Guest=page("play.multiplayer.joinLobby")}
 local kinds={toMainMenu=4,toSessionList=105,toHostLobby=108}
 local function transition(action)
  local callbacks=l.actionManager.createCallbacks(l.currentPageModule)
  if action.actionType==kinds.toMainMenu then callbacks.toMainMenu(action.data)
  elseif action.actionType==kinds.toSessionList then callbacks.toSessionList(action.data)
  elseif action.actionType==kinds.toHostLobby then callbacks.toHostLobby(action.data)
  else _G.error("Unknown pair automation action") end
 end
 l.weplayPairDriver=(function(...)
''' + driver + '''
 end)(e,l,pages,transition,kinds,{gold=''' + gold + ',host=' + gold + ',duration=' + str(duration) + '''},
  function(phase)
   e.UI.WriteToConsole("WEPLAY_PAIR_OBSERVATION_V1 driver_phase="..phase)
   if phase:sub(1,8)=="waiting-" then
    e.UI.WriteToConsole("WEPLAY_PAIR_OBSERVATION_V1 waiting="..phase)
   else e.UI.WriteToConsole("Attempt to crossplay-native-driver:"..phase) end
  end)
 e.WePlayRegisterPairDriver(l.weplayPairDriver.tick)
end
'''
    host = '''
function d.weplayCanAutoStart()
 return e.frame~=nil and e.startButton~=nil and n.Network.SessionIsInSession()
  and n.Network.IsHost() and e.otherHumanInSession==true
  and e.weHaveDifferentTeamsInHost==true and e.allHumansAreReady==true
  and n.ui.isEnabled(e.startButton)
end
function d.weplayAutoStart()
 if d.weplayCanAutoStart() then n.button.activate(e.startButton) end
end
'''
    guest = '''
function c.weplayCanAutoReady()
 if not o.frame or not o.slots or not l.Network.SessionIsInSession()
  or l.Network.IsHost() or l.Network.GetLocalPlayerSlot()~=2
  or l.Network.GetPlayerSlotReadyState(2)~=false then return false end
 local slot=o.slots[2]
 return slot~=nil and slot.isLocalPlayerSlot==true and slot.readyButtonID~=nil
  and l.ui.isEnabled(slot.readyButtonID)
end
function c.weplayAutoReady()
 if c.weplayCanAutoReady() then l.button.activate(o.slots[2].readyButtonID) end
end
'''
    frame = '''
local weplayPairWatchdog=(function(...)
''' + WATCHDOG.read_text(encoding='utf-8') + '''
end)(e,{duration=''' + str(duration) + '''},function(row)
   local line="WEPLAY_PAIR_OBSERVATION_V1 edition=''' + edition + ''' sample=".._G.tostring(row.sample)
    .." elapsed_ms=".._G.tostring(row.elapsed_ms).." in_map=".._G.tostring(row.in_map)
    .." turn=".._G.tostring(row.turn).." game_time_ms=".._G.tostring(row.game_time_ms)
   e.UI.WriteToLog(line.."\\n")
   e.UI.WriteToConsole(line)
end)
local weplayPairFrame=(function(...)
''' + FRAME_LOOP.read_text(encoding='utf-8') + '''
end)(update,weplayPairWatchdog)
function WePlayRegisterPairDriver(tick) weplayPairFrame.register(tick) end
function update() weplayPairFrame.update() end
'''
    core = insert(archive.read(CORE), decode, 'function updateTurn()', frame)
    changes = {
        MAIN: insert(archive.read(MAIN), decode, 'l["moduleManager"]["update"]()', main, after=True),
        HOST: insert(archive.read(HOST), decode, 'return d\n', host),
        GUEST: insert(archive.read(GUEST), decode, 'return c\n', guest),
        CORE: core,
    }
    if edition == 'gold':
        # Keep the already identified cumulative-turn camera correction in the
        # opt-in laboratory archive as well as the normal Gold build path.
        changes[graphics_assets.FOCALIZER] = graphics_assets.focalizer_script(
            archive.read(graphics_assets.FOCALIZER))
    patched = archive.replace(changes)
    verified = Archive(patched)
    if set(archive.entries) != set(verified.entries):
        raise ValueError('Archive member set changed')
    for name, entry in archive.entries.items():
        if entry.values[4] != 256 and verified.read(name) != changes.get(name, archive.read(name)):
            raise ValueError('Unexpected archive payload change')
    return patched, {
        'edition': edition, 'source_sha256': PINS[edition],
        'candidate_sha256': hashlib.sha256(patched).hexdigest(),
        'driver_sha256': hashlib.sha256(DRIVER.read_bytes()).hexdigest(),
        'watchdog_sha256': hashlib.sha256(WATCHDOG.read_bytes()).hexdigest(),
        'frame_loop_sha256': hashlib.sha256(FRAME_LOOP.read_bytes()).hexdigest(),
        'duration_ms': duration, 'changed_members': list(changes),
        'all_other_members_unchanged': True,
        'qualification': 'staged-only-not-native-runtime-proof',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edition', choices=tuple(PINS), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration-ms', type=int, default=180000)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output directory must not exist')
    data, receipt = build(args.source.read_bytes(), args.edition, args.duration_ms)
    args.output.mkdir(parents=True)
    (args.output / 'Interface.bba').write_bytes(data)
    (args.output / 'receipt.private.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
