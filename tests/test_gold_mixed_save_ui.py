"""Original Gold Save/Load menu branches with authored live-world conditions."""
from pathlib import Path
import sys,unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/gold'))
import mixed_save_interface as patch
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime=None


@unittest.skipUnless(LuaRuntime,'Lua 5.1 test runtime is required')
class MixedSaveMenuTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.globals().source=(ROOT/'tools/gold/mixed_save_menu.lua').read_text(encoding='utf-8')
        self.lua.globals().branch=patch.MIXED
        self.lua.execute('''
function fixture()
 local s={mode="host",gameMode=2,network=true,session=true,host=true,slot=1,inMap=true,
  game={started=true,isDisconnected=false},queries=0}
 local G={game=s.game,UI={WriteToConsole=function(command)
  assert(command=="weplay-crossplay-v1:mode");s.queries=s.queries+1;return s.mode end},
 Network={GetGameMode=function()return s.gameMode end,InNetworkMode=function()return s.network end,
  SessionIsInSession=function()return s.session end,IsHost=function()return s.host end,
  GetLocalPlayerSlot=function()return s.slot end},Game={InMapMode=function()return s.inMap end}}
 local allowed=assert(loadstring(source))(G)
 local menu=assert(loadstring('return function(e,weplayCanSaveMixed) '
  ..'local i,l,u=nil,0,false;local calls={};local t={actions={save=17,load=18}};'
  ..'local function o(i,l,label,action) calls[#calls+1]={label,action};return nil,l+1 end;'
  ..branch..';return calls end'))()
 return s,G,allowed,function()return menu(G,allowed)end
end
''')

    def test_only_a_live_original_mixed_gold_host_gets_save_without_load(self):
        self.lua.execute('''
_G.pcall=nil
local s,G,allowed,menu=fixture();assert(allowed())
local calls=menu();assert(#calls==1 and calls[1][1]=='SAVE GAME' and calls[1][2]==17)
for field,value in pairs({mode='guest',gameMode=3,network=false,session=false,host=false,slot=2,inMap=false})do
 local before=s[field];s[field]=value;assert(not allowed());assert(#menu()==0);s[field]=before
end
for _,field in ipairs({'started','isDisconnected','exitTriggerTime','lastDesyncTurn'})do
 local before=s.game[field]
 if field=='started' then s.game[field]=false else s.game[field]=true end
 assert(not allowed() and #menu()==0);s.game[field]=before
end
s.mode='solo';assert(not allowed() and #menu()==0)
G.UI.WriteToConsole=nil
assert(not allowed() and #menu()==0)
''')

    def test_original_solo_branch_keeps_both_callbacks_without_mixed_queries(self):
        self.lua.execute('''
local s,G,allowed,menu=fixture();s.mode='solo';s.gameMode=1
local calls=menu()
assert(#calls==2 and calls[1][1]=='SAVE GAME' and calls[1][2]==17)
assert(calls[2][1]=='LOAD GAME' and calls[2][2]==18 and s.queries==0)
''')


if __name__=='__main__':unittest.main()
