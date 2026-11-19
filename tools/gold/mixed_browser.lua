-- Export the original Gold browser's creation callback for an owned mixed host.
-- Discovery completion and the selected network filter remain original UI state.
local G, Panel, State, states, create = ...
local function allowed()
 if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return false end
 local mode=G.UI.WriteToConsole("weplay-crossplay-v1:mode")
 if (mode~="host" and mode~="room-host") or not State.frame or State.lastDiscoveryFinished~=true
  or State.sessionListState~=states.idle or not G.Network.IsLoggedIn()
  or G.Network.SessionIsInSession() then return false end
 local selected=State.sessionsFilterList and State.sessionsFilterList.getSelectedItemData()
 if _G.type(selected)~="table" or _G.type(selected.lan)~="boolean" then return false end
 local discovery=G.Network.SessionGetDiscovery(false)
 return _G.type(discovery)=="table" and discovery.State==2
end
function Panel.weplayCanOpenMixedCreation() return allowed() end
function Panel.weplayOpenMixedCreation()
 if not allowed() then return false end
 create()
 return true
end

if not G.UI or _G.type(G.UI.WriteToConsole) ~= "function"
    or G.UI.WriteToConsole("weplay-crossplay-v1:mode") ~= "room-host" then return end
local dialog
local originalCreate, originalUpdate, originalDestroy = Panel.create, Panel.update, Panel.destroy
local function closeDialog()
    if dialog then dialog.destroy(); dialog = nil end
end
local function openDialog()
    if dialog or not allowed() then return end
    local factory = G.require("local.menu.panel.shared.tools.loadSaveDialogFactory")
    dialog = factory.createLoadMultiplayerGameDialog()
    local width, height = G.system.getScreenSize()
    dialog.setPosition(width / 2, height / 2, G.UI.Anchor.Center, G.UI.Anchor.Center)
end
local function loadSlot(slot)
    if not allowed() then return end
    local map = G.UI.WriteToConsole("weplay-crossplay-v1:room-map")
    local data = G.MapSetup.GetMapDataBySavegame(slot)
    if _G.type(map) ~= "string" or _G.type(data) ~= "table" or _G.type(data.MapPath) ~= "string"
        or data.MapPath:gsub("\\", "/"):lower() ~= map:gsub("\\", "/"):lower() then
        G.ui.messageBox("WePlay", "Dieser Spielstand gehört nicht zur Karte des Raums.")
        return
    end
    local neo = G.require("local.tools.settings").weplayNeoLogistics
    if not neo.selectSaved(slot) then
        G.ui.messageBox("WePlay", "Dieser Spielstand konnte für den Raum nicht geprüft werden.")
        return
    end
    local source = G.Game.GetSaveGameFileName(slot)
    G.map.loadSetupMapConfig(map, nil)
    if not G.MapSetup.IsValid() or not G.Network.SessionCreate(map, false, 2, false, false, nil, false) then
        G.ui.messageBox("WePlay", "Die Lobby konnte nicht erstellt werden.")
        return
    end
    G.MapSetup.SetCoopMode(0)
    local host = G.require("local.menu.panel.menu.play.multiplayer.hostLobby")
    host.weplayPrepareSavedRoom(source)
    State.callbacks.toHostLobby({fixedTeams=false, fixedPositions=false, privateSession=false})
end
function Panel.create(...)
    originalCreate(...)
    local _, height = G.system.getScreenSize()
    local button = G.button.create(State.browserFrame, {x=80,y=height-230}, "LOAD GAME",
        G.button.class.optionsButton, openDialog, nil, G.UI.Anchor.Top, G.UI.Anchor.Left)
    G.ui.setSize(button, 240, 45)
end
function Panel.update(...)
    if dialog then
        local types = G.require("local.menu.panel.shared.loadSaveListDialog").actionTypes
        for _, action in _G.ipairs(dialog.fetchActions()) do
            if action.actionType == types.confirm then
                local slot = action.data.itemData.data.slot
                closeDialog()
                loadSlot(slot)
                return
            elseif action.actionType == types.cancel then closeDialog(); return end
        end
    end
    originalUpdate(...)
end
function Panel.destroy(...)
    closeDialog()
    return originalDestroy(...)
end
