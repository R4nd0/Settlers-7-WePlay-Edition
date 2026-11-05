local G,Panel,getState=...
local M=G.require("local.tools.settings").weplayNeoLogistics
local Options=G.require("local.ui.optionsGroup")
local create,update,destroy=Panel.create,Panel.update,Panel.destroy
local group,item,map
local function refresh()
    local current=G.mapBrowser.filename
    if current~=map then
        map=current
        group.setSelectedItem(item,M.setup(map))
    end
end
function Panel.create(...)
    create(...)
    local State=getState()
    if not State.optionsFrame then return end
    group=Options.create(State.optionsFrame,{x=630,y=950},370,60,nil,G.font.small,
        Options.groupType.checkBoxGroup,Options.arrangeOrder.heightFirst,1,
        Options.buttonPlacement.toLeft,function(_,checked) M.select(checked) end)
    item=group.addItem("NEO-Logistics",
        "FIFO-Transport ohne Gruppenprioritaeten. Einzelprioritaeten bleiben aktiv. Alle Mitspieler brauchen die passende WePlay-Version.")
    map=nil;refresh()
    local supported=M.available() and not G.Network.IsQuickMatch()
    G.ui.enable(group.getWidget(item),supported)
    if not supported then group.setSelectedItem(item,false) end
end
function Panel.update(...)
    update(...)
    if group then refresh() end
end
function Panel.destroy(...)
    group,item,map=nil,nil,nil
    return destroy(...)
end
