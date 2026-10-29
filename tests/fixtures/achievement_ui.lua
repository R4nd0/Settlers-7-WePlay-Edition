-- Invented globals for read-only execution of the fingerprinted UI modules.
-- There is no game process, network transport, profile or filesystem backend.
local function dummy()
 return setmetatable({}, {__index=function(t,key)local value=dummy();rawset(t,key,value);return value end,
                         __call=function()return nil end})
end
Test={popupCalls=0,ticks=0}
require=function()return dummy()end
Data={Achievements=setmetatable({}, {__index=function(_,key)return key end})}
iconFactory={getTextureInGrid=function()return {}end}
defaultPriorities={preGameSites=1,messageBox=2}
ui={createModalFrame=function()Test.popupCalls=Test.popupCalls+1;error('popup_created')end}
system={getScreenSize=function()return 1280,720 end}
desktop={overlayFrame={},cutsceneFrame={},update=function()end}
state={}
setmetatable(_G, {__index=function(t,key)local value=dummy();rawset(t,key,value);return value end})
