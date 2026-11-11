-- Original WePlay extension. The native query is read-only and requires an
-- authenticated roster, matching preparation rows and the actual guest role.
-- This selects the original guest page; it never toggles Ready or starts play.
local G, Panel, State = ...
local update, destroy = Panel.update, Panel.destroy
local queued = false
function Panel.update(...)
    if not queued and State.frame and State.callbacks
        and _G.type(State.callbacks.toJoinLobby) == "function"
        and G.Network.SessionIsInSession() and not G.Network.IsHost()
        and G.Network.GetLocalPlayerSlot() == 2
        and G.UI.WriteToConsole("weplay-crossplay-guest-presentation-v1") == true then
        queued = true
        State.callbacks.toJoinLobby({fixedTeams=G.Network.GetIsTeams(),fixedPositions=true})
        return
    end
    return update(...)
end
function Panel.destroy(...)
    queued = false
    return destroy(...)
end
