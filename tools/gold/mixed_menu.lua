-- One-shot menu routing for an explicitly admitted native mixed launch.
-- The native host owns every map/save/AI/castle/Ready/start choice. Read-only
-- console calls and original engine jobs must remain direct and yieldable.
local G, State, Pages, actions = ...
local function query(name)
 if not G.UI or _G.type(G.UI.WriteToConsole)~="function" then return nil end
 return G.UI.WriteToConsole("weplay-crossplay-v1:"..name)
end
local mode=query("mode")
local active=mode=="host" or mode=="guest"
local phase=active and "waiting" or "inactive"
local finished,busy,requested,prepared=not active,false,false,false
local hostChoices=nil
local nativePhases={pending=true,["local-lobby"]=true,connecting=true,
 admitting=true,["roster-bound"]=true,["setup-bound"]=true}
local function fail()
 finished=true;busy=false;phase="failed"
end
local function mapPath(value)
 if _G.type(value)~="string" or #value<1 or #value>255
  or value:sub(1,1)=="/" or value:sub(1,1)=="\\" or value:find(":",1,true) then return false end
 for i=1,#value do
  local byte=value:byte(i)
  if byte<32 or byte>126 then return false end
 end
 local components=value:gsub("\\","/")
 if components:sub(-1)=="/" or components:find("//",1,true) then return false end
 for part in components:gmatch("[^/]+") do
  if part=="." or part==".." then return false end
 end
 return true
end
local function created(page)
 return page and State.currentPageModule==page and page.isCreated()
end
local function step()
 local native=query("phase")
 if native=="closed" or native=="failed" then finished=true;phase=native;return end
 if not nativePhases[native] then fail();return end
 if not G.Network.IsLoggedIn() then phase="waiting-login";return end
 if mode=="host" then
  if G.Network.SessionIsInSession() then finished=true;phase="host-session-present";return end
  if created(Pages.Creation) then finished=true;phase="host-creation-open";return end
  if created(Pages.Main) and not requested then
   requested=true;phase="browser-requested"
   actions.open_browser()
  elseif created(Pages.Browser) and actions.can_open_host_creation() then
   -- This callback opens the original dialog and retains its actual selected
   -- network filter. It does not choose or create a session on the user's behalf.
   finished=true;phase="host-creation-open"
   actions.open_host_creation()
  end
  return
 end
 if not prepared then
  if G.Network.SessionIsInSession() then fail();return end
  if not created(Pages.Main) then phase="waiting-menu";return end
  local map=query("bootstrap-map")
  if map==false or map==nil then phase="waiting-host-context";return end
  if not mapPath(map) then fail();return end
  local flags=query("bootstrap-flags")
  local template=query("bootstrap-template")
  if flags==false or flags==nil or template==false or template==nil then
   phase="waiting-host-context";return
  end
  if _G.type(flags)~="string" or not flags:match("^[0-7]$")
   or _G.type(template)~="string" or #template>255 then fail();return end
  for i=1,#template do
   local byte=template:byte(i)
   if byte<32 or byte>126 then fail();return end
  end
  local bits=_G.tonumber(flags)
  hostChoices={fixedPositions=bits%2==1,fixedTeams=_G.math.floor(bits/2)%2==1,
   coop=_G.math.floor(bits/4)%2==1,template=template~="" and template or nil}
  -- Latch before entering the original, potentially yielding engine job. A
  -- nested frame must not duplicate map loading or local session preparation.
  prepared=true;phase="guest-preparing"
  local success=actions.prepare_guest_map(map,hostChoices)
  if success~=true then fail();return end
  phase="guest-prepared"
  return
 end
 if native=="setup-bound" and G.Network.SessionIsInSession()
  and not G.Network.IsHost() and G.Network.GetLocalPlayerSlot()==2 then
  finished=true;phase="guest-lobby-requested"
  actions.open_guest_lobby(hostChoices)
 end
end
local function tick()
 if finished or busy then return end
 busy=true
 step()
 busy=false
end
return {tick=tick,state=function()return phase end,active=function()return active end}
