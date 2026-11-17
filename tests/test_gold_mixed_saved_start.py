"""Original SessionStarted routing after a genuinely admitted native start."""
from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

class SavedStartTests(unittest.TestCase):
    def test_saved_start_uses_original_slot_and_fresh_solo_preserve_arguments(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.globals().source=(ROOT/'tools/gold/mixed_saved_start.lua').read_text()
        lua.execute('''
local route=assert(loadstring(source))
local state={mode='host',saved='1',calls=0}
local G={game={multiplayerSavegameSlot=1000},UI={WriteToConsole=function(command)
 state.calls=state.calls+1
 if command=='weplay-crossplay-v1:mode' then return state.mode end
 assert(command=='weplay-crossplay-v1:saved-start');return state.saved
end}}
for _,mode in ipairs({'host','guest'})do
 state.mode=mode;assert(route(G,nil)==1000 and route(G,17)==1000)
 state.saved='0';assert(route(G,nil)==nil and route(G,17)==17);state.saved='1'
end
state.mode='solo';state.calls=0;assert(route(G,17)==17 and state.calls==1)
G.UI.WriteToConsole=nil;assert(route(G,17)==17)
''')

    def test_missing_native_admission_and_missing_original_slot_fail_closed(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.globals().source=(ROOT/'tools/gold/mixed_saved_start.lua').read_text()
        lua.execute('''
local route=assert(loadstring(source))
local state={saved=false}
local G={game={multiplayerSavegameSlot=1000},UI={WriteToConsole=function(command)
 if command=='weplay-crossplay-v1:mode' then return 'guest' end
 return state.saved
end}}
for _,value in ipairs({false,true,'','pending','01'})do state.saved=value;assert(not pcall(route,G,nil)) end
state.saved='1'
for _,value in ipairs({false,'1000',-1,1.5,4294967296})do G.game.multiplayerSavegameSlot=value;assert(not pcall(route,G,nil)) end
G.game=nil;assert(not pcall(route,G,nil))
''')

if __name__=='__main__':unittest.main()
