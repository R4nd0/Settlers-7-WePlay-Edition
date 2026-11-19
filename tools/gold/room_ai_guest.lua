-- Apply the authenticated host's AI settings through the original local
-- preparation API. The native adapter verifies the completed row and catalog.
local G=...
local requested

return function()
    if G.UI.WriteToConsole("weplay-crossplay-v1:mode")~="room-guest"
        or not G.Network.SessionIsInSession() or G.Network.IsHost() then return false end
    local raw=G.UI.WriteToConsole("weplay-crossplay-v1:room-gold-ai-choice")
    if _G.type(raw)~="string" or raw==requested then return false end
    local slot,profile,difficulty,peaceful=raw:match("^([34]),(%d+),([012]),([01])$")
    slot,profile,difficulty=_G.tonumber(slot),_G.tonumber(profile),_G.tonumber(difficulty)
    if not slot or not profile or profile<1 or profile>255 then return false end
    local players=G.MapSetup.GetActivePlayers(true)
    local player=_G.type(players)=="table" and players[slot]
    if _G.type(player)~="number" or player%1~=0 or player<1 or player>24
        or G.MapSetup.IsNeutralPlayer(player) then return false end
    local profileArgument
    for _,candidate in _G.ipairs(G.MapSetup.GetAINamesListSorted()) do
        if _G.type(candidate)=="table" and _G.tonumber(candidate.profileID)==profile then
            profileArgument=candidate.profileID
            break
        end
    end
    if not profileArgument or G.UI.WriteToConsole("weplay-crossplay-v1:room-gold-ai-choice")~=raw then return false end
    requested=raw
    G.MapSetup.SetProfileID(slot,profileArgument)
    G.MapSetup.SetAIDifficultyLevelByPlayerID(player,difficulty)
    G.MapSetup.SetAIPeacefulFlagByPlayerID(player,peaceful=="1")
    return true
end
