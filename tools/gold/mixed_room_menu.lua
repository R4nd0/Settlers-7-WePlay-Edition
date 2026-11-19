-- Open the original browser and prepare the native local side of a room guest.
-- Host choices, Ready, and Start stay in the original lobby controls.
local G, State, mode, gold = ...
local function query(name) return G.UI.WriteToConsole("weplay-crossplay-v1:" .. name) end
local function page(name) return G.require("local.menu.panel.menu." .. name) end
local main = page("sectionMenus.mainMenu")
local browser = page("play.multiplayer.sessionList")
local requested, prepared, presented, busy = false, false, false, false
local function callbacks()
    return State.actionManager.createCallbacks(State.currentPageModule)
end
local function mapKey(path)
    if _G.type(path) == "string" then return path:gsub("\\", "/"):lower() end
end
local function step()
    if query("phase") == "failed" or not G.Network.IsLoggedIn() then return end
    if State.currentPageModule == main and not requested then
        requested = true
        callbacks().toSessionList()
        return
    end
    if mode == "room-host" then return end
    if not prepared then
        if State.currentPageModule ~= browser or not browser.weplayCanPrepareMixedGuest() then return end
        local map = query("room-map")
        local count = _G.tonumber(query("room-players"))
        if not mapKey(map) or not count or count % 1 ~= 0 or count < 2 or count > 8 then return end
        prepared = true
        G.map.loadSetupMapConfig(map, nil)
        local current = G.MapSetup.GetCurrentMapData()
        if not G.MapSetup.IsValid() or _G.type(current) ~= "table" or mapKey(current.FilePath) ~= mapKey(map) then
            G.ui.messageBox("WePlay", "Die Karte dieses Raums konnte nicht geladen werden.")
            return
        end
        local created
        if gold then
            created = G.Network.SessionCreate(map, false, 2, false, false, nil, false)
        else
            G.Network.SetIsPrivate(false)
            created = G.Network.SessionCreate(map, false, false, count, false, false, nil, false)
        end
        if created ~= true then
            G.ui.messageBox("WePlay", "Die Lobby dieses Raums konnte nicht erstellt werden.")
            return
        end
        G.MapSetup.SetCoopMode(0)
        callbacks().toHostLobby({fixedTeams=false, fixedPositions=false, privateSession=false})
        return
    end
    local saved = query("room-saved-state") == "stream-verified"
    if not (saved or query("room-ready-action") == "1") or not G.Network.SessionIsInSession()
        or G.Network.IsHost() or G.Network.GetLocalPlayerSlot() < 2 then return end
    local actions = callbacks()
    if actions and _G.type(actions.toJoinLobby) == "function" then
        presented = true
        actions.toJoinLobby({fixedTeams=saved, fixedPositions=saved})
    end
end
return function()
    if busy or presented then return end
    busy = true
    step()
    busy = false
end
