"""Execute owned Lua5.1 logic against an explicit native-API test double.

These test metadata/guards/lifecycle only, not native saved-world transport.
"""
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
DEV_RUNTIME = HERE.parents[1] / "work/lua-test-runtime"
if DEV_RUNTIME.is_dir():
    sys.path.insert(0, str(DEV_RUNTIME))
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None

BACKEND = r'''
local C={host=true,session=true,network=true,mode=2,mutations=0,stores=0,writes=0,cache={},saved={},players={},settings={}}
C.map="Maps\\Skirmish\\s02_m05_Twin_Peaks"
for i=1,4 do C.players[i]={state=i<3 and 11 or 22,team=i<3 and 1 or 2,
    position=i,color=i,profile=i==3 and 123456789 or 4294967295,difficulty=1,peaceful=false} end
local N={Services={SessionConfig={PlayerSlotState={Human=11,CPU=22,Closed=33,Open=44}}}}
function N.InNetworkMode() return C.network end
function N.IsHost() return C.host end
function N.SessionIsInSession() return C.session end
function N.GetGameMode() return C.mode end
function N.GetLocalPlayerSlot() return C.host and 1 or 2 end
function N.GetNbPlayerSlots() return 4 end
function N.GetMapForgeTemplateName() return "" end
function N.GetIsCoop() return false end
function N.GetPlayerSlotState(i) return C.players[i].state end
function N.GetTeamID(i) return C.players[i].team end
function N.GetStartPosition(i) return C.players[i].position end
function N.GetPlayerColor(i) return C.players[i].color end
local function set(i,key,v) C.mutations=C.mutations+1;C.players[i][key]=v end
function N.SetPlayerSlotState(i,v) set(i,"state",v) end
function N.SetAIPlayerProfileID(i,v) set(i,"profile",v) end
function N.SetTeamID(i,v) set(i,"team",v) end
function N.SetStartPosition(i,v) set(i,"position",v) end
function N.SetPlayerColor(i,v) set(i,"color",v) end
local A={}
function A.GetActivePlayers() return {1,2,3,4} end
function A.GetCurrentMapData() return {FilePath=C.map} end
function A.GetMapPathBySavegame() return C.map end
function A.GetProfileIDByPlayerID(i) return C.players[i].profile end
function A.GetAIDifficultyLevelByPlayerID(i) return C.players[i].difficulty end
function A.GetAIPeacefulFlagByPlayerID(i) return C.players[i].peaceful end
function A.SetCoopMode(v) C.coop=v;C.mutations=C.mutations+1 end
function A.SetProfileID(i,v) set(i,"profile",v) end
function A.SetAIDifficultyLevelByPlayerID(i,v) set(i,"difficulty",v) end
function A.SetAIPeacefulFlagByPlayerID(i,v) set(i,"peaceful",v) end
local Game={}
function Game.GetCurrentMapData() return {FilePath=C.map} end
function Game.CacheSaveGameValue(k,v) C.cache[k]=v;C.writes=C.writes+1 end
function Game.IsSaveGameCacheKey(k) return C.cache[k]~=nil end
function Game.GetCachedSaveGameValue(k) return C.cache[k] end
function Game.SaveGameExists(slot) return C.saved[slot]~=nil end
function Game.GetSaveGames(slot,count) return C.saved[slot] and {C.saved[slot]} or {} end
C.G={Network=N,MapSetup=A,Game=Game}
function C.store() C.stores=C.stores+1 end
function C.finishSave(slot,name,date) C.saved[slot]={slot=slot,name=name,date=date,map="Twin Peaks"} end
return C
'''


