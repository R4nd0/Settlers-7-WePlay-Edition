-- Owned session-browser extension. The caller supplies the existing UI module
-- and its private state; saved roles come exclusively from validated metadata.
local G, Panel, State = ...
local Resume = G.require("local.tools.settings").weplayMPResume
local dialog
local originalCreate, originalUpdate, originalDestroy = Panel.create, Panel.update, Panel.destroy
local function closeDialog()
    if dialog then dialog.destroy(); dialog = nil end
end
local function openDialog()
    if dialog then return end
    dialog = G.require("local.menu.panel.shared.tools.loadSaveDialogFactory").createLoadMultiplayerGameDialog()
    local width, height = G.system.getScreenSize()
    dialog.setPosition(width / 2, height / 2, G.UI.Anchor.Center, G.UI.Anchor.Center)
end
local function loadSlot(slot)
    local record = Resume.lookup(slot)
    if not record then
        G.ui.messageBox("WePlay", "Für diesen Spielstand fehlt eine geprüfte Mehrspieler-Belegung.")
        return
    end
    local template = record.template ~= "" and record.template or nil
    if template then
        local found = false
        for _, name in _G.ipairs(G.MapForge.GetTemplateNames(record.map) or {}) do
            if name == template then found = true; break end
        end
        if not found then
            G.ui.messageBox("WePlay", "Die Karten-Vorlage dieses Spielstands fehlt. Bitte die ursprüngliche Vorlage wiederherstellen.")
            return
        end
    end
    local savePath = G.Game.GetSaveGameFileName(slot)
    if not savePath or savePath == "" then return end
    G.map.loadSetupMapConfig(record.map, template)
    G.MapSetup.SetCoopMode(record.coop)
    G.Network.SetIsQuickMatch(false)
    G.Network.SetIsPrivate(false)
    local humans = 0
    for _, player in _G.ipairs(record.players) do if player.role == 1 then humans = humans + 1 end end
    local filter = State.sessionsFilterList.getSelectedItemData()
    if not G.Network.SessionCreate(record.map, false, filter.lan, humans, true, true, record.template, false) then
        G.ui.messageBox("WePlay", "Die Lobby konnte nicht erstellt werden.")
        return
    end
    local function applyResume()
        local loaded = G.Network.ChangeSavegame(savePath)
        if not loaded or loaded == "" or not Resume.applyRoster(record) then
            G.Network.SessionLeave()
            G.ui.messageBox("WePlay", "Der Mehrspieler-Spielstand konnte nicht wiederhergestellt werden.")
            return false
        end
        return true
    end
    local host = G.require("local.menu.panel.menu.play.multiplayer.hostLobby")
    host.weplayPrepareResume(applyResume, record)
    State.callbacks.toHostLobby({fixedTeams = true, fixedPositions = true, privateSession = false})
end
function Panel.create(callbacks)
    originalCreate(callbacks)
    local width, height = G.system.getScreenSize()
    local button = G.button.create(State.browserFrame, {x = 80, y = height - 230}, "LOAD GAME",
        G.button.class.optionsButton, openDialog, nil, G.UI.Anchor.Top, G.UI.Anchor.Left)
    G.ui.setSize(button, 240, 45)
end
function Panel.update()
    if dialog then
        local types = G.require("local.menu.panel.shared.loadSaveListDialog").actionTypes
        for _, action in _G.ipairs(dialog.fetchActions()) do
            if action.actionType == types.confirm then
                local slot = action.data.itemData.data.slot
                closeDialog(); loadSlot(slot); return
            elseif action.actionType == types.cancel then closeDialog(); return end
        end
    end
    originalUpdate()
end
function Panel.destroy()
    closeDialog()
    originalDestroy()
end
return Panel
