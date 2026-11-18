-- Opt-in four-slot lab preparation, using the original host AI-chooser APIs.
-- Local readback is not a peer acknowledgement or a world-start result.
local G, options, report = ...
local humans = options.human_count==nil and 2 or options.human_count
local phase, busy, profiles, players, requestedAt = "waiting", false, nil, nil, nil
local function fail(reason) phase="failed";report("failed-"..reason);return false end
local function tick()
    if phase=="configured" then return true end
    if phase=="failed" or busy then return false end
    if humans~=2 and humans~=3 then return fail("human-count") end
    if not G.Network.SessionIsInSession() or not G.Network.IsHost()
        or G.Network.GetLocalPlayerSlot()~=1 then return false end
    local states=G.Network.Services.SessionConfig.PlayerSlotState
    if G.Network.GetNbPlayerSlots()~=4 then return fail("four-slots-required") end
    for slot=2,humans do
        local state=G.Network.GetPlayerSlotState(slot)
        if state==states.Open or state==states.Closed then return false end
        if state~=states.Human then return fail(slot==2 and "second-human-required" or "third-human-required") end
    end
    if phase=="requested" then
        local current=G.MapSetup.GetActivePlayers(true)
        if _G.type(current)~="table" or #current~=4 then return fail("player-layout-changed") end
        for slot=1,4 do
            if current[slot]~=players[slot] then return fail("player-layout-changed") end
        end
        local okay=true
        for slot=humans+1,4 do
            okay=okay and G.Network.GetPlayerSlotState(slot)==states.CPU
                and G.MapSetup.IsAIPlayer(players[slot])
                and G.MapSetup.GetProfileIDByPlayerID(players[slot])==profiles[slot-humans]
                and G.MapSetup.GetAIDifficultyLevelByPlayerID(players[slot])==options.difficulty
                and G.MapSetup.GetAIPeacefulFlagByPlayerID(players[slot])==false
        end
        if okay then phase="configured";report("local-config-observed");return true end
        if G.Game.GetRunTime()-requestedAt>=5000 then return fail("original-config-not-consumed") end
        return false
    end
    if options.difficulty~=0 and options.difficulty~=1 and options.difficulty~=2 then
        return fail("difficulty")
    end
    busy=true
    players=G.MapSetup.GetActivePlayers(true)
    if _G.type(players)~="table" or #players~=4 then busy=false;return fail("active-player-layout") end
    local seen={}
    for slot=1,4 do
        local id=players[slot]
        if _G.type(id)~="number" or id%1~=0 or id<1 or id>24 or seen[id]
            or G.MapSetup.IsNeutralPlayer(id) then busy=false;return fail("player-identity") end
        seen[id]=true
        if slot<=humans and G.MapSetup.IsAIPlayer(id) then busy=false;return fail("human-profile") end
        if slot>humans then
            local state=G.Network.GetPlayerSlotState(slot)
            if state~=states.Open and state~=states.Closed and state~=states.CPU then
                busy=false;return fail("ai-slot-occupied")
            end
        end
    end
    profiles={}
    local known={}
    for _,row in _G.ipairs(G.MapSetup.GetAINamesListSorted()) do
        local id=_G.type(row)=="table" and row.profileID
        if (_G.type(id)=="number" and id>0 and id%1==0) or (_G.type(id)=="string" and #id>0 and #id<=128) then
            if not known[id] then
                known[id]=true;profiles[#profiles+1]=id
                if #profiles==4-humans then break end
            end
        end
    end
    if #profiles~=4-humans then
        busy=false;return fail(humans==2 and "two-native-ai-profiles-required" or "one-native-ai-profile-required")
    end
    -- Latch before setters can pump another native update. No protected Lua
    -- call boundary is added around engine jobs and no setter is retried.
    phase,requestedAt="requested",G.Game.GetRunTime()
    report("original-config-requested")
    for slot=humans+1,4 do
        local profile=profiles[slot-humans]
        G.MapSetup.SetProfileID(slot,profile)
        G.MapSetup.SetAIDifficultyLevelByPlayerID(players[slot],options.difficulty)
        G.MapSetup.SetAIPeacefulFlagByPlayerID(players[slot],false)
        G.Network.SetPlayerSlotState(slot,states.CPU)
        G.Network.SetAIPlayerProfileID(slot,profile)
    end
    busy=false
    return false
end
return {tick=tick,state=function()return phase end}
