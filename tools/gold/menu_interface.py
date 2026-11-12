"""Stage Gold's fast, quiet menu from its exact original Interface.bba.

Includes the existing local-profile correction. Never edits an installation.
The black menu is the default; -mapInMainMenu explicitly retains Castle Forge.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import profile_interface as profile

MAIN = 'Script/local/menu/panel/menu/sectionMenus/mainMenu.lua'
HUD = 'Script/local/menu/panel/hud.lua'
NEWS = 'Script/local/menu/panel/menu/newsTicker.lua'
BOOT = 'Script/local/tools/boot.lua'
CARD = 'Script/local/menu/panel/menu/playerCard.lua'
STATUS = 'Script/local/menu/panel/menu/networkStatus.lua'
MISC = 'Script/local/menu/tools/misc.lua'
STATE = 'Script/local/menu/panel/menu/control/mainState.lua'
ACHIEVEMENT = 'Script/local/menu/panel/shared/achievement.lua'
UPLAY = 'Script/local/menu/panel/shared/uPlayAction.lua'
MAP_FORGE = 'Script/local/menu/panel/menu/customize/mapFrg.lua'
CONFIRM = 'Script/local/ui/confirmDialog.lua'
HOST = 'Script/local/menu/panel/menu/play/multiplayer/hostLobby.lua'
BROWSER = 'Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'
UI_CORE = 'Script/local/ui/core.lua'
GLOBAL_CHAT = 'Script/local/menu/panel/menu/community/globalChat.lua'
SCENARIO = 'Script/local/menu/panel/menu/play/scenario.lua'
GAME_END = 'Script/local/menu/panel/gameEnd.lua'
FACEBOOK = 'Script/local/control/facebookHelper.lua'
TEMPLATE_VOTE = 'Script/local/menu/panel/templateVote.lua'
CAMPAIGN = 'Script/local/menu/panel/menu/play/campaign.lua'
MAP_LIST_HELPER = 'Script/local/menu/panel/menu/tools/mapListHelper.lua'
OBSOLETE = {
    'Script/local/menu/panel/menu/community/contacts.lua': 'b11a4531f8a8d0152e4014d108239c1484157102e81ec245cf53c2fda43aa88b',
    'Script/local/menu/panel/menu/community/leaderBoards.lua': '2d99565412803eab79b21dc56c03d708913f312c882b7f2fb85a05a4fca47d3e',
    'Script/local/menu/panel/menu/community/mailbox.lua': '4eca2909004e1931ca454ac33262c1927f4d28bbefd42d5536230816e2c1ddc3',
    'Script/local/menu/panel/menu/community/content.lua': '349a360f2bd322b41a7c00bfd026591f0813175c79e3a7d120ca3d78adc0bd64',
    'Script/local/menu/panel/menu/community/globalChat.lua': '5aa180d6767d720343a82aa787d2ba8cc941259d4716de4c732023af691a92e7',
    'Script/local/menu/panel/menu/community/achievementBrowser.lua': '7cf0017ef7428ab27f32653ad097df962242fe8174c62c2d27d044f855374ad0',
    'Script/local/menu/panel/menu/community/templateExchange.lua': '044b8cf6dbe0369ed54b0c7c4487438f487f87c436cf7001c0b6610a84f1018f',
    'Script/local/menu/panel/menu/customize/shop.lua': '15e031098d138e05081cdd8f699948840c7862be301749d2ccc903f015ee699c',
    'Script/local/menu/panel/menu/extras/privilegeItems.lua': '8973b6afe47ec9e36ce5328723240bccf5454ad508aa3fd44f3eae4ffcf76873',
    'Script/local/menu/panel/menu/expansion/expansionOverview.lua': '3c43f7fd3cdf6f270044cdc6d2f75c207fec1a18c0b0bf6b571cf86c4cbfbac9',
    'Script/local/menu/panel/menu/play/empire.lua': '7db0eef1224d1ccfe166e7c97a6ff7dc66d4745080d8f4460187d5f8d5caaf29',
}
PINS = {
    MAIN: 'b68176fe9cf810ec0cc26a2b5e67eef5abe9026e4dd959861d283062ce59f8cc',
    HUD: 'a131e75abb7266db168809356a03b5417982d97453b22566da53c2b26aa5c677',
    NEWS: '38432b841dd76ce6c3fcdc4f71524a0222658f25cf4043fa23c9b9f1dfa4a936',
    BOOT: '5bcca2a429799ef838a7a7324734b414c3e1e97b151ab28e86cfe13c259ddf8b',
    CARD: 'e9097624991168b32224fd38ee0c63601db137c66b443c63e9085a1c91df2f93',
    STATUS: '539bef44b303a1d83e533d413672338f7cccd8249480b87984335b3bf8548627',
    MISC: 'afd4173678db12165abc793150796a3eeb68e419b02e66de7ff5df6a257b36db',
    STATE: '8177a252cb8865620903580d40d401094faef1058e7bf0518a5121cafb0aa419',
    ACHIEVEMENT: '39b86885b3e54ba489e94ccaff948bff88bad12826c89f8717cf0479ac7bb32a',
    UPLAY: '8efd524e353a835264189fa6183a026a733942bb3c1507dd9b44fe4cef9a29de',
    profile.MEMBER: 'e0be2442bc2fa7e8432db85898016cf4edddcf234b548054a5446c9da370f901',
    **OBSOLETE,
}

# Retain a real empty frame for the original caller's getFrame/isCreated
# lifecycle. No feed refresh, promotional widget or URL callback is installed.
QUIET_NEWS = '''
local frame
local news = {}
function news.create()
 if frame == nil then
  frame = l.frame.create("WePlayQuietNews", l.desktop.frame, 0, 0, 0, 0, l.defaultPriorities.base)
 end
end
function news.isCreated() return frame ~= nil end
function news.getFrame() return frame end
function news.update() end
function news.destroy()
 if frame ~= nil then l.frame.destroy(frame); frame = nil end
end
return news
'''

QUIET_STATUS = '''
local frame, previous, changed
local status = {}
function status.create()
 if frame == nil then
  frame = e.frame.create("WePlayQuietStatus", e.desktop.frame, 0, 0, 0, 0, e.defaultPriorities.base)
  previous, changed = nil, nil
 end
end
function status.isCreated() return frame ~= nil end
function status.getFrame() return frame end
function status.update()
 if frame ~= nil then
  local actual = e.Network.IsLoggedIn()
  if actual ~= previous then previous, changed = actual, actual end
 end
end
function status.show() return changed end
function status.resetShow() changed = nil end
function status.destroy()
 if frame ~= nil then e.frame.destroy(frame); frame = nil end
 previous, changed = nil, nil
end
return status
'''

# Obsolete vendor pages retain distinct inert module identities for the native
# module manager. Dispatch redirects their actions before any page can open.
QUIET_PAGE = '''
local page = {}
function page.create(callbacks) if callbacks and callbacks.toMainMenu then callbacks.toMainMenu() end end
function page.isCreated() return false end
function page.getFrame() return nil end
function page.update() end
function page.destroy() end
return page
'''

# The original in-game chat bootstrap calls create() followed by hide(), even
# in single-player. Retain the retired global channel's complete public shape
# while keeping all channel creation, vendor queries and UI absent.
QUIET_GLOBAL_CHAT = '''
local chat = {}
function chat.create(callbacks) if callbacks and callbacks.toMainMenu then callbacks.toMainMenu() end end
function chat.isCreated() return false end
function chat.getFrame() return nil end
function chat.show() end
function chat.hide() end
function chat.isVisible() return false end
function chat.destroy() end
function chat.update() end
function chat.createChatRoom() end
function chat.receiveChatMessagesCallback() end
return chat
'''

REDIRECT = '''
local weplayRemovedPages = {[10]=true,[12]=true,[101]=true,[102]=true,[103]=true,
 [104]=true,[106]=true,[110]=true,[111]=true,[112]=true,[1003]=true}
if weplayRemovedPages[i.actionType] then i.actionType=t.actionTypes.toMainMenu end
'''

QUIET_POPUP = '''
local popup = {}
function popup.create()
 return {destroy=function()end,update=function()end,
  isCreated=function()return false end,isRunning=function()return false end}
end
function popup.getAchievementData() return nil end
return popup
'''

QUIET_FACEBOOK = '''
local service = {requestStatus={error=1,waiting=2,done=3,requestNotStarted=4}}
local function unavailable() return service.requestStatus.done,{false} end
function service.createRequest()
 return {getResponse=unavailable,setIgnoredErrors=function()end}
end
service.isApplicationAuthorized=unavailable
service.isWallPostPermitted=unavailable
function service.startApplicationAuthorizedRequest()end
function service.startWallPostPermittedRequest()end
function service.updateFirstStatusRequest()end
function service.forceAuthorized()end
return service
'''

QUIET_VOTE = '''
local vote = {}
function vote.create()end
function vote.isCreated()return false end
function vote.isVoteRequired()return false end
function vote.update()end
function vote.updateTurn()end
function vote.show()end
return vote
'''

REMOVED_CALLBACKS = re.compile(
    r'\b\w+\["(?:Game|Network)"\]\["(?:VisitShop|VisitShopLatestDLC|VisitG2W|VisitMyCity|'
    r'StartUplayBrowser|OpenUplayInDefaultBrowser|OpenURLInFeed|SendTrackingTag|SetTrackingTagData)"\]')


def change(raw, before, after):
    source, body, offsets = profile.decoded_view(raw)
    if body.count(before) != 1:
        raise ValueError('Gold menu anchor must match exactly once: ' + before[:70])
    at = body.index(before)
    return (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf8')


def section(raw, first, following, replacement):
    _, body, _ = profile.decoded_view(raw)
    if body.count(first) != 1 or body.count(following) != 1:
        raise ValueError('Gold menu section boundaries are ambiguous')
    begin, end = body.index(first), body.index(following)
    if end <= begin:
        raise ValueError('Gold menu section boundaries are reversed')
    return change(raw, body[begin:end], replacement)


def main_script(raw):
    raw = change(raw,
        'if l["firstStart"]()then\nl["createIntro"](l["intro1"])else\nl["createMainMenu"]()end',
        'l["createMainMenu"]()')
    raw = change(raw, 'e["menuFocalizer"]["create"]()if r then',
                 'e["menuFocalizer"]["create"]()if false then')
    raw = change(raw, '["showLastChanges"]=function()',
                 '["showLastChanges"]=function()do return end\n')
    # Preserve the five containers used by original online-state transitions,
    # without constructing buttons, labels, tooltips or external callbacks.
    empty = '\n'.join(
        'l["' + name + '"]=e.frame.create("WePlayQuietLink",l.frame,0,0,0,0,0)'
        for name in ('onlineStorePanel', 'extrasPanel', 'uplayPanel', 'fbPanel', 'myCityPanel')) + '\n'
    raw = section(raw, 'do\nlocal t=function()e["math"]["randomseed"]',
                  'e["switchCameraCB"]=function()', empty)
    raw = section(raw, 'if not l["bAlreadyCheckedForPendingInvites"]and not l["isIntroOrFlightCreated"]()then',
                  'if not l["loadDialog"]then', '')
    return raw


def hud_script(raw):
    raw = change(raw,
        'e["fancyDialog"]["addButton"](2,45,300+l,o,"Shop",i["shopCB"],"Use your earned coins to purchase new game content.")', '')
    raw = change(raw, '(2,45,350+l,o,"TE_TemplateExchange_Caption"',
                 '(2,45,300+l,o,"TE_TemplateExchange_Caption"')
    raw = section(raw,
        'do\nlocal l=10local o=e["fancyDialog"]["create"](4,',
        'e["fancyDialog"]["updateAll"]()if e["superMenu"]["previous"]==nil then', '')
    raw = change(raw, 'if e["superMenu"]["previous"]==nil then',
                 'if e["superMenu"]["previous"]==nil or e.superMenu.previous>3 then')
    for group, label in ((2, 'Shop'), (4, 'TreasuryTitle'), (4, 'CodeInputTitle')):
        raw = change(raw, 'e["fancyDialog"]["setButtonState"](' + str(group) + ',"' + label + '",l)', '')
    for before in (
        'e["fancyDialog"]["addButton"](1,25+l,300+o,l,"Empire",i["empireCB"],"Play Ladder Games in the Empire Mode.")',
        'e["fancyDialog"]["addButton"](2,45,300+l,o,"TE_TemplateExchange_Caption",i["templateExchangeCB"],"TE_TemplateExchange_Desc")',
        'e["fancyDialog"]["setButtonState"](1,"Empire",l)',
        'e["fancyDialog"]["setButtonState"](2,"TE_TemplateExchange_Caption",l)',
    ):
        raw = change(raw, before, '')
    return raw


def boot_script(raw):
    raw = change(raw, '["mapInMainMenu"]=true', '["mapInMainMenu"]=false')
    return change(raw, 'local function d()if e["Opts"]["noMapInMainMenu"]then',
                  'local function d()if e.Opts.mapInMainMenu then t.mapInMainMenu=true end\n'
                  'if e["Opts"]["noMapInMainMenu"]then')


def card_script(raw):
    # The original layout also measures this local before creating the card.
    raw = change(raw, 'local M=l["Shop"]["GetPlayersCashAmount"]()', 'local M=""')
    raw = change(raw, 'local o,C=550,80', 'local o,C=340,80 ')
    raw = section(raw, 'local u=function(o)', 'local function G()', '')
    raw = change(raw, 'local f=l["require"]("local.menu.panel.shared.chatMentorHelper")', '')
    raw = change(raw, 'local d=l["require"]("local.menu.panel.menu.friendsList")', '')
    raw = section(raw, 'i=o-10t=C-28local r=', 'local function c()', 'end\n')
    raw = section(raw, 'local function c()f["destroyAllDialogs"]()',
                  'l["frame"]["destroy"](e["frame"])', 'local function c()')
    raw = section(raw, 'local function t()if not l["Game"]["PlayerProfileHasActiveProfile"]()',
                  'local l={}function l.create', 'local function t()end\n')
    return section(raw, 'function l.requestDialogCallback(...)', 'return l\n',
        'function l.destroy()if l.isCreated()then c()end end\n'
        'function l.requestDialogCallback()end\nfunction l.highlightContacts()end\n'
        'function l.updateMailCount()end\nfunction l.resetMailIcon()end\n')


def news_script(raw):
    return replace_body(raw, QUIET_NEWS)


def replace_body(raw, body):
    source, _, offsets = profile.decoded_view(raw)
    return (source[:offsets[0]] + body).encode('utf8')


def status_script(raw):
    source, _, offsets = profile.decoded_view(raw)
    return (source[:offsets[0]] + QUIET_STATUS).encode('utf8')


def page_script(raw):
    source, _, offsets = profile.decoded_view(raw)
    return (source[:offsets[0]] + QUIET_PAGE).encode('utf8')


def misc_script(raw):
    # Both open and closed DLC padlocks are sales badges. Keep native Locked
    # and isUnlocked values, enumeration, selection and admission unchanged.
    raw = change(raw, 'if i["isDLC"]then\nlocal o\nif i["isUnlocked"]then',
                 'if false then\nlocal o\nif i["isUnlocked"]then')
    # This older include still names a removed global factory. Use the actual
    # Gold list-control module already used by profile/session screens, rather
    # than relying on unrelated vendor pages to populate a global alias.
    source, body, offsets = profile.decoded_view(raw)
    matches = list(re.finditer(re.escape('l["listControlFactory"]'), body))
    if not matches:
        raise ValueError('Gold map list-control references are missing')
    for match in reversed(matches):
        source = source[:offsets[match.start()]] + 'weplayMapListControl' + source[offsets[match.end()]:]
    source = source[:offsets[0]] + 'local weplayMapListControl=(l.require("local.ui.listControl")).listControl\n' + source[offsets[0]:]
    return source.encode('utf8')


def profile_details_script(raw):
    for field, label, step in (('mentorList', 'Mentor', 75), ('aboutMeEditID', 'AboutMe', 100)):
        before = ('o=o+' + str(step) + 'l["label"]["create"](e["frame"],{["x"]=t,["y"]=o+10},'
                  '"{left}{color:#FFFFFFFF}' + label + ':"')
        raw = change(raw, before,
            'o=o+' + str(step) + ';local weplayLegacyCaption=l["label"]["create"]'
            '(e["frame"],{["x"]=t,["y"]=o+10},"{left}{color:#FFFFFFFF}' + label + ':"')
        raw = change(raw, 'e["' + field + '"]=' ,
                     'l.ui.hide(weplayLegacyCaption)e["' + field + '"]=')
    raw = change(raw, 'e["mentorList"]["setSelectedIndex"](2)end',
                 'e["mentorList"]["setSelectedIndex"](2)end\nl.ui.hide(e.mentorList.getMainFrame())\n')
    return change(raw, 'l["chatInput"]["setTextInputCharLimiter"](e["aboutMeEditID"],500)',
                  'l.ui.hide(e.aboutMeEditID)l["chatInput"]["setTextInputCharLimiter"](e["aboutMeEditID"],500)')


def state_script(raw):
    raw = change(raw, 'local V=function()x["updateFirstStatusRequest"]()end', 'local V=function()end')
    raw = change(raw,
        'local i,t,l=e["Game"]["GetPlayersFacebookCredentials"]()if i~=""and t~=""and l~=""then\n'
        'e["Network"]["SetFBUserCredentials"](i,t,l)end', '')
    return change(raw, 'for c,i in _G["ipairs"](i)do\nif o then',
                  'for c,i in _G["ipairs"](i)do\n' + REDIRECT + '\nif o then')


def achievement_script(raw):
    return replace_body(raw, QUIET_POPUP)


def uplay_script(raw):
    return replace_body(raw, QUIET_POPUP)


def no_vendor_callbacks(raw):
    """Remove external shop/browser and tracking callback references everywhere.

    The complete original Gold archive is fingerprinted before this scan. This
    also catches functions passed as button callbacks, not just direct calls.
    Copyright/credit text and native multiplayer operations are not matched.
    """
    try:
        source, body, offsets = profile.decoded_view(raw)
    except ValueError:
        return raw
    matches = list(REMOVED_CALLBACKS.finditer(body))
    if not matches:
        return raw
    for match in reversed(matches):
        source = source[:offsets[match.start()]] + 'weplayIgnoreVendor' + source[offsets[match.end()]:]
    source = source[:offsets[0]] + 'local weplayIgnoreVendor=function()end\n' + source[offsets[0]:]
    return source.encode('utf8')


def call_end(body, opening):
    """End of one Lua call, ignoring parentheses inside quoted strings."""
    depth, quote, escaped = 0, None, False
    for at in range(opening, len(body)):
        char = body[at]
        if quote:
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == quote:
                quote = None
        elif char in ('"', "'"):
            quote = char
        elif char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
            if depth == 0:
                return at + 1
    raise ValueError('Unterminated Gold UI call')


def remove_vendor_controls(raw):
    try:
        source, body, offsets = profile.decoded_view(raw)
    except ValueError:
        return raw
    pattern = re.compile(r'(?P<target>\w+\["(?:enterCodeButtonID|onlineStoreButtonID)"\])='
                         r'(?P<api>\w+)\["button"\]\["create"\]\(')
    edits, targets = [], []
    for match in pattern.finditer(body):
        end = call_end(body, match.end() - 1)
        target, api = match['target'], match['api']
        owner = target.split('[')[0]
        # Keep an invisible noninteractive frame for original online-state
        # enable/disable bookkeeping; never create the vendor button/callback.
        replacement = (target + '=' + api + '.frame.create("WePlayQuietVendorControl",'
                       + owner + '.frame,0,0,0,0,0);' + api + '.ui.hide(' + target + ');')
        edits.append((match.start(), end, replacement))
        targets.append(target)
    for target in targets:
        style = re.compile(r'\b\w+\["button"\]\["\w+"\]\(' + re.escape(target) + r'[,)]')
        for match in style.finditer(body):
            if any(start <= match.start() < end for start, end, _ in edits):
                continue
            opening = body.index('(', match.start())
            edits.append((match.start(), call_end(body, opening), 'weplayIgnoreVendorControl()'))
    if not edits:
        return raw
    for start, end, replacement in sorted(edits, reverse=True):
        source = source[:offsets[start]] + replacement + source[offsets[end]:]
    source = source[:offsets[0]] + 'local weplayIgnoreVendorControl=function()end\n' + source[offsets[0]:]
    return source.encode('utf8')


def map_forge_script(raw):
    raw = section(raw, 'l["onlineStoreButtonID"]=e["button"]["create"](',
                  'end,["create"]=function(l)', '')
    raw = change(raw, '["caption"]="TE_Publish",["callback"]=function()Q(t["template"])end',
                 '["caption"]="",["callback"]=function()end')
    return change(raw, 'if a==4then\nd["publishButtonID"]=f',
                  'if a==4 then\ne.ui.hide(f)d["publishButtonID"]=f')


def confirm_script(raw):
    # A genuinely unavailable local map still fails. Its dialog can only close;
    # it no longer offers a dead online store or code-redemption route.
    for kind in ('expansion', 'lastChangesExpansion'):
        raw = change(raw,
            '[e["types"]["' + kind + '"]]={[e["actionTypes"]["confirm"]]="OnlineStoreTitle",'
            '[e["actionTypes"]["deny"]]="CodeInputTitle",[e["actionTypes"]["cancel"]]="CANCEL",}',
            '[e["types"]["' + kind + '"]]={[e["actionTypes"]["confirm"]]="OK",'
            '[e["actionTypes"]["deny"]]=nil,[e["actionTypes"]["cancel"]]=nil,}')
    return change(raw, 'local s=function(f,h,c,i,o,u,s,d,t)if not o then',
        'local s=function(f,h,c,i,o,u,s,d,t)\n'
        'if f==e.types.expansion or f==e.types.lastChangesExpansion then\n'
        'h={"This map is not available in the local installation.",true};d={"Map unavailable",true}\nend\n'
        'if not o then')


def host_script(raw):
    raw = change(raw, 'false)local o=i-155-530o=o+100+5local a=n["label"]["create"]',
        'false)n.ui.hide(e.inviteButtonID)n.ui.hide(e.inviteButtonOrnamentID)'
        'local o=i-155-530;o=o+100+5;local a=n["label"]["create"]')
    for name in ('inviteButtonID', 'inviteButtonOrnamentID'):
        raw = change(raw, 'n["ui"]["show"](e["' + name + '"],e["InvitePossible"])',
                     'n["ui"]["hide"](e["' + name + '"])')
    return raw


def browser_script(raw):
    before = 'e["ui"]["setSize"](l["seeInvitationsButtonID"],200,45)'
    return change(raw, before, before + 'e.ui.hide(l.seeInvitationsButtonID)')


def ui_core_script(raw):
    # Resolve inside the template constructor, after core UI initialization;
    # eager loading here would create a UI-core/list-control dependency cycle.
    before = 'e["listControlFactory"]'
    source, body, offsets = profile.decoded_view(raw)
    matches = list(re.finditer(re.escape(before), body))
    if len(matches) != 2:
        raise ValueError('Gold template list-control references changed')
    for match in reversed(matches):
        source = source[:offsets[match.start()]] + '(e.require("local.ui.listControl")).listControl' + source[offsets[match.end()]:]
    return source.encode('utf8')


def scenario_script(raw):
    # Enumerate the complete installed mini-campaign rather than its completed
    # prefix. Content existence/availability and saved completion stay native.
    raw = change(raw, 'local h=e["MapSetup"]["GetUnlockedRotRMaps"](false)', 'local h=r ')
    # These navigation entries have no button ID and therefore need their own
    # removal; deleting only browser callbacks would leave visible dead links.
    for label, callback, count in (('OnlineStoreTitle', 'shopCB', 4),
                                    ('CodeInputTitle', 'redeemCB', 2)):
        anchor = '{["name"]="' + label + '",["callback"]=l["' + callback + '"]},'
        if profile.decoded_view(raw)[1].count(anchor) != count:
            raise ValueError('Gold scenario vendor navigation changed')
        for _ in range(count):
            source, body, offsets = profile.decoded_view(raw)
            at = body.index(anchor)
            raw = (source[:offsets[at]] + source[offsets[at+len(anchor)]:]).encode('utf8')
    return section(raw, '["showScenarioOffer"]=function()',
                   '["mediaPlayer"]=e["require"]', '["showScenarioOffer"]=function()end,')


def campaign_script(raw):
    # Gold binding 5B5E40 -> 5B5770 enumerates the full campaign vector when
    # argument 1 is true. It does not write completed missions to the profile.
    raw = change(raw, 'e["MapSetup"]["GetUnlockedCampaignMaps"]()',
                 'e["MapSetup"]["GetUnlockedCampaignMaps"](true)')
    source, body, offsets = profile.decoded_view(raw)
    matches = list(re.finditer(re.escape('e["listControlFactory"]'), body))
    if len(matches) != 3:
        raise ValueError('Gold campaign difficulty list-control references changed')
    for match in reversed(matches):
        source = source[:offsets[match.start()]] + '(e.require("local.ui.listControl")).listControl' + source[offsets[match.end()]:]
    return source.encode('utf8')


def campaign_list_script(raw):
    return change(raw, 'n["MapSetup"]["GetUnlockedCampaignMaps"](n["release"])',
                  'n["MapSetup"]["GetUnlockedCampaignMaps"](true)')


def game_end_script(raw):
    # Keep local rewards, campaign progression, statistics and session report.
    # Remove the shop-currency presentation and its purchase-content nag.
    raw = section(raw, 'if n["gameEnd"]["funds"]then\nn["label"]["create"]',
                  'local i=n["frame"]["create"]("gameEndToggleButtonsFrame"', '')
    raw = section(raw, 'if n["gameEnd"]["funds"]then\nt=n["iconFactory"]',
                  'if c["isCreated"]()then\ne=n["button"]["create"]', '')
    for name, following in (('showSummary', 'destroySummary'), ('updateFunds', 'quitCB')):
        raw = section(raw, '["' + name + '"]=function()',
                      '["' + following + '"]=function()', '["' + name + '"]=function()end,')
    return raw


def build(raw):
    if hashlib.sha256(raw).hexdigest() != profile.SOURCE:
        raise ValueError('Gold menu requires the exact original Gold archive')
    original = profile.Archive(raw)
    archive = profile.Archive(profile.build(raw))
    replacements = {}
    patches = {MAIN: main_script, HUD: hud_script, NEWS: news_script,
               BOOT: boot_script, CARD: card_script, STATUS: status_script,
               MISC: misc_script, STATE: state_script, ACHIEVEMENT: achievement_script,
               UPLAY: uplay_script, profile.MEMBER: profile_details_script,
               **{name: page_script for name in OBSOLETE}}
    patches[GLOBAL_CHAT] = lambda raw: replace_body(raw,QUIET_GLOBAL_CHAT)
    for name, patch in patches.items():
        script = original.read(name)
        if hashlib.sha256(script).hexdigest() != PINS[name]:
            raise ValueError('Unexpected original Gold menu member: ' + name)
        replacements[name] = patch(archive.read(name))
    # These original members are covered by the full archive fingerprint.
    replacements[MAP_FORGE] = map_forge_script(original.read(MAP_FORGE))
    replacements[CONFIRM] = confirm_script(original.read(CONFIRM))
    replacements[HOST] = host_script(original.read(HOST))
    replacements[BROWSER] = browser_script(original.read(BROWSER))
    replacements[UI_CORE] = ui_core_script(original.read(UI_CORE))
    replacements[SCENARIO] = scenario_script(original.read(SCENARIO))
    replacements[GAME_END] = game_end_script(original.read(GAME_END))
    replacements[FACEBOOK] = replace_body(original.read(FACEBOOK), QUIET_FACEBOOK)
    replacements[TEMPLATE_VOTE] = replace_body(original.read(TEMPLATE_VOTE), QUIET_VOTE)
    replacements[CAMPAIGN] = campaign_script(original.read(CAMPAIGN))
    replacements[MAP_LIST_HELPER] = campaign_list_script(original.read(MAP_LIST_HELPER))
    for name, entry in original.entries.items():
        if entry.values[4] != 256 and name.endswith('.lua'):
            before = replacements.get(name, archive.read(name))
            after = no_vendor_callbacks(remove_vendor_controls(before))
            if after != before:
                replacements[name] = after
    result = archive.replace(replacements)
    verified = profile.Archive(result)
    if original.entries.keys() != verified.entries.keys():
        raise ValueError('Gold menu changed the archive member set')
    for name, entry in original.entries.items():
        if entry.values[4] != 256 and verified.read(name) != replacements.get(name, archive.read(name)):
            raise ValueError('Gold menu archive verification failed: ' + name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Original Gold Interface.bba')
    parser.add_argument('--output', required=True, type=Path, help='New staging directory')
    args = parser.parse_args()
    result = build(args.source.read_bytes())
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'Interface.bba').write_bytes(result)
    report = {'profile': 'gold-quiet-menu', 'source_sha256': profile.SOURCE,
              'archive_sha256': hashlib.sha256(result).hexdigest(), 'installed': False,
              'startup_options_requires_native_adapter': True}
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
