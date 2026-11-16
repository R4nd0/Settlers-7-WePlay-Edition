-- Eligibility for the original Gold SAVE GAME button only. This grants no
-- native save authority and writes no save data, roster or NEO state.
local G=...
return function()
 local game=G.game
 if not game or game.started~=true or game.isDisconnected==true
  or game.exitTriggerTime~=nil or game.lastDesyncTurn~=nil then return false end
 local okay,mode=_G.pcall(G.UI.WriteToConsole,"weplay-crossplay-v1:mode")
 return okay and mode=="host" and G.Network.GetGameMode()==2
  and G.Network.InNetworkMode()==true and G.Network.SessionIsInSession()==true
  and G.Network.IsHost()==true and G.Network.GetLocalPlayerSlot()==1
  and G.Game.InMapMode()==true
end
