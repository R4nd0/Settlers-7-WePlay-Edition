-- Original WePlay extension. The native query is read-only and requires an
-- authenticated roster, matching preparation rows and the actual guest role.
-- This selects the original guest page; it never toggles Ready or starts play.
local G, Panel, State = ...
local update, destroy = Panel.update, Panel.destroy
local queued, waiting, expected = false, false, nil
local function mapKey(path)
    if _G.type(path) ~= "string" or path == "" then return nil end
    return (path:gsub("\\", "/"):lower())
end
local function present()
    -- GetMap reads the configuration event already accepted by the native
    -- consumer. GetMapName reads the separate backend room's old metadata.
    local path = G.Network.GetMap()
    expected = mapKey(path)
    if not expected then return false end
    if G.UI.WriteToConsole("weplay-crossplay-map-reload-begin-v1") ~= true then return false end
    -- Follow the original guest creation path before its minimap reads MapSetup.
    -- The template getter belongs to the native session; no UI selection is used.
    G.map.loadSetupMapConfig(path, G.Network.GetMapForgeTemplateName())
    local current = G.MapSetup.GetCurrentMapData()
    if not G.MapSetup.IsValid() or _G.type(current) ~= "table"
        or mapKey(current.FilePath) ~= expected then return false end
    G.MapSetup.SetCoopMode(G.Network.GetIsCoop() and 1 or 0)
    -- Loading rebuilds the native preparation rows. The adapter must restore
    -- its admitted players and observe the original queue consumers first.
    waiting = true
    return true
end
function Panel.update(...)
    if waiting then
        if not State.frame or not State.callbacks
            or _G.type(State.callbacks.toJoinLobby) ~= "function"
            or not G.Network.SessionIsInSession() or G.Network.IsHost()
            or G.Network.GetLocalPlayerSlot() ~= 2 then waiting=false;return update(...) end
        local current = G.MapSetup.GetCurrentMapData()
        if mapKey(G.Network.GetMap()) ~= expected or not G.MapSetup.IsValid()
            or _G.type(current) ~= "table" or mapKey(current.FilePath) ~= expected then
            waiting=false;return update(...)
        end
        if G.UI.WriteToConsole("weplay-crossplay-map-reload-commit-v1") == true
            and G.UI.WriteToConsole("weplay-crossplay-guest-presentation-v1") == true then
            waiting=false
            State.callbacks.toJoinLobby({fixedTeams=G.Network.GetIsTeams(),fixedPositions=true})
            return
        end
        return update(...)
    end
    if not queued and State.frame and State.callbacks
        and _G.type(State.callbacks.toJoinLobby) == "function"
        and G.Network.SessionIsInSession() and not G.Network.IsHost()
        and G.Network.GetLocalPlayerSlot() == 2
        and G.UI.WriteToConsole("weplay-crossplay-guest-presentation-v1") == true then
        -- Latch before the native loader: reentry or failure must not repeatedly
        -- reload preparation data or open a guest page with a stale map.
        queued = true
        -- Use the original direct engine call path. Its caller owns Lua error
        -- handling; a protected-call wrapper must not intercept native jobs.
        if present() then return end
    end
    return update(...)
end
function Panel.destroy(...)
    queued, waiting, expected = false, false, nil
    return destroy(...)
end
