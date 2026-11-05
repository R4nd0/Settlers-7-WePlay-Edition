-- Only group backgrounds/dragging are disabled. Task controls remain native.
local G = ...
local M = G.require("local.tools.settings").weplayNeoLogistics
local Panel = G.logisticsSystem
local create, update = Panel.create, Panel.update
local startDrag, stopDrag = Panel.startPanelDragging, Panel.stopPanelDragging
local function apply()
    if not M.active() or not Panel.isCreated() then return end
    for _, group in _G.pairs(Panel.panels) do
        G.ui.enable(group.panelButton, false)
        G.ui.setAlpha(group.icon, 100)
        G.label.setContent(group.rankingLabel, {"-",true}, false)
    end
end
Panel.create = function(...)
    create(...)
    if M.active() and Panel.isCreated() then
        G.label.create(Panel.frame, {x=60,y=28},
            {"NEO-Logistics aktiv: Gruppenprioritaeten deaktiviert",true},
            G.font.small, G.UI.Anchor.Top, G.UI.Anchor.Left, 1150)
    end
    apply()
end
Panel.update = function(...) update(...); apply() end
Panel.startPanelDragging = function(...)
    if not M.active() then return startDrag(...) end
end
Panel.stopPanelDragging = function(...)
    if not M.active() then return stopDrag(...) end
end
