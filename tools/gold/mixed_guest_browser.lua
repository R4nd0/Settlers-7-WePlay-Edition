-- Observe the original History browser's completed discovery. Its own callbacks
-- own lastDiscoveryFinished and the idle state; no session or Ready is created.
local G, Panel, State, states = ...
function Panel.weplayCanPrepareMixedGuest()
 if not G.UI or _G.type(G.UI.WriteToConsole)~="function"
  or G.UI.WriteToConsole("weplay-crossplay-v1:mode")~="guest"
  or not State.frame or State.lastDiscoveryFinished~=true
  or State.sessionListState~=states.idle or not G.Network.IsLoggedIn()
  or G.Network.SessionIsInSession() then return false end
 local discovery=G.Network.SessionGetDiscovery(false)
 return _G.type(discovery)=="table" and discovery.State==2
end
