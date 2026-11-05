local G,Panel,getState=...
local M=G.require("local.tools.settings").weplayNeoLogistics
local create,update,destroy=Panel.create,Panel.update,Panel.destroy
local label
local function refresh()
    local State=getState()
    local value=M.networkState()
    if label then G.label.setContent(label,{value=="1" and "NEO-Logistics: AN"
        or value=="0" and "NEO-Logistics: AUS" or "NEO-Logistics: Einstellungen werden abgeglichen",true}) end
    if value~="0" and value~="1" then
        if State.startButton then G.ui.enable(State.startButton,false) end
        for _,slot in _G.ipairs(State.slots or {}) do
            if slot.readyButtonID then G.ui.enable(slot.readyButtonID,false) end
        end
    else
        for _,slot in _G.ipairs(State.slots or {}) do
            if slot.readyButtonID then G.ui.enable(slot.readyButtonID,true) end
        end
    end
end
function Panel.create(...)
    create(...)
    local State=getState()
    if State.frame then
        local _,height=G.system.getScreenSize()
        label=G.label.create(State.frame,{x=620,y=height-155},{"",true},
            G.font.small,G.UI.Anchor.Top,G.UI.Anchor.Left,900)
        refresh()
    end
end
function Panel.update(...) update(...);refresh() end
function Panel.destroy(...) label=nil;return destroy(...) end
