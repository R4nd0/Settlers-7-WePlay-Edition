"""Lua 5.1 routing/yield contracts with authored pages and engine callbacks."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/gold'))
import mixed_interface
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime=None


@unittest.skipUnless(LuaRuntime,'Lua 5.1 test runtime is required')
class MixedMenuTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.globals().source=(ROOT/'tools/gold/mixed_menu.lua').read_text(encoding='utf-8')
        self.lua.globals().prepare_source=(ROOT/'tools/gold/mixed_guest_prepare.lua').read_text(encoding='utf-8')
        self.lua.execute('''
function fixture(mode)
 local state={mode=mode,phase="pending",map=false,flags="0",template="",login=true,inSession=false,
  isHost=false,slot=0,opened=0,prepared=0,guest=0,browser=0,queries={}}
 local pages={Main={},Browser={},Creation={}}
 for _,page in pairs(pages)do page.isCreated=function()return true end end
 local ui={currentPageModule=pages.Main}
 local game={UI={WriteToConsole=function(command)
  state.queries[#state.queries+1]=command
  if command=="weplay-crossplay-v1:mode" then return state.mode end
  if command=="weplay-crossplay-v1:phase" then return state.phase end
  if command=="weplay-crossplay-v1:bootstrap-map" then return state.map end
  if command=="weplay-crossplay-v1:bootstrap-flags" then return state.flags end
  if command=="weplay-crossplay-v1:bootstrap-template" then return state.template end
 end},Network={IsLoggedIn=function()return state.login end,
  SessionIsInSession=function()return state.inSession end,
  IsHost=function()return state.isHost end,GetLocalPlayerSlot=function()return state.slot end}}
 local actions={open_browser=function()state.browser=state.browser+1 end,
  can_open_host_creation=function()return state.discovery==true end,
  open_host_creation=function()state.opened=state.opened+1 end,
  prepare_guest_map=function(map,choices)state.prepared=state.prepared+1;state.actualMap=map;state.choices=choices;return true end,
  open_guest_lobby=function(choices)state.guest=state.guest+1;state.lobbyChoices=choices end}
 local driver=assert(loadstring(source))(game,ui,pages,actions)
 return state,ui,pages,actions,driver,game
end
''')

    def test_ordinary_or_legacy_mode_does_not_route_or_query_phase(self):
        self.lua.execute('''
for _,mode in ipairs({"solo","unknown",false})do
 local s,u,p,a,d=fixture(mode)
 for i=1,10 do d.tick()end
 assert(not d.active() and d.state()=="inactive" and #s.queries==1)
 assert(s.browser==0 and s.opened==0 and s.prepared==0 and s.guest==0)
end
local s,u,p,a,d=fixture(nil);d.tick();assert(not d.active() and #s.queries==1)
''')

    def test_host_waits_for_login_and_original_browser_before_one_dialog_request(self):
        self.lua.execute('''
local s,u,p,a,d=fixture("host")
s.login=false;d.tick();assert(s.browser==0)
s.login=true;d.tick();d.tick();assert(s.browser==1)
u.currentPageModule=p.Browser;d.tick();assert(s.opened==0 and d.state()~="host-creation-open")
s.discovery=true;d.tick();d.tick();assert(s.opened==1 and d.state()=="host-creation-open")
assert(s.prepared==0 and s.guest==0)
''')

    def test_guest_uses_only_authenticated_map_and_waits_for_native_roster_role(self):
        self.lua.execute('''
local s,u,p,a,d=fixture("guest")
d.tick();assert(s.prepared==0 and d.state()=="waiting-host-context")
s.map="maps/original-host-choice.map";d.tick();d.tick()
assert(s.prepared==1 and s.actualMap==s.map and s.guest==0)
s.phase="roster-bound";s.inSession=true;s.slot=2;s.isHost=true;d.tick();assert(s.guest==0)
s.isHost=false;s.slot=1;d.tick();assert(s.guest==0)
s.slot=2;d.tick();assert(s.guest==0)
s.phase="setup-bound";d.tick();d.tick();assert(s.guest==1 and s.prepared==1)
assert(s.browser==0 and s.opened==0)
''')

    def test_guest_waits_for_original_profile_and_main_menu_before_preparation(self):
        self.lua.execute('''
local s,u,p,a,d=fixture("guest");s.map="maps/actual.map"
u.currentPageModule={isCreated=function()return true end}
d.tick();assert(s.prepared==0 and d.state()=="waiting-menu")
u.currentPageModule=p.Main;d.tick();assert(s.prepared==1)
''')

    def test_native_preparation_can_yield_and_reenter_without_duplicate_calls(self):
        self.lua.execute('''
local s,u,p,a,d=fixture("guest");s.map="maps/authenticated.map"
a.prepare_guest_map=function(map)
 s.prepared=s.prepared+1;d.tick();assert(s.prepared==1)
 coroutine.yield("original-job")
 d.tick();return true
end
local job=coroutine.create(d.tick)
local okay,value=coroutine.resume(job);assert(okay and value=="original-job")
d.tick();assert(s.prepared==1 and coroutine.status(job)=="suspended")
assert(coroutine.resume(job));assert(d.state()=="guest-prepared" and s.prepared==1)
''')

    def test_original_history_prepare_jobs_keep_exact_arity_flags_and_yielding(self):
        self.lua.execute('''
local s,u,p,a,d,g=fixture("guest");s.map="maps/actual.map";s.flags="7";s.template="Host Template"
local loads,creates=0,0
g.map={loadSetupMapConfig=function(map,template)
 assert(map==s.map and template==s.template);loads=loads+1;d.tick();coroutine.yield("load")
end}
g.MapSetup={IsValid=function()return true end}
g.Network.SessionCreate=function(...)
 local args={n=select("#",...),...};creates=creates+1
 assert(args.n==8 and args[1]==s.map and args[2]==false and args[3]==false and args[4]==2)
 assert(args[5]==true and args[6]==true and args[7]==s.template and args[8]==true)
 d.tick();coroutine.yield("create");return true
end
a.prepare_guest_map=assert(loadstring(prepare_source))(g)
local job=coroutine.create(d.tick)
local okay,value=coroutine.resume(job);assert(okay and value=="load")
d.tick();assert(loads==1 and creates==0)
okay,value=coroutine.resume(job);assert(okay and value=="create")
d.tick();assert(loads==1 and creates==1)
assert(coroutine.resume(job));assert(d.state()=="guest-prepared")
''')

    def test_original_history_prepare_rejects_invalid_loaded_map_without_create(self):
        self.lua.execute('''
local s,u,p,a,d,g=fixture("guest");s.map="maps/actual.map"
local loads,creates=0,0
g.map={loadSetupMapConfig=function()loads=loads+1 end}
g.MapSetup={IsValid=function()return false end}
g.Network.SessionCreate=function()creates=creates+1;return true end
a.prepare_guest_map=assert(loadstring(prepare_source))(g)
d.tick();assert(loads==1 and creates==0 and d.state()=="failed")
''')

    def test_guest_preserves_all_actual_host_flags_and_template_until_setup_bound(self):
        self.lua.execute('''
for bits=0,7 do
 local s,u,p,a,d=fixture("guest");s.map="maps/actual.map";s.flags=tostring(bits)
 s.template=bits==0 and "" or "Actual Host Template"
 d.tick();assert(s.prepared==1)
 assert(s.choices.fixedPositions==(bits%2==1))
 assert(s.choices.fixedTeams==(math.floor(bits/2)%2==1))
 assert(s.choices.coop==(math.floor(bits/4)%2==1))
 if bits==0 then assert(s.choices.template==nil)else assert(s.choices.template==s.template)end
 s.phase="roster-bound";s.inSession=true;s.slot=2;d.tick();assert(s.guest==0)
 s.phase="setup-bound";d.tick();assert(s.guest==1 and s.lobbyChoices==s.choices)
end
''')

    def test_missing_flags_or_template_waits_and_invalid_values_fail_before_preparation(self):
        self.lua.execute('''
for _,field in ipairs({"flags","template"})do
 local s,u,p,a,d=fixture("guest");s.map="maps/actual.map";s[field]=false
 d.tick();assert(s.prepared==0 and d.state()=="waiting-host-context")
end
for _,flags in ipairs({"", "8", "00", "-1", "7extra", 7, true})do
 local s,u,p,a,d=fixture("guest");s.map="maps/actual.map";s.flags=flags
 d.tick();assert(s.prepared==0 and d.state()=="failed")
end
for _,template in ipairs({19, true, string.rep("a",256), "line\\nfeed", "nul\\0byte"})do
 local s,u,p,a,d=fixture("guest");s.map="maps/actual.map";s.template=template
 d.tick();assert(s.prepared==0 and d.state()=="failed")
end
''')

    def test_failed_closed_unknown_state_or_invalid_map_never_opens_lobby(self):
        self.lua.execute('''
for _,phase in ipairs({"closed","failed","unknown",false})do
 local s,u,p,a,d=fixture("guest");s.phase=phase;s.map="maps/test.map"
 d.tick();d.tick();assert(s.prepared==0 and s.guest==0)
end
for _,map in ipairs({"/absolute","C:/file","maps/../file","maps/./file","maps//file",
 "maps\\\\file","maps/file/","maps/line\\nfile",string.rep("a",256),19})do
 local s,u,p,a,d=fixture("guest");s.map=map;d.tick();d.tick()
 assert(s.prepared==0 and s.guest==0 and d.state()=="failed")
end
local s,u,p,a,d=fixture("guest");s.map="maps/test.map"
a.prepare_guest_map=function()s.prepared=s.prepared+1;return false end
d.tick();d.tick();assert(s.prepared==1 and s.guest==0 and d.state()=="failed")
''')


@unittest.skipUnless(LuaRuntime,'Lua 5.1 test runtime is required')
class MixedWrapperTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)

    def test_gold_browser_uses_only_original_completed_discovery_and_selected_filter(self):
        self.lua.globals().source=mixed_interface.fragment('mixed_browser.lua')
        self.lua.execute('''
local mode,login,session,discovery="host",true,false,{State=2}
local selected={lan=false}
local G={UI={WriteToConsole=function()return mode end},Network={
 IsLoggedIn=function()return login end,SessionIsInSession=function()return session end,
 SessionGetDiscovery=function(lan)assert(lan==false);return discovery end}}
local S={frame={},lastDiscoveryFinished=true,sessionListState=0,
 sessionsFilterList={getSelectedItemData=function()return selected end}}
local P,count,actual={},0,nil
local original=function()count=count+1;actual=selected.lan end
assert(loadstring(source))(G,P,S,{idle=0},original)
for _,lan in ipairs({false,true})do
 selected.lan=lan;assert(P.weplayCanOpenMixedCreation())
 assert(P.weplayOpenMixedCreation());assert(actual==lan)
end
assert(count==2)
for _,bad in ipairs({"solo","guest",false})do mode=bad;assert(not P.weplayOpenMixedCreation())end
mode="host";login=false;assert(not P.weplayOpenMixedCreation());login=true
session=true;assert(not P.weplayOpenMixedCreation());session=false
S.lastDiscoveryFinished=false;assert(not P.weplayOpenMixedCreation());S.lastDiscoveryFinished=true
S.sessionListState=1;assert(not P.weplayOpenMixedCreation());S.sessionListState=0
discovery={State=1};assert(not P.weplayOpenMixedCreation());discovery={State=2}
selected={};assert(not P.weplayOpenMixedCreation());selected={lan=false}
S.frame=nil;assert(not P.weplayOpenMixedCreation());assert(count==2)
''')

    def test_composed_main_fragment_routes_host_once_through_original_action_manager(self):
        self.lua.globals().source=mixed_interface.main_fragment('gold')
        self.lua.execute('''
local mode="host"
local main,browser,creation={},{},{}
for _,p in ipairs({main,browser,creation})do p.isCreated=function()return true end end
local opened,requests,discovery=0,0,false
browser.weplayCanOpenMixedCreation=function()return discovery end
browser.weplayOpenMixedCreation=function()opened=opened+1 end
local pages={['sectionMenus.mainMenu']=main,['play.multiplayer.sessionList']=browser,
 ['play.multiplayer.sessionCreation']=creation}
local e={UI={WriteToConsole=function(command)
 if command=='weplay-crossplay-v1:mode' then return mode end
 if command=='weplay-crossplay-v1:phase' then return 'pending' end end},
 require=function(path)return assert(pages[path:sub(#'local.menu.panel.menu.'+1)])end,
 Network={IsLoggedIn=function()return true end,SessionIsInSession=function()return false end}}
local l={currentPageModule=main}
l.actionManager={createCallbacks=function(owner)
 assert(owner==l.currentPageModule)
 return {toSessionList=function()requests=requests+1 end}
end}
local tick=assert(loadstring('return function(e,l) '..source..' end'))()
tick(e,l);tick(e,l);assert(requests==1 and opened==0)
local lifetime=l.weplayMixedMenu
l.currentPageModule=browser;tick(e,l);assert(opened==0)
discovery=true;tick(e,l);tick(e,l);assert(opened==1 and l.weplayMixedMenu==lifetime)
tick(e,nil)
-- Wrong edition role and ordinary launches do not resolve or operate pages.
for _,value in ipairs({'solo','guest',false})do
 mode=value;e.require=function()error('inactive UI must not resolve mixed pages')end
 local clean={};tick(e,clean);tick(e,clean)
end
''')

    def test_composed_history_fragment_can_prepare_but_cannot_present_unapplied_setup(self):
        self.lua.globals().source=mixed_interface.main_fragment('history')
        self.lua.execute('''
local main,browser,creation={},{},{}
for _,p in ipairs({main,browser,creation})do p.isCreated=function()return true end end
local pages={['sectionMenus.mainMenu']=main,['play.multiplayer.sessionList']=browser,
 ['play.multiplayer.sessionCreation']=creation}
local phase,session,loads,creates,callbacks='pending',false,0,0,0
local e={UI={WriteToConsole=function(command)
 local values={mode='guest',phase=phase,['bootstrap-map']='maps/original.map',
 ['bootstrap-flags']='5',['bootstrap-template']=''}
 return values[command:sub(#'weplay-crossplay-v1:'+1)] end},
 require=function(path)return assert(pages[path:sub(#'local.menu.panel.menu.'+1)])end,
 map={loadSetupMapConfig=function(map,template)assert(map=='maps/original.map' and template==nil);loads=loads+1 end},
 MapSetup={IsValid=function()return true end},Network={
 IsLoggedIn=function()return true end,SessionIsInSession=function()return session end,
 IsHost=function()return false end,GetLocalPlayerSlot=function()return 2 end,
 SessionCreate=function(map,private,lan,two,positions,teams,template,coop)
 assert(map=='maps/original.map' and private==false and lan==false and two==2)
 assert(positions==true and teams==false and template==nil and coop==true)
 creates=creates+1;session=true;return true end}}
local l={currentPageModule=main,actionManager={createCallbacks=function()callbacks=callbacks+1;return {}end}}
local tick=assert(loadstring('return function(e,l) '..source..' end'))()
tick(e,l);phase='roster-bound';tick(e,l);phase='setup-bound';tick(e,l);tick(e,l)
assert(loads==1 and creates==1 and callbacks==0)
''')


class MixedArchiveGuardTests(unittest.TestCase):
    def test_builder_rejects_unknown_edition_and_unpinned_archive(self):
        for edition in ('gold','history','steam','unknown',None):
            with self.assertRaises(ValueError):mixed_interface.build(b'not a game archive',edition)


if __name__=='__main__':unittest.main()
