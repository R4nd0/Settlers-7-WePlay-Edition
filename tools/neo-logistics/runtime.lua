-- Per-match configuration and saved explicit-priority state. No profile setting.
local G,settings,store,Resume = ...
local M = {}
local key = "weplay_neo_logistics_v1"
local selected, active, setupMap, loading = false, false, nil, false
local function bridge(command) return G.UI.WriteToConsole("weplay-neo-v1:" .. command) end
function M.available() return bridge("capabilities") == "NEO1" end
function M.networkState() return bridge("network-state") end
function M.networkReady()
    local state=M.networkState()
    return state=="0" or state=="1"
end
function M.selectSaved(slot)
    if not Resume or not Resume.lookup(slot) then return false end
    local saved=settings.weplayMPResumeSlots[_G.tostring(slot)]
    if saved.neoLogistics~=nil and _G.type(saved.neoLogistics)~="boolean" then return false end
    local enabled=saved.neoLogistics==true
    if bridge("stage-file:" .. (enabled and "1:" or "0:") .. G.Game.GetSaveGameFileName(slot))==true then
        selected=enabled
        return true
    end
    M.select(false)
    return false
end
function M.active() return active end
function M.setup(map)
    if map ~= setupMap then
        selected, setupMap = false, map
        bridge("stage:0")
    end
    return selected
end
function M.select(value)
    selected = value == true and M.available()
    if bridge("stage:" .. (selected and "1" or "0")) ~= true then selected = false end
    return selected
end
local function failed()
    G.ui.messageBox("NEO-Logistics", "Die Logistikeinstellung konnte nicht sicher uebernommen werden. Der Vorgang wurde abgebrochen.")
    return false
end
function M.acceptNetworkStart()
    if not M.networkReady() or bridge("start-event")~=true then return failed() end
    return true
end
function M.beforeStart()
    loading = false
    -- Starting a map recreates the local Lua state. The native adapter owns
    -- the pending per-map choice across that boundary.
    local choice=bridge("selection")
    selected = choice == "1"
    if M.available() and choice~="0" and choice~="1" then return failed() end
    if selected and not M.available() then return failed() end
    if bridge("begin:" .. (selected and "1" or "0")) ~= true then
        -- Legacy adapters may still start an ordinary game.
        if selected or M.available() or G.Network.InNetworkMode() then return failed() end
    end
    active = selected
    return true
end
function M.beforeLoad()
    selected, active, setupMap, loading = false, false, nil, true
    return bridge("load") == true
end
function M.restore(jar)
    if not loading then return true end
    loading = false
    if G.Network.InNetworkMode() then
        if bridge("restore-network")==true then
            local state=bridge("status")
            if state~="0" and state~="1" then return failed() end
            active=state=="1"
            return true
        end
        -- An enabled match must have the full host-file state locked by the
        -- real start event; never rebuild an empty individual-intent ledger.
        if M.networkState()=="1" then return failed() end
    end
    if not jar.isPickled(key) then
        if G.Network.InNetworkMode() and M.networkState()=="1" then return failed() end
        active = false
        bridge("legacy")
        return true
    end
    local saved = jar.get(key)
    if _G.type(saved) ~= "table" or saved.version ~= 1 or _G.type(saved.payload) ~= "string"
        or #saved.payload > 2097152 or saved.payload:find("%z") then return failed() end
    if bridge("restore:" .. saved.payload) ~= true then return failed() end
    local state = bridge("status")
    if state ~= "0" and state ~= "1" then return failed() end
    active = state == "1"
    return true
end
function M.save(jar)
    local payload = bridge("save-snapshot:" .. G.Game.GetCurrentTurn())
    if _G.type(payload) ~= "string" or #payload > 2097152 then
        if active then return failed() end
        return true
    end
    jar.put(key, {version=1, payload=payload})
    return true
end
function M.finished()
    bridge("end")
    selected, active, setupMap, loading = false, false, nil, false
end
function M.hookGame(game, jar)
    local start, network, load, pickle, destroy = game.startSingleplayer, game.startNetwork, game.loadSavegame, game.pickle, game.destroy
    game.startSingleplayer = function(...)
        _G.assert(M.beforeStart(), "NEO-Logistics start rejected")
        local result = start(...)
        if active then _G.assert(bridge("ready") == true, "NEO-Logistics world start rejected")
        else bridge("ready") end
        return result
    end
    game.startNetwork = function(slot,...)
        _G.assert(M.networkReady(), "NEO-Logistics network configuration missing")
        if slot==game.multiplayerSavegameSlot then M.beforeLoad()
        else _G.assert(M.beforeStart(), "NEO-Logistics network start rejected") end
        local result=network(slot,...)
        if slot==game.multiplayerSavegameSlot then
            _G.assert(M.restore(jar), "NEO-Logistics multiplayer saved state rejected")
        else _G.assert(bridge("ready")==true, "NEO-Logistics network world start rejected") end
        return result
    end
    game.loadSavegame = function(...)
        M.beforeLoad()
        local okay = load(...)
        if okay then _G.assert(M.restore(jar), "NEO-Logistics saved state rejected") end
        return okay
    end
    game.pickle = function(...)
        pickle(...)
        _G.assert(M.save(jar), "NEO-Logistics save rejected")
    end
    game.destroy = function(...)
        local wasStarted = game.started == true
        local result = destroy(...)
        if wasStarted then M.finished() end
        return result
    end
end
if Resume then
    local saved=Resume.saved
    Resume.saved=function(slot,name)
        local okay=saved(slot,name)
        if okay then
            settings.weplayMPResumeSlots[_G.tostring(slot)].neoLogistics=active
            store()
        end
        return okay
    end
end
return M
