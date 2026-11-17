-- Per-match configuration and saved explicit-priority state. No profile setting.
local G,settings,store,Resume = ...
local M = {}
local key = "weplay_neo_logistics_v1"
local selected, active, setupMap, loading = false, false, nil, false
local restoredMixed=false
-- Resolve this process-local mode once. Start/world calls must not consult
-- preparation, phase or applied-lobby objects after the native world transition.
local mode=G.UI.WriteToConsole("weplay-crossplay-v1:mode")
local mixed=mode=="host" or mode=="guest"
local mixedQueries={capabilities=true,selection=true,["network-state"]=true,status=true}
local mixedActions={["stage:0"]=true,["stage:1"]=true,["start-event"]=true,
    ["begin:0"]=true,["begin:1"]=true,ready=true,load=true,["restore-network"]=true}
local function bridge(command)
    if mixed then
        local snapshot=mode=="host" and command:match("^save%-snapshot:%d+$")
        local selectedFile=mode=="host" and command:sub(1,12)=="select-file:"
        if not mixedQueries[command] and not mixedActions[command] and not snapshot and not selectedFile then return false end
        local result=G.UI.WriteToConsole("weplay-crossplay-neo-v1:"..command)
        if mixedActions[command] or selectedFile then return result=="1" end
        return result
    end
    return G.UI.WriteToConsole("weplay-neo-v1:"..command)
end
function M.available() return bridge("capabilities") == (mixed and "NEO-FIFO-INTENT-1" or "NEO1") end
function M.networkState() return bridge("network-state") end
function M.networkReady()
    local state=M.networkState()
    return state=="0" or state=="1"
end
function M.selectSaved(slot)
    if mixed then
        if mode~="host" or _G.type(slot)~="number" or slot<0 or slot>4294967294 or slot~=_G.math.floor(slot)
            or _G.type(G.Game.GetSaveGameFileName)~="function" then return false end
        local path=G.Game.GetSaveGameFileName(slot)
        if _G.type(path)~="string" or #path==0 or #path>32767 or path:find("%z") then return false end
        return bridge("select-file:"..path)==true
    end
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
    restoredMixed=false
    -- Starting a map recreates the local Lua state. The native adapter owns
    -- the pending per-map choice across that boundary.
    local choice=bridge("selection")
    if mixed and (not M.available() or (choice~="0" and choice~="1")) then return failed() end
    selected = choice == "1"
    if M.available() and choice~="0" and choice~="1" then return failed() end
    if selected and not M.available() then return failed() end
    if bridge("begin:" .. (selected and "1" or "0")) ~= true then
        -- Legacy adapters may still start an ordinary game.
        if mixed or selected or M.available() or G.Network.InNetworkMode() then return failed() end
    end
    active = selected
    return true
end
function M.beforeLoad()
    if bridge("load") ~= true then return false end
    selected, active, setupMap, loading = false, false, nil, true
    restoredMixed=false
    return true
end
function M.restore(jar)
    if not loading then
        if not mixed then return true end
        return restoredMixed and bridge("status")== (active and "1" or "0")
    end
    loading = false
    if mixed then
        -- The native owner admits the complete transferred stream and parsed
        -- host-file NEO state before its original saved start. A recreated Lua
        -- state must observe that restored ledger, never initialize a new one.
        if bridge("restore-network")~=true then return failed() end
        local state=bridge("status")
        if state~="0" and state~="1" then return failed() end
        active=state=="1"
        restoredMixed=true
        return true
    end
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
    if mixed and (mode~="host" or bridge("status")~=(active and "1" or "0")) then return failed() end
    local turn=G.Game.GetCurrentTurn()
    if mixed and (_G.type(turn)~="number" or turn<0 or turn>4294967294 or turn~=_G.math.floor(turn)) then return failed() end
    local payload = bridge("save-snapshot:" .. turn)
    if mixed then
        -- Native export checks the actual world, clock, owner and ledger. This
        -- only puts its exact metadata into the original host's pickle jar;
        -- it does not select/transfer a file or authorize a saved-world load.
        local prefix="NEO2\n"..(turn+1).."\n"..turn.."\n"
        if _G.type(payload)~="string" or #payload>2097152 or payload:find("%z")
            or (active and payload:sub(1,#prefix)~=prefix) or (not active and payload~="NEO0\n") then return failed() end
    elseif _G.type(payload) ~= "string" or #payload > 2097152 then
        if active then return failed() end
        return true
    end
    jar.put(key, {version=1, payload=payload})
    return true
end
function M.finished()
    if mixed then
        restoredMixed=false
        selected, active, setupMap, loading = false, false, nil, false
        return false -- Native end has no qualified mixed lifecycle contract yet.
    end
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
        if slot==game.multiplayerSavegameSlot then
            local accepted=M.beforeLoad()
            if mixed then _G.assert(accepted,"NEO-Logistics mixed load unavailable") end
        else _G.assert(M.beforeStart(), "NEO-Logistics network start rejected") end
        local result=network(slot,...)
        if slot==game.multiplayerSavegameSlot then
            _G.assert(M.restore(jar), "NEO-Logistics multiplayer saved state rejected")
        else _G.assert(bridge("ready")==true, "NEO-Logistics network world start rejected") end
        return result
    end
    game.loadSavegame = function(...)
        local accepted=M.beforeLoad()
        if mixed then _G.assert(accepted,"NEO-Logistics mixed load unavailable") end
        local okay = load(...)
        if okay then _G.assert(M.restore(jar), "NEO-Logistics saved state rejected") end
        return okay
    end
    game.pickle = function(...)
        if mixed then _G.assert(M.save(jar),"NEO-Logistics mixed save unavailable") end
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
        if mixed then return false end
        local okay=saved(slot,name)
        if okay then
            settings.weplayMPResumeSlots[_G.tostring(slot)].neoLogistics=active
            store()
        end
        return okay
    end
end
return M
