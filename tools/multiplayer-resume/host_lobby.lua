-- Owned development hook; pairs with the guarded native readiness getter.
local G, Panel, State = ...
local pending, pendingAt, resumeRecord, refreshAt
local Resume = G.require("local.tools.settings").weplayMPResume
local originalUpdate, originalDestroy = Panel.update, Panel.destroy
function Panel.weplayPrepareResume(callback, record)
    _G.assert(not pending)
    pending, pendingAt = callback, G.Game.GetRunTime() + 1500
    resumeRecord, refreshAt = record, 0
end
function Panel.update()
    originalUpdate()
    if pending then
        G.ui.enable(State.startButton, false)
        if G.Game.GetRunTime() >= pendingAt and G.Network.SessionIsInSession()
            and G.Network.IsHost() and G.Network.GetLocalPlayerSlot() == 1 and G.MapSetup.IsValid() then
            local callback = pending
            pending = nil
            if callback() then Panel.updatePlayerSlots() else resumeRecord = nil end
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
        if not Resume.ready(resumeRecord) then G.ui.enable(State.startButton, false) end
    end
end
function Panel.destroy()
    pending, resumeRecord, refreshAt = nil, nil, nil
    originalDestroy()
end
return Panel
