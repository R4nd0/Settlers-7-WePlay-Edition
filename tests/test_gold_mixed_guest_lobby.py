"""Guest presentation with authored native queries and original callbacks."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime=None


@unittest.skipUnless(LuaRuntime,'Lua 5.1 test runtime is required')
class MixedGuestLobbyTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.globals().source=(ROOT/'tools/gold/mixed_guest_lobby.lua').read_text('utf-8')
        self.lua.execute('''
function fixture()
 local s={mode='guest',phase='setup-bound',flags='0',template='',session=true,host=false,slot=2,opened=0,queries={}}
 local G={UI={WriteToConsole=function(command)
  s.queries[#s.queries+1]=command
  local name=command:sub(#'weplay-crossplay-v1:'+1)
  if name=='mode' then return s.mode end
  if name=='phase' then return s.phase end
  if name=='applied-flags' then return s.flags end
  if name=='applied-template' then return s.template end
  error('Unexpected query, including any bootstrap fallback: '..command)
 end},Network={SessionIsInSession=function()return s.session end,
  IsHost=function()return s.host end,GetLocalPlayerSlot=function()return s.slot end}}
 local S={currentPageModule={}}
 S.actionManager={createCallbacks=function(owner)
  assert(owner==S.currentPageModule)
  return {toJoinLobby=function(settings)s.opened=s.opened+1;s.settings=settings end}
 end}
 local open=assert(loadstring(source))(G,S)
 return s,G,S,open
end
''')

    def test_only_stable_applied_flags_reach_the_original_page_once(self):
        self.lua.execute('''
_G.pcall=function()error('Protected engine calls are forbidden')end
for bits=0,7 do
 local s,g,u,open=fixture();s.flags=tostring(bits);s.template=bits==0 and '' or 'Actual Host Template'
 assert(open()==true and s.opened==1)
 assert(s.settings.fixedPositions==(bits%2==1))
 assert(s.settings.fixedTeams==(math.floor(bits/2)%2==1))
 local count=0;for _ in pairs(s.settings)do count=count+1 end;assert(count==2)
 for i=1,10 do assert(open()==false)end
 assert(s.opened==1)
end
''')

    def test_missing_or_malformed_applied_data_never_uses_bootstrap_defaults(self):
        self.lua.execute('''
for _,bad in ipairs({false,7,true,'','00','8','-1','7extra'})do
 local s,g,u,open=fixture();s.flags=bad
 assert(not open() and s.opened==0)
 s.flags='7';assert(open() and s.opened==1)
end
for _,bad in ipairs({false,17,true,string.rep('a',256),'nul\\0byte','line\\nfeed'})do
 local s,g,u,open=fixture();s.template=bad
 assert(not open() and s.opened==0)
end
''')

    def test_roster_and_wrong_role_or_closed_native_state_cannot_present_setup(self):
        self.lua.execute('''
for key,values in pairs({mode={'solo','host',false},phase={'pending','roster-bound','closed','failed',false},
 session={false,1,'true'},host={true,0,'false'},slot={0,1,3,true,'2'}})do
 for _,value in ipairs(values)do
  local s,g,u,open=fixture();s[key]=value
  assert(not open() and s.opened==0)
 end
end
''')

    def test_yielded_query_reentry_and_changed_applied_values_cannot_open_stale_setup(self):
        self.lua.execute('''
local s,g,u,open=fixture();local original=g.UI.WriteToConsole
local reads=0
g.UI.WriteToConsole=function(command)
 if command=='weplay-crossplay-v1:applied-flags' then
  reads=reads+1
  if reads==1 then
   local value=s.flags;assert(not open());coroutine.yield('native-applied-query');return value
  end
 end
 return original(command)
end
local job=coroutine.create(open)
local okay,value=coroutine.resume(job);assert(okay and value=='native-applied-query')
assert(not open() and s.opened==0)
s.flags='7'
okay,value=coroutine.resume(job);assert(okay and value==false and s.opened==0)
assert(open() and s.opened==1 and s.settings.fixedPositions and s.settings.fixedTeams)
''')

    def test_authority_loss_during_yield_and_yielding_original_callback_remain_closed_or_once(self):
        self.lua.execute('''
local s,g,u,open=fixture();local original=g.UI.WriteToConsole
g.UI.WriteToConsole=function(command)
 if command=='weplay-crossplay-v1:applied-template' then coroutine.yield('native-template')end
 return original(command)
end
local job=coroutine.create(open);assert(coroutine.resume(job));s.phase='closed'
while coroutine.status(job)~='dead' do assert(coroutine.resume(job))end
assert(s.opened==0)
s,g,u,open=fixture()
u.actionManager.createCallbacks=function()return {toJoinLobby=function(settings)
 s.opened=s.opened+1;assert(not open());coroutine.yield('original-page');assert(not open())
end}end
job=coroutine.create(open)
local okay,value=coroutine.resume(job);assert(okay and value=='original-page')
assert(not open() and s.opened==1)
okay,value=coroutine.resume(job);assert(okay and value==true)
assert(not open() and s.opened==1)
''')


if __name__=='__main__':unittest.main()
