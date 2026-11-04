-- Original WePlay replacement for the unavailable Ubisoft invitation overlay.
-- Only a nonce and mode cross this bridge. Room credentials stay in the launcher.
local G = ...
return function()
    local raw=G.UI.WriteToConsole("weplay-invitation-read-v1")
    local nonce,mode
    if _G.type(raw)=="string" and #raw<=96 then
        nonce,mode=raw:match("^weplay%-invitation%-ui%-v1|([0-9a-f]+)|([a-z]+)\n$")
    end
    if not nonce or #nonce~=32 then
        G.ui.messageBox("WePlay","Einladung im WePlay-Launcher teilen. / Share the invitation in the WePlay launcher.")
    elseif mode=="lan" then
        G.ui.messageBox("WePlay","Mitspieler waehlen LAN im WePlay-Launcher und treten dieser Partie bei. / Other players choose LAN in WePlay and join this game.")
    elseif mode=="guest" then
        G.ui.messageBox("WePlay","Bitte den Raum-Host um eine Einladung bitten. / Ask the room host to share an invitation.")
    elseif mode=="private" or mode=="public" then
        if G.UI.WriteToConsole("weplay-invitation-open-v1:"..nonce)~=true then
            G.ui.messageBox("WePlay","Einladung im WePlay-Launcher oeffnen. / Open the invitation in the WePlay launcher.")
        end
    else
        G.ui.messageBox("WePlay","Einladungen sind hier nicht verfuegbar. / Invitations are unavailable here.")
    end
end
