"""Modeled Lua 5.1 command/coordinate guards; no native gameplay qualification."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND = '''
local C={turn=0,now=0,host=true,inMap=true,valid=true,preview=true,visible=true,
 player=1,scans=0,builds=0,roads=0,stops=0,frames=0,logs={}}
local G={Game={},Network={},GUI={},Data={Groups={Storehouse=7,NormalStreet=8}},
 Buildings={},GFX={},UI={},focalizer={},camera={}}
G.Game.InMapMode=function()return C.inMap end
G.Game.GetCurrentTurn=function()return C.turn end
G.Game.GetRunTime=function()return C.now end
G.Network.IsHost=function()return C.host end
G.GUI.GetCurrentPlayer=function()return C.player end
G.Buildings.GetCastleOfPlayer=function(p)return 2,3 end
G.Buildings.GetReachableStorehouse=function(c,id)assert(c==2 and id==3);return 2,4 end
G.GFX.GetEntityPosition=function(c,id)return true,id==3 and 1000 or 3000,2000,100 end
G.GUI.BuildModeSetImmediate=function(v)assert(v==false);C.normal=true end
G.Game.PlaceBuildingHelper_SetEntityGroup=function(v)C.group=v end
G.Game.PlaceBuildingHelper_SetSnapToStreetDistance=function(v)assert(v==0)end
G.Game.PlaceBuildingHelper_SetRotation=function(v)assert(v==0)end
G.UI.GetBaseCoordinatesFromWorldPosition=function(x,y,z)
 if C.builds==0 then return true,500,600 end
 return C.visible, x==1000 and 700 or 900,800
end
G.Game.PlaceBuildingHelper_SetPosition=function(x,y)assert(x==500 and y==600);C.scans=C.scans+1 end
G.Game.PlaceBuildingHelper_IsValid=function()return C.valid,C.valid and 0 or 1 end
G.Game.PlaceBuildingHelper_GetPreviewInformation=function()return C.preview,1000,2000,100 end
G.Game.PlaceBuildingHelper_Place=function()C.builds=C.builds+1;if C.reenter then C.reenter()end;return true end
G.focalizer.setLookAt=function(x,y)assert(x==2000 and y==2000);C.frames=C.frames+1 end
G.camera.immediateUpdate=function()end
G.Game.PlaceStreetHelper_StartStreet=function(g,x,y)assert(g==8 and x==700 and y==800)end
G.Game.PlaceStreetHelper_SetSnapToStreetDistance=function(v)assert(v==300)end
G.Game.PlaceStreetHelper_SetSnapToNodeDistance=function(v)assert(v==100)end
G.Game.PlaceStreetHelper_EndStreet=function(x,y)assert(x==900 and y==800);return not C.rejectRoad end
G.Game.PlaceStreetHelper_Place=function()C.roads=C.roads+1;if C.reenter then C.reenter()end;return 2 end
G.Game.PlaceStreetHelper_Stop=function()C.stops=C.stops+1 end
G.UI.WriteToConsole=function(v)C.logs[#C.logs+1]=v end
G.UI.WriteToLog=function()end
C.G=G;return C
'''


class ConstructionTests(unittest.TestCase):
    def fixture(self, host=True):
        lua = LuaRuntime(unpack_returned_tuples=True)
        c = lua.execute(BACKEND)
        c.host, c.player = host, 1 if host else 2
        tick = lua.execute((ROOT / 'tools/gold/pair_construction_probe.lua').read_text(), c.G)
        return c, tick

    def test_both_roles_reproject_both_points_after_original_camera_and_wait(self):
        for host in (True, False):
            with self.subTest(host=host):
                c, tick = self.fixture(host)
                c.turn = 30 if host else 60
                c.reenter = tick
                tick();tick()
                self.assertEqual(c.builds, 1)
                self.assertTrue(c.normal)
                self.assertEqual(c.group, 0)
                c.turn = 90 if host else 120
                tick();tick()
                self.assertEqual((c.frames, c.roads), (1, 0))
                c.now = 249;tick()
                self.assertEqual(c.roads, 0)
                c.now = 250;tick();tick()
                self.assertEqual((c.builds, c.roads, c.stops), (1, 1, 1))

    def test_wrong_player_never_submits_commands(self):
        c, tick = self.fixture();c.player = 2;c.turn = 200
        tick();tick()
        self.assertEqual((c.builds, c.roads, c.scans), (0, 0, 0))

    def test_turn_and_map_gate(self):
        c, tick = self.fixture();tick()
        self.assertEqual(c.scans, 0)
        c.turn = 200;c.inMap = False;tick()
        self.assertEqual(c.scans, 0)

    def test_failed_placement_search_stops_at_128_and_clears_helper(self):
        c, tick = self.fixture();c.turn = 200;c.valid = False
        for _ in range(100):tick()
        self.assertEqual((c.scans, c.builds, c.roads, c.group), (128, 0, 0, 0))

    def test_missing_preview_never_uses_old_screen_point(self):
        c, tick = self.fixture();c.turn = 200;c.preview = False
        tick();tick();c.now = 1000;tick()
        self.assertEqual((c.builds, c.frames, c.roads), (1, 0, 0))

    def test_offscreen_after_frame_never_submits_road(self):
        c, tick = self.fixture();c.turn = 200
        tick();tick();c.now = 1000;c.visible = False;tick();tick()
        self.assertEqual((c.frames, c.roads, c.stops), (1, 0, 0))

    def test_native_invalid_road_preview_is_not_overridden(self):
        c, tick = self.fixture();c.turn = 200;c.rejectRoad = True
        tick();tick();c.now = 1000;tick();tick()
        self.assertEqual((c.builds, c.roads, c.stops), (1, 0, 1))


if __name__ == '__main__':
    unittest.main()
