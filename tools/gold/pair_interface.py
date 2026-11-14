"""Stage opt-in, fingerprinted pair automation; never deploy to a game.

Keep the quiet Gold menu and the native History guest/map-reload integration.
Only the browser/lobby members, frame watchdog, Gold's existing camera fix and
optional loading-progress display change. Every other archive payload is checked.
"""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / 'lan')]
from bba import Archive
from profile_interface import decoded_view as gold_decode
import graphics_assets
import loading_screen
import menu_interface

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
BROWSER = 'Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'
CORE = 'Script/local/core.lua'
DRIVER = HERE / 'native_pair_driver.lua'
WATCHDOG = HERE / 'pair_watchdog.lua'
FRAME_LOOP = HERE / 'pair_frame_loop.lua'
CONSTRUCTION_PROBE = HERE / 'pair_construction_probe.lua'
PACING = HERE / 'pair_pacing.lua'
AI_ROSTER = HERE / 'pair_ai_roster.lua'
BROWSER_GUARD = '''
function a.weplayCanAutoCreate()
 return l.frame~=nil and l.lastDiscoveryFinished==true and l.sessionListState==r.idle
end
'''


def insert(raw, decode, anchor, fragment, after=False):
    source, body, offsets = decode(raw)
    if body.count(anchor) != 1:
        raise ValueError('Expected one automation anchor')
    position = body.index(anchor) + (len(anchor) if after else 0)
    offset = offsets[position]
    return (source[:offset] + fragment + source[offset:]).encode('utf-8')


def build(raw, edition, duration=180000, construction_probe=False, speed=1,
          map_token='monhaim', ai_players=0, ai_difficulty=2):
    if edition not in PINS or hashlib.sha256(raw).hexdigest() != PINS[edition]:
        raise ValueError('Unrecognized automation input archive')
    if not isinstance(duration, int) or not 15000 <= duration <= 600000:
        raise ValueError('Automation deadline outside 15–600 seconds')
    if not isinstance(construction_probe, bool):
        raise ValueError('Construction probe must be explicitly enabled or disabled')
    if type(speed) is not int or speed not in (1,20):
        raise ValueError('Laboratory speed must be 1 or 20')
    if not isinstance(map_token,str) or not re.fullmatch(r'[a-z0-9_ -]{1,48}',map_token) or not map_token.strip():
        raise ValueError('Expected a bounded lowercase laboratory map token')
    if type(ai_players) is not int or ai_players not in (0,2):
        raise ValueError('Laboratory AI roster must be disabled or contain two AIs')
    if type(ai_difficulty) is not int or ai_difficulty not in (0,1,2):
        raise ValueError('Expected original easy/normal/hard AI difficulty')
    decode = gold_decode if edition == 'gold' else history.decoded_view
    archive = Archive(raw)
    gold = 'true' if edition == 'gold' else 'false'
    browser_guard = BROWSER_GUARD
    browser_anchor = 'function a.notifyDiscoveryEnded()'
    if edition == 'gold':
        browser_guard = browser_guard.replace('function a.', 'function f.').replace('==r.idle', '==h.idle')
        browser_anchor = 'function f.notifyDiscoveryEnded()'
    driver = DRIVER.read_text(encoding='utf-8')
    options = ('local weplayPairOptions={gold='+gold+',host='+gold+',duration='+str(duration)
               +',map_token='+json.dumps(map_token)+',ai_players='+str(ai_players)+'}\n')
    if ai_players == 2 and edition == 'gold':
        options += '''
local weplayPairAI=(function(...)
''' + AI_ROSTER.read_text(encoding='utf-8') + '''
end)(e,{difficulty='''+str(ai_difficulty)+'''},function(phase)
 e.UI.WriteToConsole("WEPLAY_PAIR_OBSERVATION_V1 ai_roster="..phase)
end)
weplayPairOptions.configure_ai=weplayPairAI.tick
weplayPairOptions.ai_state=weplayPairAI.state
'''
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
''' + options + '''
 l.weplayPairDriver=(function(...)
''' + driver + '''
 end)(e,l,pages,transition,kinds,weplayPairOptions,
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
function d.weplayCanConfigureAI()
 return e.frame~=nil and n.Network.SessionIsInSession()
  and n.Network.IsHost() and e.otherHumanInSession==true
  and e.allHumansAreReady==true
end
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
    .." loading_screen_visible=".._G.tostring(row.loading_screen_visible)
   e.UI.WriteToLog(line.."\\n")
   e.UI.WriteToConsole(line)
end)
local weplayPairFrame=(function(...)
''' + FRAME_LOOP.read_text(encoding='utf-8') + '''
end)(update,weplayPairWatchdog)
function WePlayRegisterPairDriver(tick) weplayPairFrame.register(tick) end
function update() weplayPairFrame.update() end
'''
    if speed == 20:
        frame += '''
