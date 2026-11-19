-- Owned development hook; pairs with the guarded native readiness getter.
local G, Panel, getState = ...
local pending, pendingAt, resumeRecord, refreshAt
local Resume = G.require("local.tools.settings").weplayMPResume
local console = G.UI and G.UI.WriteToConsole
local mixedRoomHost = _G.type(console) == "function" and console("weplay-crossplay-v1:mode") == "room-host"
local roomRosterRemembered, roomTeamsRequested, roomStartReady = false, false, false
local originalUpdate, originalDestroy, originalSlots = Panel.update, Panel.destroy, Panel.updatePlayerSlots
local function cancelResume()
    pending, pendingAt, resumeRecord, refreshAt = nil, nil, nil, nil
end
local function ownsLobby(state)
    return Panel.isCreated() and getState() == state
        and G.Network.SessionIsInSession() and G.Network.IsHost()
        and G.Network.GetLocalPlayerSlot() == 1
end
local function stopResume(state)
    cancelResume()
    if Panel.isCreated() and getState() == state and state.startButton then
        G.ui.enable(state.startButton, false)
    end
end
function Panel.updatePlayerSlots(...)
    -- Native SetSlotCounts requires the session's live handler. A queued
    -- configuration event may outlive the lobby that originally requested it.
    local state = getState()
    if not ownsLobby(state) then stopResume(state); return end
    return originalSlots(...)
end
function Panel.weplayPrepareResume(callback, record)
    _G.assert(not pending)
    pending, pendingAt = callback, G.Game.GetRunTime() + 1500
    resumeRecord, refreshAt = record, 0
end
function Panel.update()
    if not Panel.isCreated() then cancelResume(); return end
    local State = getState()
    originalUpdate()
    -- The original update may synchronously change/destroy the current page.
    -- Do not cancel work belonging to a newly created replacement page.
    if getState() ~= State then return end
    if not ownsLobby(State) then stopResume(State); return end
    if mixedRoomHost and not roomRosterRemembered and not pending and not resumeRecord then
        if not roomTeamsRequested and console("weplay-crossplay-v1:room-all-ready") == "1" then
            roomTeamsRequested = console("weplay-crossplay-v1:room-sync-teams") == "1"
        end
        roomRosterRemembered = Resume.rememberRoomLobby()
    end
    if pending then
        G.ui.enable(State.startButton, false)
        local loadReady = mixedRoomHost and console("weplay-crossplay-v1:room-saved-state") == "selection-available"
            or not mixedRoomHost and G.Game.GetRunTime() >= pendingAt
        if loadReady and G.Network.SessionIsInSession()
            and G.Network.IsHost() and G.Network.GetLocalPlayerSlot() == 1 and G.MapSetup.IsValid() then
            local callback = pending
            pending = nil
            local restored = callback()
            if getState() ~= State then return end
            if not ownsLobby(State) then stopResume(State); return end
            if restored then Panel.updatePlayerSlots() else resumeRecord = nil end
        end
    end
    if resumeRecord and not pending then
        -- Queue completion does not modify native Ready, so it emits no lobby
        -- configuration event. Refresh the cached UI while waiting for peers.
        local now = G.Game.GetRunTime()
        if not State.allHumansAreReady and now >= refreshAt then
            refreshAt = now + 100
            Panel.updatePlayerSlots()
        end
        if not mixedRoomHost and not Resume.ready(resumeRecord) then G.ui.enable(State.startButton, false) end
    end
    if mixedRoomHost and not pending then
        local ready
        if resumeRecord then
            ready = console("weplay-crossplay-v1:room-saved-state") == "ready-verified"
        else
            ready = console("weplay-crossplay-v1:room-teams-confirmed") == "1"
        end
        if not ready then
            G.ui.enable(State.startButton, false)
        elseif not roomStartReady then
            Panel.updatePlayerSlots()
        end
        roomStartReady = ready
    end
end
function Panel.destroy()
    cancelResume()
    roomRosterRemembered, roomTeamsRequested, roomStartReady = false, false, false
    originalDestroy()
end
return Panel
