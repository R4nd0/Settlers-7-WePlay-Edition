"""Gold menu guards, UI lifecycle and removal of vendor routes/controls."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/gold'), str(ROOT / 'work/lua-test-runtime')]
import menu_interface as menu
from lupa.lua51 import LuaRuntime


class GoldMenuTests(unittest.TestCase):
    def test_turn_counter_cannot_rotate_camera_but_actual_input_and_damping_remain(self):
        body=('local n={focalizer={rotateCamera=1,getDampenerVal=function()return 400 end}};'
              'local function frame(input) '+menu.TURN_ROTATION+' '
              'n.focalizer.rotateCamera=n.focalizer.rotateCamera+input*0.5;'
              'return n.focalizer.rotateCamera end; return frame')
        with patch.object(menu.profile,'decoded_view',return_value=(body,body,list(range(len(body)+1)))):
            corrected=menu.change(body.encode(),menu.TURN_ROTATION,'').decode()
        frame=LuaRuntime().execute(corrected)
        for _ in range(500):self.assertEqual(frame(0),1)
        self.assertEqual(frame(2),2)
        self.assertEqual(frame(-2),1)

    def test_unknown_archive_is_rejected_before_archive_parser(self):
        with patch.object(menu.profile, 'Archive') as archive:
            with self.assertRaisesRegex(ValueError, 'exact original Gold'):
                menu.build(b'unknown or already modified archive')
            archive.assert_not_called()

    def test_change_rejects_missing_and_duplicate_anchors(self):
        for body in ('missing', 'anchor anchor'):
            with patch.object(menu.profile, 'decoded_view', return_value=(body, body, list(range(len(body)+1)))):
                with self.assertRaisesRegex(ValueError, 'exactly once'):
                    menu.change(body.encode(), 'anchor', 'replacement')

    def test_sections_reject_reversed_or_ambiguous_boundaries(self):
        for body in ('end start', 'start start end', 'start missing'):
            with patch.object(menu.profile, 'decoded_view', return_value=(body, body, [])):
                with self.assertRaises(ValueError):
                    menu.section(body.encode(), 'start', 'end', '')

    def test_quiet_news_supports_reentry_and_teardown_without_feeds_or_links(self):
        lua = LuaRuntime()
        state = lua.execute('''
local state = {created=0,destroyed=0}
local fail = function() error("unexpected service or UI dependency") end
l = {desktop={frame=17},defaultPriorities={base=2},frame={
 create=function(name,parent,x,y,w,h,priority)
  assert(parent==17 and x==0 and y==0 and w==0 and h==0 and priority==2)
  state.created=state.created+1; return state.created
 end,
 destroy=function(frame) assert(frame~=nil);state.destroyed=state.destroyed+1 end}}
setmetatable(l,{__index=fail})
return state
''')
        news = lua.execute(menu.QUIET_NEWS)
        self.assertFalse(news.isCreated())
        news.destroy()
        for _ in range(3):
            news.create(); news.create()
            self.assertTrue(news.isCreated())
            self.assertIsNotNone(news.getFrame())
            for _ in range(20):
                news.update()
            news.destroy(); news.destroy()
            self.assertFalse(news.isCreated())
            self.assertIsNone(news.getFrame())
        self.assertEqual((state.created, state.destroyed), (3, 3))

    def test_hidden_status_reports_only_real_network_transitions(self):
        lua = LuaRuntime()
        state = lua.execute('''
local state={actual=false,frames=0}
e={desktop={frame=1},defaultPriorities={base=2},frame={
 create=function(...)state.frames=state.frames+1;return state.frames end,
 destroy=function()state.frames=state.frames-1 end},
 Network={IsLoggedIn=function()return state.actual end}}
setmetatable(e.Network,{__index=function()error("hidden status must not log in or out")end})
return state
''')
        status = lua.execute(menu.QUIET_STATUS)
        status.create(); status.create(); self.assertEqual(state.frames, 1)
        for actual in (False, True, False):
            state.actual = actual
            status.update(); self.assertEqual(status.show(), actual)
            status.resetShow(); self.assertIsNone(status.show())
            status.update(); self.assertIsNone(status.show())
        status.destroy(); status.destroy(); self.assertEqual(state.frames, 0)
        status.create(); status.update(); self.assertFalse(status.show())

    def test_removed_vendor_routes_preserve_native_lobby_actions_and_payload(self):
        lua = LuaRuntime()
        redirect = lua.eval('function(i) local t={actionTypes={toMainMenu=4}}; '
                            + menu.REDIRECT + ' return i end')
        for action in (10,12,101,102,103,104,106,110,111,112,1003):
            result = redirect(lua.table_from({'actionType': action, 'data': 123}))
            self.assertEqual(result.actionType, 4); self.assertEqual(result.data, 123)
        for action in (1,4,6,7,8,9,11,13,14,15,16,20,100,105,107,108,109):
            result = redirect(lua.table_from({'actionType': action, 'data': 123}))
            self.assertEqual(result.actionType, action); self.assertEqual(result.data, 123)

    def test_callback_scan_preserves_adjacent_calls_and_native_multiplayer(self):
        body = '''
local count=0
local function step()count=count+1 end
local e={Game={VisitShop=function()error("vendor call")end,
 SendTrackingTag=function()error("tracking call")end},
 Network={SessionJoin=function()count=count+10;return true end}}
step()e["Game"]["SendTrackingTag"]("MENU")
local callback=e["Game"]["VisitShop"];callback()
assert(e["Network"]["SessionJoin"]())
return count
'''
        with patch.object(menu.profile, 'decoded_view',
                          return_value=(body, body, list(range(len(body)+1)))):
            result = menu.no_vendor_callbacks(body.encode()).decode()
        self.assertEqual(LuaRuntime().execute(result), 11)

    def test_retired_pages_and_popups_do_not_open_or_create_online_ui(self):
        lua = LuaRuntime()
        calls = lua.table_from({'count': 0})
        callbacks = lua.eval('function(c) return {toMainMenu=function()c.count=c.count+1 end} end')(calls)
        page = lua.execute(menu.QUIET_PAGE)
        page.create(callbacks); self.assertEqual(calls.count, 1)
        self.assertFalse(page.isCreated()); self.assertIsNone(page.getFrame())
        popup = lua.execute(menu.QUIET_POPUP).create()
        popup.update(); popup.destroy()
        self.assertFalse(popup.isCreated()); self.assertFalse(popup.isRunning())

    def test_retired_global_chat_keeps_singleplayer_bootstrap_and_callbacks(self):
        lua=LuaRuntime()
        lua.execute('l=setmetatable({},{__index=function()error("unexpected UI or platform dependency")end})')
        chat=lua.execute(menu.QUIET_GLOBAL_CHAT)
        bootstrap=lua.eval('function(chat) chat.create();chat.hide();return true end')
        self.assertTrue(bootstrap(chat))
        for _ in range(3):
            chat.show();chat.hide();chat.update();chat.createChatRoom(1,'ignored')
            chat.receiveChatMessagesCallback({});chat.destroy()
            self.assertFalse(chat.isCreated());self.assertFalse(chat.isVisible());self.assertIsNone(chat.getFrame())
        calls=lua.table_from({'count':0})
        callbacks=lua.eval('function(c)return {toMainMenu=function()c.count=c.count+1 end}end')(calls)
        chat.create(callbacks);self.assertEqual(calls.count,1)

    def test_vendor_controls_never_create_buttons_or_evaluate_callbacks(self):
        body = '''
local created,hidden,bookkeeping=0,0,0
local owner={frame=17}
local fail=function()error("vendor control or callback evaluated")end
local api={button={create=fail,setText=fail},frame={create=function(_,parent)
 assert(parent==17);created=created+1;return created end},ui={
 hide=function(id)assert(id>0);hidden=hidden+1 end,
 disable=function(id)assert(id>0);bookkeeping=bookkeeping+1 end}}
owner["enterCodeButtonID"]=api["button"]["create"]("quoted )",fail())
api["button"]["setText"](owner["enterCodeButtonID"],"(escaped \\\" )")
api.ui.disable(owner["enterCodeButtonID"])
owner["onlineStoreButtonID"]=api["button"]["create"]('another )',fail())
return created,hidden,bookkeeping
'''
        with patch.object(menu.profile, 'decoded_view',
                          return_value=(body, body, list(range(len(body)+1)))):
            result = menu.remove_vendor_controls(body.encode()).decode()
        self.assertEqual(LuaRuntime(unpack_returned_tuples=True).execute(result), (2,2,1))

    def test_call_parser_rejects_unterminated_input(self):
        with self.assertRaisesRegex(ValueError, 'Unterminated'):
            menu.call_end('(nested("unterminated)', 0)

    def test_retired_social_services_never_authorize_or_require_a_vote(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        social = lua.execute(menu.QUIET_FACEBOOK)
        for method in (social.isApplicationAuthorized, social.isWallPostPermitted,
                       social.createRequest().getResponse):
            status, values = method()
            self.assertEqual(status, social.requestStatus.done)
            self.assertFalse(values[1])
        social.forceAuthorized()
        self.assertFalse(social.isApplicationAuthorized()[1][1])
        vote = lua.execute(menu.QUIET_VOTE)
        vote.create(); vote.update(); vote.updateTurn(); vote.show()
        self.assertFalse(vote.isCreated()); self.assertFalse(vote.isVoteRequired())

    def test_campaign_lists_use_all_native_maps_without_writing_completion(self):
        lua = LuaRuntime()
        lua.execute('''
local all={}
for i=1,12 do all[i]="campaign-map-"..i end
e={MapSetup={GetUnlockedCampaignMaps=function(includeAll)
 assert(includeAll==true,"completed prefix queried");return all end}}
e.require=function(name)
 assert(name=="local.ui.listControl")
 return {listControl={create=function()end,templates={},itemTemplates={}}}
end
n=e
setmetatable(e.MapSetup,{__index=function()error("unexpected native mutation")end})
''')
        def decoded(raw):
            body = raw.decode()
            return body, body, list(range(len(body)+1))
        for transform, body in (
            (menu.campaign_script, 'local create=e["listControlFactory"]["create"];'
             'local templates=e["listControlFactory"]["templates"];'
             'local items=e["listControlFactory"]["itemTemplates"];'
             'return e["MapSetup"]["GetUnlockedCampaignMaps"]()'),
            (menu.campaign_list_script, 'return n["MapSetup"]["GetUnlockedCampaignMaps"](n["release"])'),
        ):
            with patch.object(menu.profile, 'decoded_view', side_effect=decoded):
                result = transform(body.encode())
            maps = lua.execute(result.decode())
            self.assertEqual(len(maps), 12)
            self.assertEqual(maps[12], 'campaign-map-12')


if __name__ == '__main__':
    unittest.main()
