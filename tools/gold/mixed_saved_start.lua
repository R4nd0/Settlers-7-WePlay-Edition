-- The original SessionStarted event has an empty save-name field after a
-- native saved-cache admission. Route its UI callback using the admitted start,
-- so the original boot/game loader consumes MPSave instead of creating a map.
local G,slot=...
if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return slot end
local mode=G.UI.WriteToConsole("weplay-crossplay-v1:mode")
if mode~="host" and mode~="guest" and mode~="room-host" and mode~="room-guest" then return slot end
local saved=G.UI.WriteToConsole("weplay-crossplay-v1:saved-start")
_G.assert(saved=="0" or saved=="1","WePlay original start context unavailable")
if saved=="1" then
    local original=G.game and G.game.multiplayerSavegameSlot
    _G.assert(_G.type(original)=="number" and original>=0 and original<=4294967295
        and original==_G.math.floor(original),"WePlay original multiplayer save slot unavailable")
    return original
end
return slot
