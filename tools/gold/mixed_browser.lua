-- Export the original Gold browser's creation callback for an owned mixed host.
-- Discovery completion and the selected network filter remain original UI state.
local G, Panel, State, states, create = ...
local function allowed()
 if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return false end
 local mode=G.UI.WriteToConsole("weplay-crossplay-v1:mode")
 if mode~="host" or not State.frame or State.lastDiscoveryFinished~=true
  or State.sessionListState~=states.idle or not G.Network.IsLoggedIn()
  or G.Network.SessionIsInSession() then return false end
 local selected=State.sessionsFilterList and State.sessionsFilterList.getSelectedItemData()
 if _G.type(selected)~="table" or _G.type(selected.lan)~="boolean" then return false end
 local discovery=G.Network.SessionGetDiscovery(false)
 return _G.type(discovery)=="table" and discovery.State==2
end
function Panel.weplayCanOpenMixedCreation() return allowed() end
function Panel.weplayOpenMixedCreation()
 if not allowed() then return false end
 create()
 return true
end
