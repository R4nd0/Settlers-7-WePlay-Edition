-- Bind one routing kernel to this original main-menu lifetime. No game controls
-- or preparation rows are written here. Native jobs execute without pcall.
local G, State, expectedMode, route, prepare = ...
if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return {tick=function()end} end
local mode=G.UI.WriteToConsole("weplay-crossplay-v1:mode")
if mode~=expectedMode then return {tick=function()end} end
local function page(name)return G.require("local.menu.panel.menu."..name)end
local Pages={Main=page("sectionMenus.mainMenu"),Browser=page("play.multiplayer.sessionList"),
 Creation=page("play.multiplayer.sessionCreation")}
local function callbacks()return State.actionManager.createCallbacks(State.currentPageModule)end
local actions={
 open_browser=function()callbacks().toSessionList()end,
 can_open_host_creation=function()return Pages.Browser.weplayCanOpenMixedCreation()==true end,
 can_prepare_guest=function()return Pages.Browser.weplayCanPrepareMixedGuest()==true end,
 open_host_creation=function()return Pages.Browser.weplayOpenMixedCreation()end,
 prepare_guest_map=prepare,
 -- Deliberately closed until the production adapter supplies an applied-setup
 -- presentation contract. Bootstrap choices cannot authorize a guest page.
 open_guest_lobby=function()end,
}
return route(G,State,Pages,actions)
