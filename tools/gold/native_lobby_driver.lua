-- Opt-in development driver, excluded from normal interfaces and launchers.
-- NOT a qualified unattended runner: real-engine follow-up stalled in the
-- inline native creation job. Use original GUI creation until its lifecycle
-- is corrected; passing Lua tests below model returning engine calls only.
-- Calls the real engine APIs and original menu transition. Never supplies a
-- native success, Ready, start, acknowledgement, turn or checksum result.
local G, State, MainPage, ProfilePage, BrowserPage, transition, actions, options, report = ...
local phase, startedAt, createdAt = "waiting", G.Game.GetRunTime(), nil
local function setPhase(value)
    phase=value
    report(value)
end
local function stopIfDue(now)
    local deadline=createdAt or startedAt
    if now-deadline>=options.duration then
        setPhase("closing")
        State.actionManager.createCallbacks(BrowserPage).shutdown()
        return true
    end
    return false
end
local function tick()
    if phase=="closing" or phase=="creating" then return end
    local now=G.Game.GetRunTime()
    if stopIfDue(now) then return end
    if phase=="created" or phase=="failed" then return end
    if not G.Network.IsLoggedIn() then return end
    if State.currentPageModule==ProfilePage then
        if phase~="profile-closed" then
            setPhase("profile-closed")
            transition({actionType=actions.toMainMenu,noPrevious=true})
        end
        return
    end
    if State.currentPageModule==MainPage then
        if phase~="browser-requested" then
            setPhase("browser-requested")
            transition({actionType=actions.toSessionList,noPrevious=true})
        end
        return
    end
    -- Its original create path selects multiplayer mode and starts discovery.
    -- The original Create button does not require NATReady; that flag gates
    -- quick match. Standalone hosting still uses the real SessionCreate result.
    if State.currentPageModule~=BrowserPage or not BrowserPage.isCreated() then return end
    if G.Network.SessionIsInSession() then setPhase("failed");return end
    -- Latch before a native job pumps callbacks, so reentry cannot create a
    -- second session. Gold and History retain their original argument lists.
    setPhase("creating")
    local okay,success=_G.pcall(function()
        local selected
        -- Original mapListHelper uses one release argument and the engine's
        -- metadata. Filenames need not contain the displayed map name.
        for _,path in _G.ipairs(G.MapSetup.GetSkirmishMaps(G.release)) do
            local data=G.MapSetup.GetMapDataByFilename(path)
            if _G.type(data)=="table" and _G.type(data.Name)=="string"
                and data.Name:lower():find("monhaim",1,true) and not data.Locked then
                if selected or _G.type(data.MapPath)~="string" or data.MapPath=="" then return false end
                selected=data.MapPath
            end
        end
        if not selected then return false end
        G.map.loadSetupMapConfig(selected,nil)
        local result
        if options.gold then
            result=G.Network.SessionCreate(selected,false,2,false,false,nil,false)
        else
            G.Network.SetIsPrivate(false)
            result=G.Network.SessionCreate(selected,false,false,2,false,false,nil,false)
        end
        if result==true then G.MapSetup.SetCoopMode(0) end
        return result
    end)
    if not okay or success~=true then setPhase("failed");return end
    createdAt=G.Game.GetRunTime()
    setPhase("created")
    transition({actionType=actions.toHostLobby,data={fixedTeams=false,fixedPositions=false,privateSession=false}})
end
return {tick=tick,state=function() return phase end}
