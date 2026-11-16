"""Lua 5.1 routing/yield contracts with authored pages and engine callbacks."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
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


if __name__=='__main__':unittest.main()
