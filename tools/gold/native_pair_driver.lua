-- Opt-in laboratory automation. Not installed by ordinary launchers.
-- Native jobs must run on the engine's Lua job stack, outside pcall/xpcall:
-- Lua 5.1 cannot yield through those protected C-call boundaries.
-- Hooks activate original, enabled lobby controls. They never supply native
-- Ready/start results, world progress, turns, acknowledgements or checksums.
local G, State, Pages, transition, actions, options, report = ...
local phase, began = "waiting", G.Game.GetRunTime()
local busy, created, readyRequested, startRequested = false, false, false, false
local lastWait
local function waiting(reason)
    if lastWait ~= reason then lastWait = reason; report("waiting-"..reason) end
end
local function change(value)
    phase = value
    report(value)
end
local function tick()
    -- A native job may pump the same update while yielding. Never create or
    -- toggle twice, or destroy its owning menu while that job is suspended.
    if busy or phase == "closing" then return end
    if G.Game.GetRunTime() - began >= options.duration then
        change("closing")
        State.actionManager.createCallbacks(Pages.Browser).shutdown()
        return
    end
    if phase:sub(1,6) == "failed" then return end
    if not G.Network.IsLoggedIn() then waiting("login"); return end
    if not created then
        if State.currentPageModule == Pages.Profile then
            if phase ~= "profile-closed" then
                change("profile-closed")
                transition({actionType=actions.toMainMenu,noPrevious=true})
            end
            return
        end
        if State.currentPageModule == Pages.Main then
            if phase ~= "browser-requested" then
                change("browser-requested")
                transition({actionType=actions.toSessionList,noPrevious=true})
            else
                waiting("main")
            end
            return
        end
        if State.currentPageModule ~= Pages.Browser then waiting("page"); return end
        if not Pages.Browser.isCreated() then waiting("browser"); return end
        if G.Network.SessionIsInSession() then change("failed-existing-session"); return end
        -- Login can precede the original discovery job's completion. In that
        -- window the native create guard correctly rejects the request. Wait
        -- for the browser's actual completion event and consumed result.
        if not Pages.Browser.weplayCanAutoCreate() then waiting("discovery"); return end
        -- Discovery can also enter native metadata/file jobs. Guard it before
        -- the first call, not only the later map loader and session creator.
        busy = true
        change("map-discovery")
        local selected
        -- The original mapBrowser.collectMaps uses a literal true in all three
        -- editions. Do not depend on a separate, potentially absent global.
        for _, path in _G.ipairs(G.MapSetup.GetSkirmishMaps(true)) do
            local data = G.MapSetup.GetMapDataByFilename(path)
            if _G.type(data) == "table" and _G.type(data.Name) == "string"
                and data.Name:lower():find("monhaim",1,true) and not data.Locked then
                if selected or _G.type(data.MapPath) ~= "string" or data.MapPath == "" then
                    busy = false; change("failed-map-ambiguous"); return
                end
                selected = data.MapPath
            end
        end
        if not selected then busy = false; change("failed-map-missing"); return end
        change("map-loading")
        G.map.loadSetupMapConfig(selected,nil)
        local current = G.MapSetup.GetCurrentMapData()
        local function key(path)
            return _G.type(path) == "string" and (path:gsub("\\","/"):lower()) or nil
        end
        if not G.MapSetup.IsValid() or _G.type(current) ~= "table"
            or key(current.FilePath) ~= key(selected) then
            busy = false; change("failed-map-not-loaded"); return
        end
        change("session-creating")
        local result
        if options.gold then
            result = G.Network.SessionCreate(selected,false,2,false,false,nil,false)
        else
            G.Network.SetIsPrivate(false)
            result = G.Network.SessionCreate(selected,false,false,2,false,false,nil,false)
        end
        if result ~= true then busy = false; change("failed-native-create"); return end
        G.MapSetup.SetCoopMode(0)
        created = true
        change("created")
        transition({actionType=actions.toHostLobby,
            data={fixedTeams=false,fixedPositions=false,privateSession=false}})
        busy = false
        return
    end
    if not G.Network.SessionIsInSession() then return end
    if options.host then
        if startRequested or State.currentPageModule ~= Pages.Host or not Pages.Host.isCreated()
            or not G.Network.IsHost() or not Pages.Host.weplayCanAutoStart() then return end
        -- Latch before invoking the original button callback, which can yield.
        startRequested, busy = true, true
        change("start-requested")
        Pages.Host.weplayAutoStart()
        busy = false
    else
        if readyRequested or State.currentPageModule ~= Pages.Guest or not Pages.Guest.isCreated()
            or G.Network.IsHost() or G.Network.GetLocalPlayerSlot() ~= 2
            or not Pages.Guest.weplayCanAutoReady() then return end
        readyRequested, busy = true, true
        change("ready-requested")
        Pages.Guest.weplayAutoReady()
        busy = false
    end
end
return {tick=tick,state=function() return phase end}