local weplayPairPacing=(function(...)
''' + PACING.read_text(encoding='utf-8') + '''
end)(e,function(row)
 e.UI.WriteToConsole("WEPLAY_PAIR_OBSERVATION_V1 pacing_requested=".._G.tostring(row.requested_speed)
  .." pacing_before_ms=".._G.tostring(row.before_ms).." pacing_actual_ms=".._G.tostring(row.actual_ms)
  .." pacing_accepted=".._G.tostring(row.accepted).." turn=".._G.tostring(row.turn))
end)
local weplayPairPacedUpdate=update
function update()
 weplayPairPacedUpdate()
 if not weplayPairWatchdog.closing() then weplayPairPacing() end
end
'''
    if construction_probe:
        frame += '''
local weplayPairConstruction=(function(...)
''' + CONSTRUCTION_PROBE.read_text(encoding='utf-8') + '''
end)(e)
local weplayPairUpdate=update
function update()
 weplayPairUpdate()
 if not weplayPairWatchdog.closing() then weplayPairConstruction() end
end
'''
    core = insert(archive.read(CORE), decode, 'function updateTurn()', frame)
    changes = {
        MAIN: insert(archive.read(MAIN), decode, 'l["moduleManager"]["update"]()', main, after=True),
        HOST: insert(archive.read(HOST), decode, 'return d\n', host),
        GUEST: insert(archive.read(GUEST), decode, 'return c\n', guest),
        BROWSER: insert(archive.read(BROWSER), decode,
            browser_anchor, browser_guard),
        CORE: core,
    }
    if edition == 'gold':
        # Keep the already identified cumulative-turn camera correction in the
        # opt-in laboratory archive as well as the normal Gold build path.
        changes[graphics_assets.FOCALIZER] = graphics_assets.focalizer_script(
            archive.read(graphics_assets.FOCALIZER))
        # The pinned lab input predates the quiet-chat lifecycle correction.
        # Gold calls hide immediately after create during startGame; retaining
        # the generic removed-page adapter aborts UI callback registration.
        quiet_chat = archive.read(menu_interface.GLOBAL_CHAT)
        if gold_decode(quiet_chat)[1] != menu_interface.QUIET_PAGE:
            raise ValueError('Unexpected legacy Gold quiet-chat adapter')
        changes[menu_interface.GLOBAL_CHAT] = menu_interface.replace_body(
            quiet_chat, menu_interface.QUIET_GLOBAL_CHAT)
        changes[loading_screen.MEMBER] = loading_screen.patch(
            archive.read(loading_screen.MEMBER))
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
        'requested_speed': speed,
        'pacing_sha256': (hashlib.sha256(PACING.read_bytes()).hexdigest() if speed==20 else None),
        'map_token': map_token, 'ai_players': ai_players, 'ai_difficulty': ai_difficulty,
        'ai_roster_sha256': (hashlib.sha256(AI_ROSTER.read_bytes()).hexdigest()
            if ai_players==2 and edition=='gold' else None),
        'construction_probe': construction_probe,
        'construction_probe_sha256': (hashlib.sha256(CONSTRUCTION_PROBE.read_bytes()).hexdigest()
            if construction_probe else None),
        'all_other_members_unchanged': True,
        'qualification': 'staged-only-not-native-runtime-proof',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edition', choices=tuple(PINS), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration-ms', type=int, default=180000)
    parser.add_argument('--construction-probe', action='store_true',
        help='Lab only: submit normal storehouse/street commands for both players')
    parser.add_argument('--speed', type=int, choices=(1,20), default=1,
        help='Lab only: request original x20 native turn pacing')
    parser.add_argument('--map-token', default='monhaim',
        help='Lab only: require one uniquely matching installed unlocked map')
    parser.add_argument('--ai-players', type=int, choices=(0,2), default=0,
        help='Lab only: prepare slots3/4 through original host AI-chooser APIs')
    parser.add_argument('--ai-difficulty', type=int, choices=(0,1,2), default=2)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output directory must not exist')
    data, receipt = build(args.source.read_bytes(), args.edition, args.duration_ms,
        args.construction_probe, args.speed,args.map_token,args.ai_players,args.ai_difficulty)
    args.output.mkdir(parents=True)
    (args.output / 'Interface.bba').write_bytes(data)
    (args.output / 'receipt.private.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
