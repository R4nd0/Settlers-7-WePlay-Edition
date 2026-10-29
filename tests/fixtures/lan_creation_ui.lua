-- Invented UI/network fixture. No game data or native game functions.
local function dummy()
 return setmetatable({}, {__index=function(t,key) local x=dummy();rawset(t,key,x);return x end,__call=function()end})
end
Test={selected={},widgets={},ids={},nativePrivate={},created=nil,host=nil}
local group={}
function group.addItem(label,description) local id=#Test.ids+1;Test.ids[id]=label;Test.widgets[id]={enabled=true};return id end
function group.setSelectedItem(id,value) Test.selected[id]=value end
function group.isItemSelected(id) return Test.selected[id]==true end
function group.getWidget(id) return Test.widgets[id] end
function group.arrangeItems()end
Test.group=group
local options=dummy();options.create=function()return group end
local common=dummy();common.createCommonMainMenuPanel=function(state)state.frame={} end;common.coopMaps={}
local modules={['local.ui.optionsGroup']=options,['local.menu.panel.menu.commonStatics']=common}
require=function(name) if not modules[name]then modules[name]=dummy()end;return modules[name] end
include={menu=function()end}
ui=dummy();ui.disable=function(widget)widget.enabled=false end;ui.enable=function(widget)widget.enabled=true end;ui.isEnabled=function(widget)return widget.enabled end
frame=dummy();frame.create=function()return {}end
label=dummy();label.create=function()return {}end
button=dummy();button.create=function(parent,position,text,class,callback)if text=='OK'then Test.pressCreate=callback end;return {}end
system={getScreenSize=function()return 1280,1024 end}
xui={templateSelection=dummy()};xui.templateSelection.isCreated=function()return false end
mapBrowser=dummy();mapBrowser.filename='InventedMap';mapBrowser.list.getSelectedIndex=function()return 1 end
mapBrowser.list.getSelectedItemData=function()return {isUnlocked=true,players=4,filename='InventedMap'}end
mapBrowser.list.getItemData=mapBrowser.list.getSelectedItemData
Network=dummy();Network.IsQuickMatch=function()return false end;Network.IsLoggedIn=function()return true end
Network.SetIsPrivate=function(value)table.insert(Test.nativePrivate,value)end
Network.SessionCreate=function(map,isPrivate,legacyLan,slots,fixedPositions,fixedTeams,template,coop)
 Test.created={private=isPrivate,legacyLan=legacyLan};return true
end
Game=dummy();MapSetup=dummy();map=dummy();font=dummy();UI=dummy();desktop=dummy();defaultPriorities=dummy()
Test.callbacks={toHostLobby=function(options)Test.host=options end,toSessionList=function()end,toMainMenu=function()error('Unexpected menu exit')end}
function Test.privateId()for id,name in ipairs(Test.ids)do if name=='Private session'then return id end end;error('Missing private checkbox')end
function Test.tamper()local id=Test.privateId();Test.selected[id]=true;Test.widgets[id].enabled=true end
function Test.assertOff()local id=Test.privateId();assert(Test.selected[id]==false,'Private checkbox selected');assert(Test.widgets[id].enabled==false,'Private checkbox enabled')end
