-- Opt-in development driver, excluded from normal interfaces and launchers.
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
        G.system.exitApi()
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
    if State.currentPageModule~=BrowserPage or not G.Network.IsNATReady() then return end
    if G.Network.SessionIsInSession() then setPhase("failed");return end
    local selected
    for _,path in _G.ipairs(G.MapSetup.GetSkirmishMaps(true,true)) do
        if _G.type(path)=="string" and path:lower():find("monhaim",1,true) then
            if selected then setPhase("failed");return end
            selected=path
        end
    end
    if not selected then setPhase("failed");return end
    -- Latch before a native job pumps callbacks, so reentry cannot create a
    -- second session. Gold and History retain their original argument lists.
    setPhase("creating")
    G.map.loadSetupMapConfig(selected,nil)
    local success
    if options.gold then
        success=G.Network.SessionCreate(selected,false,2,false,false,nil,false)
    else
        G.Network.SetIsPrivate(false)
        success=G.Network.SessionCreate(selected,false,false,2,false,false,nil,false)
    end
    if success~=true then setPhase("failed");return end
    G.MapSetup.SetCoopMode(0)
    createdAt=G.Game.GetRunTime()
    setPhase("created")
    transition({actionType=actions.toHostLobby,data={fixedTeams=false,fixedPositions=false,privateSession=false}})
end
return {tick=tick,state=function() return phase end}
