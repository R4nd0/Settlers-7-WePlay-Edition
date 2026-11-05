-- Development probe, invoked from the normal game update after default systems.
-- Keep native create/save/restart calls on their normal Lua call path. Extra
-- pcall wrappers around the test lifecycle reproduced the DXVK wait on PC1.
local G, enabled = ...
local key = "weplay_neo_probe_expected_v1"
local initialized, finished = false, false
local function log(text) G.UI.WriteToConsole("Attempt to NEO-PROBE " .. text) end
local function panelCheck()
    local panel = G.logisticsSystem
    panel.create()
    _G.assert(panel.isCreated(), "logistics panel missing")
    local groups, tasks = 0, 0
    for _, group in _G.pairs(panel.panels) do
        groups = groups + 1
        _G.assert(G.ui.isEnabled(group.panelButton) == not enabled, "group control state")
        for _, task in _G.pairs(group.tasks) do
            _G.assert(G.ui.isEnabled(task.buttonID), "individual task control disabled")
            tasks = tasks + 1
        end
    end
    _G.assert(groups == 4, "group count")
    if enabled then
        panel.startPanelDragging()
        _G.assert(panel.panelDragging == nil, "group drag accepted")
    end
    panel.destroy()
    log("panel groups=" .. groups .. " individual-controls=" .. tasks)
end
return function()
    if finished or not G.game.started or not G.Game.InMapMode() then return end
    local M = G.require("local.tools.settings").weplayNeoLogistics
    _G.assert(not G.Network.InNetworkMode(), "single-player probe only")
    _G.assert(M.active() == enabled, "active mode")
    _G.assert(G.UI.WriteToConsole("weplay-neo-v1:status") == (enabled and "1" or "0"), "native mode")
    local turn = G.Game.GetCurrentTurn()
    if not initialized then
        initialized = true
        log("world enabled=" .. _G.tostring(enabled) .. " turn=" .. turn)
        G.Game.SetGameTurnDuration(5)
    end
    if G.state.currentMode == G.state.mode.saveGame then
        local expected = G.Game.GetCachedValue(key)
        local actual = G.UI.WriteToConsole("weplay-neo-v1:snapshot")
        _G.assert(_G.type(expected) == "string" and expected == actual, "saved intent ledger changed")
        panelCheck()
        finished = true
        log("PASS start-save-load exact-ledger turn=" .. turn)
        G.system.exitApi()
    elseif turn >= 2000 then
        panelCheck()
        local payload = G.UI.WriteToConsole("weplay-neo-v1:snapshot")
        _G.assert(_G.type(payload) == "string", "snapshot missing")
        local count = enabled and _G.tonumber(payload:match("^NEO1\n%d+\n(%d+)")) or 0
        _G.assert(count and (not enabled or count > 0), "no explicit AI intents observed")
        G.Game.CacheValue(key, payload)
        local saves = G.require("local.file.saveGames")
        local slot = saves.getFreeSaveGameSlot()
        _G.assert(slot and not G.Game.SaveGameExists(slot), "no unused test save slot")
        log("save slot=" .. slot .. " turn=" .. turn .. " explicit-intents=" .. count)
        G.game.save({slot=slot, name="NEO-Logistics lifecycle probe"})
        _G.assert(G.Game.SaveGameExists(slot), "native save missing")
        finished = true
        G.game.restartWithSaveGame({slot=slot})
    end
end
