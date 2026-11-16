-- Original History local preparation after the authenticated Gold bootstrap.
-- Keep both engine jobs yieldable. The literal 2 is the original UI argument;
-- this helper grants no roster, castle, Ready or world-start permission.
local G = ...
return function(map, choices)
 G.map.loadSetupMapConfig(map, choices.template)
 if G.MapSetup.IsValid() ~= true then return false end
 return G.Network.SessionCreate(map, false, false, 2,
  choices.fixedPositions, choices.fixedTeams, choices.template, choices.coop) == true
end