@unittest.skipIf(LuaRuntime is None, "Lua5.1 execution requires optional lupa.lua51")
class ResumeRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.context = self.lua.execute(BACKEND)
        self.source = (HERE / "runtime.lua").read_text(encoding="utf-8")
        self.runtime = self.load()

    def load(self):
        return self.lua.execute(self.source, self.context.G, self.context.settings, self.context.store)

    def saved(self):
        self.assertTrue(self.runtime.rememberLobby())
        self.assertTrue(self.runtime.cacheSave(1009, "WePlay 2v2"))
        self.context.finishSave(1009, "WePlay 2v2", "2026-11-02 14:07:14")
        self.assertTrue(self.runtime.saved(1009, "WePlay 2v2"))

    def test_round_trip_preserves_actual_roles_profiles_and_teams(self):
        record = self.runtime.captureLobby()
        result = self.runtime.decode(self.runtime.encode(record))
        self.assertEqual([result.players[i].role for i in range(1, 5)], [1, 1, 2, 2])
        self.assertEqual([result.players[i].team for i in range(1, 5)], [1, 1, 2, 2])
        self.assertEqual(result.players[3].profile, 123456789)
        self.assertEqual(result.players[4].profile, 4294967295)

    def test_variant_name_survives_native_cache_profile_index_and_module_reload(self):
        self.context.G.Network.GetMapForgeTemplateName = self.lua.eval('function() return "Mesa +3" end')
        self.saved()
        self.assertEqual(self.runtime.lookup(1009).template, 'Mesa +3')
        self.runtime = self.load()
        self.assertEqual(self.runtime.lookup(1009).template, 'Mesa +3')
        self.assertEqual(self.runtime.loadedRoster().template, 'Mesa +3')

    def test_bad_metadata_is_rejected_without_executing_lua(self):
        encoded = self.runtime.encode(self.runtime.captureLobby())
        bad = [encoded.replace("WPMPR1", "WPMPR2"), encoded + "\n",
               encoded[:50], encoded.replace("4294967295", "4294967296"),
               encoded.replace("123456789", "01"), encoded.replace("123456789", "1e3"),
               encoded.replace("123456789", "-1"), "return os.execute('unexpected')", "x" * 4097]
        for value in bad:
            with self.subTest(value=value[:50]):
                self.assertIsNone(self.runtime.decode(value))
        self.assertEqual(self.context.mutations, 0)
        self.assertEqual(self.context.writes, 0)

    def test_duplicate_start_positions_colors_and_nonhuman_host_rejected(self):
        for key, value in [("position", 1), ("color", 1), ("role", 0)]:
            record = self.runtime.captureLobby()
            record.players[1 if key == "role" else 2][key] = value
            self.assertIsNone(self.runtime.encode(record))

    def test_paths_and_names_are_data_and_cannot_escape_map_namespace(self):
        for value in ["C:/Maps/a", "../Maps/a", "Maps/../a", "Maps/a/..", "Maps/./a", "Maps/a\x00b"]:
            record = self.runtime.captureLobby()
            record.map = value
            self.assertIsNone(self.runtime.encode(record))
        record = self.runtime.captureLobby()
        record.name = "Spielstand – Grüße, | = {}"
        self.assertEqual(self.runtime.decode(self.runtime.encode(record)).name, record.name)

    def test_guest_and_reserved_native_slots_cannot_write_resume_data(self):
        self.assertTrue(self.runtime.rememberLobby())
        for slot in [30, 1000, 1010, 1002.5]:
            self.assertFalse(self.runtime.cacheSave(slot, "Test"))
        self.context.host = False
        self.assertFalse(self.runtime.cacheSave(1009, "Test"))
        self.assertIsNone(self.runtime.captureLobby())
        self.assertEqual(self.context.writes, 0)

    def test_autosave_slot_preserves_resume_metadata_after_reload_and_overwrite(self):
        self.assertTrue(self.runtime.rememberLobby())
        for date in ("2026-11-02 22:00:00", "2026-11-02 22:05:00"):
            self.assertTrue(self.runtime.cacheSave(1001, "Autosave"))
            self.context.finishSave(1001, "Autosave", date)
            self.assertTrue(self.runtime.saved(1001, "Autosave"))
            loaded = self.load().lookup(1001)
            self.assertEqual([loaded.players[i].role for i in range(1, 5)], [1, 1, 2, 2])

    def test_native_unnamed_autosave_embeds_and_indexes_roster_but_manual_name_required(self):
        self.assertTrue(self.runtime.rememberLobby())
        self.assertTrue(self.runtime.cacheSave(1001, ""))
        self.assertEqual(self.runtime.decode(self.context.cache[self.runtime.cacheKey]).name, "")
        self.context.finishSave(1001, "", "2026-11-03 00:29:01")
        self.assertTrue(self.runtime.saved(1001, ""))
        self.assertEqual(self.load().lookup(1001).name, "")
        self.assertFalse(self.runtime.cacheSave(1008, ""))

    def test_native_nil_autosave_name_is_normalized_and_manual_nil_rejected(self):
        self.assertTrue(self.runtime.rememberLobby())
        self.assertTrue(self.runtime.cacheSave(1001, None))
        self.assertEqual(self.runtime.decode(self.context.cache[self.runtime.cacheKey]).name, "")
        self.context.finishSave(1001, None, "2026-11-03 00:34:01")
        self.assertTrue(self.runtime.saved(1001, None))
        self.assertEqual(self.load().lookup(1001).name, "")
        self.assertFalse(self.runtime.cacheSave(1008, None))
        self.context.finishSave(1008, None, "2026-11-03 00:34:01")
        self.assertFalse(self.runtime.saved(1008, None))

    def test_host_autosave_five_minute_boundary_and_no_catchup_burst(self):
        calls = []
        save = self.lua.eval("function(cb) return function() cb() end end")(lambda: calls.append(1))
        tick = self.runtime.tickHostAutosave
        self.assertFalse(tick(900000, True, save))
        self.assertFalse(tick(1199999, True, save))
        self.assertTrue(tick(1200000, True, save))
        self.assertFalse(tick(1200000, True, save))
        self.assertTrue(tick(2500000, True, save))
        self.assertFalse(tick(2500001, True, save))
        self.assertEqual(len(calls), 2)

    def test_autosave_host_session_eligibility_and_clock_lifecycle(self):
        save = self.lua.eval("function() end")
        tick = self.runtime.tickHostAutosave
        for field in ("host", "session"):
            tick(0, True, save)
            self.context[field] = False
            self.assertFalse(tick(300000, True, save))
            self.context[field] = True
            self.assertFalse(tick(300001, True, save))
            self.runtime.resetAutosave()
        tick(0, True, save)
        self.assertFalse(tick(300000, False, save))
        self.assertFalse(tick(300001, True, save))
        self.assertFalse(tick(1, True, save))
        self.assertFalse(tick(300000, True, save))
        self.assertTrue(tick(300001, True, save))
        self.runtime.resetAutosave()
        self.assertFalse(tick(900000, True, save))

    def test_autosave_failure_does_not_retry_every_frame_or_hide_error(self):
        tick = self.runtime.tickHostAutosave
        fail = self.lua.eval("function() error('native save failure') end")
        tick(0, True, fail)
        with self.assertRaisesRegex(Exception, "native save failure"):
            tick(300000, True, fail)
        self.assertFalse(tick(300001, True, fail))

    def test_manual_save_does_not_postpone_autosave(self):
        tick = self.runtime.tickHostAutosave
        save = self.lua.eval("function() end")
        tick(0, True, save)
        self.saved()
        self.assertTrue(tick(300000, True, save))

    def test_host_autosave_never_runs_in_singleplayer_or_without_network(self):
        tick = self.runtime.tickHostAutosave
        fail = self.lua.eval("function() error('must not save') end")
        for field, value in (("network", False), ("mode", 1), ("mode", 0)):
            original = self.context[field]
            self.context[field] = value
            self.assertFalse(tick(0, True, fail))
            self.assertFalse(tick(600000, True, fail))
            self.context[field] = original

    def test_invalid_clock_cannot_trigger_or_poison_autosave(self):
        tick = self.runtime.tickHostAutosave
        save = self.lua.eval("function() end")
        for value in (None, -1, float("inf"), float("nan"), 4294967296):
            self.assertFalse(tick(value, True, save))
            self.assertFalse(tick(300000, True, save))
            self.runtime.resetAutosave()

    def test_native_fractional_frame_clock_does_not_reset_five_minute_timer(self):
        tick = self.runtime.tickHostAutosave
        save = self.lua.eval("function() end")
        self.assertFalse(tick(0.25, True, save))
        for now in (100.125, 1999.75, 12345.875, 299999.99):
            self.assertFalse(tick(now, True, save))
        self.assertTrue(tick(300000.125, True, save))
        self.assertFalse(tick(599999.99, True, save))
        self.assertTrue(tick(600000.75, True, save))

    def test_profile_index_survives_module_reload_and_rejects_replaced_save(self):
        self.saved()
        reloaded = self.load()
        self.assertIsNotNone(reloaded.lookup(1009))
        self.context.finishSave(1009, "WePlay 2v2", "2026-11-02 14:09:00")
        self.assertIsNone(reloaded.lookup(1009))
        self.assertEqual(self.context.mutations, 0)

    def test_embedded_roster_is_bound_to_the_world_and_save_name(self):
        self.assertTrue(self.runtime.rememberLobby())
        self.context.map = "Maps/Skirmish/another_map"
        self.assertFalse(self.runtime.cacheSave(1009, "Other world"))
        self.assertEqual(self.context.writes, 0)
        self.context.map = "Maps/Skirmish/s02_m05_Twin_Peaks"
        self.assertTrue(self.runtime.cacheSave(1009, "Named checkpoint"))
        encoded = self.context.cache[self.runtime.cacheKey]
        self.assertEqual(self.runtime.decode(encoded).name, "Named checkpoint")

    def test_restore_preserves_ai_parameters_and_waits_for_all_humans(self):
        record = self.runtime.captureLobby()
        self.context.players[2].state = 44
        self.context.players[3].state = 44
        self.context.players[3].profile = 0
        self.context.players[4].difficulty = 2
        self.assertTrue(self.runtime.applyRoster(record))
        self.assertEqual(self.context.players[3].profile, record.players[3].profile)
        self.assertEqual(self.context.players[4].difficulty, 1)
        self.assertFalse(self.runtime.ready(record))
        self.context.players[2].state = 11
        self.assertTrue(self.runtime.ready(record))
        self.context.players[2].team = 4
        self.assertFalse(self.runtime.ready(record))

    def test_loaded_native_cache_is_read_only_for_both_players_and_bound_to_map(self):
        self.assertIsNone(self.runtime.loadedRoster())
        self.saved()
        for host in (True, False):
            self.context.host = host
            loaded = self.runtime.loadedRoster()
            self.assertEqual([loaded.players[i].role for i in range(1, 5)], [1, 1, 2, 2])
        self.assertEqual(self.context.mutations, 0)
        self.assertEqual(self.context.writes, 1)
        self.context.map = "Maps/Skirmish/another_map"
        self.assertIsNone(self.runtime.loadedRoster())

    def test_restore_never_replaces_a_connected_human_with_ai(self):
        record = self.runtime.captureLobby()
        self.context.players[3].state = 11
        self.assertFalse(self.runtime.applyRoster(record))
        self.assertEqual(self.context.mutations, 0)


if __name__ == "__main__":
    unittest.main()
