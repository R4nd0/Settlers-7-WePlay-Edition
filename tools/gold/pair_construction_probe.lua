-- Opt-in laboratory action: normal, payable storehouse and connecting street.
-- Requires the authenticated native pair adapter; never enabled by a launcher.
local G = ...
local attempted, initialized, scanned = false, false, 0
local group, cx, cy, cz, placedX, placedY, placedWorldX, placedWorldY, placedWorldZ
local function log(text)
    G.UI.WriteToConsole("WEPLAY_PAIR_OBSERVATION_V1 gameplay="..text)
    G.UI.WriteToLog("WEPLAY_PAIR_OBSERVATION_V1 gameplay="..text.."\n")
end
local roadTried, roadFramed, roadFramedAt = false, false, 0
local function tryRoad()
    if roadTried or not placedX or not G.Game.InMapMode()
        or G.Game.GetCurrentTurn() < (G.Network.IsHost() and 90 or 120) then return end
    local category, id = G.Buildings.GetCastleOfPlayer(G.GUI.GetCurrentPlayer())
    local targetCategory, targetId = G.Buildings.GetReachableStorehouse(category,id)
    local exists, x, y, z = G.GFX.GetEntityPosition(targetCategory,targetId)
    if not exists then roadTried=true;log("road-rejected-connected-target");return end
    if not placedWorldX then roadTried=true;log("road-rejected-missing-original-preview");return end
    if not roadFramed then
        roadFramed=true;roadFramedAt=G.Game.GetRunTime()
        G.focalizer.setLookAt((placedWorldX+x)/2,(placedWorldY+y)/2)
        G.camera.immediateUpdate()
        log("road-original-camera-framed-endpoints")
        return
    end
    if G.Game.GetRunTime()-roadFramedAt<250 then return end
    -- The camera can move between building placement and road placement.
    -- Reproject both original world points, including when the target was visible.
    local visible, tx, ty = G.UI.GetBaseCoordinatesFromWorldPosition(x,y,z)
    local startVisible, sx, sy = G.UI.GetBaseCoordinatesFromWorldPosition(
        placedWorldX,placedWorldY,placedWorldZ)
    if not visible or not startVisible then
        roadTried=true;log("road-rejected-projection-after-framing");return
    end
    roadTried=true
    G.Game.PlaceStreetHelper_StartStreet(G.Data.Groups.NormalStreet,sx,sy)
    G.Game.PlaceStreetHelper_SetSnapToStreetDistance(300)
    G.Game.PlaceStreetHelper_SetSnapToNodeDistance(100)
    if G.Game.PlaceStreetHelper_EndStreet(tx,ty) then
        local result=G.Game.PlaceStreetHelper_Place()
        log("original-street-place-result-".._G.tostring(result))
    else log("original-street-preview-rejected") end
    G.Game.PlaceStreetHelper_Stop()
end

return function()
    if attempted then tryRoad();return end
    if not G.Game.InMapMode()
        or G.Game.GetCurrentTurn() < (G.Network.IsHost() and 30 or 60) then return end
    if not initialized then
        initialized=true
        local player=G.GUI.GetCurrentPlayer()
        group=G.Data.Groups.Storehouse
        local category,id=G.Buildings.GetCastleOfPlayer(player)
        local exists
        exists,cx,cy,cz=G.GFX.GetEntityPosition(category,id)
        if player~=(G.Network.IsHost() and 1 or 2) or not group or group==0 or not exists then
            attempted=true;log("rejected-host-castle-or-group");return
        end
        G.GUI.BuildModeSetImmediate(false)
        G.Game.PlaceBuildingHelper_SetEntityGroup(group)
        G.Game.PlaceBuildingHelper_SetSnapToStreetDistance(0)
        G.Game.PlaceBuildingHelper_SetRotation(0)
        log("initialized-original-normal-build-helper")
    end
    for budget=1,4 do
        if scanned>=128 then
            attempted=true;G.Game.PlaceBuildingHelper_SetEntityGroup(0)
            log("no-valid-payable-placement-in-bounded-search");return
        end
        local radius=900+_G.math.floor(scanned/16)*500
        local angle=(scanned%16)*_G.math.pi/8
        scanned=scanned+1
        local visible,x,y=G.UI.GetBaseCoordinatesFromWorldPosition(
            cx+radius*_G.math.cos(angle),cy+radius*_G.math.sin(angle),cz)
        if visible and x>=0 and x<=1920 and y>=0 and y<=1080 then
            G.Game.PlaceBuildingHelper_SetPosition(x,y)
            local valid,feedback=G.Game.PlaceBuildingHelper_IsValid()
            if valid==true and feedback==0 then
                attempted=true -- Prevent a duplicate command on native reentry.
                placedX,placedY=x,y
                local preview,wx,wy,wz=G.Game.PlaceBuildingHelper_GetPreviewInformation()
                if preview==true then placedWorldX,placedWorldY,placedWorldZ=wx,wy,wz end
                local placed=G.Game.PlaceBuildingHelper_Place()
                G.Game.PlaceBuildingHelper_SetEntityGroup(0)
                log("original-place-result-".._G.tostring(placed).."-turn-"..
                    _G.tostring(G.Game.GetCurrentTurn()).."-candidate-".._G.tostring(scanned))
                return
            end
        end
    end
end
