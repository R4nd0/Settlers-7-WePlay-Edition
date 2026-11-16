-- Guest presentation from the native applied-setup contract.
-- This never selects Ready, starts a world or uses the
-- bootstrap choices as the guest's applied settings.
local G, State = ...
local busy,opened=false,false
local function query(name)
 if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return nil end
 return G.UI.WriteToConsole("weplay-crossplay-v1:"..name)
end
local function eligible()
 return query("mode")=="guest" and query("phase")=="setup-bound"
  and G.Network.SessionIsInSession()==true and G.Network.IsHost()==false
  and G.Network.GetLocalPlayerSlot()==2
end
local function present()
 if not eligible() then return false end
 local flags,template=query("applied-flags"),query("applied-template")
 if _G.type(flags)~="string" or not flags:match("^[0-7]$")
  or _G.type(template)~="string" or #template>255 then return false end
 for i=1,#template do
  local byte=template:byte(i)
  if byte<32 or byte>126 then return false end
 end
 -- Engine queries may yield. Recheck applied values and current authority
 -- before entering the original page callback.
 if query("applied-flags")~=flags or query("applied-template")~=template
  or not eligible() then return false end
 local callbacks=State.actionManager.createCallbacks(State.currentPageModule)
 if not callbacks or _G.type(callbacks.toJoinLobby)~="function" then return false end
 local bits=_G.tonumber(flags)
 opened=true
 callbacks.toJoinLobby({fixedPositions=bits%2==1,fixedTeams=_G.math.floor(bits/2)%2==1})
 return true
end
return function()
 if busy or opened then return false end
 busy=true
 local success=present()
 busy=false
 return success
end
