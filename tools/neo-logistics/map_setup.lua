local G, Panel, getState = ...
local M = G.require("local.tools.settings").weplayNeoLogistics
local Options = G.require("local.ui.optionsGroup")
local create, destroy = Panel.create, Panel.destroy
local group, item
function Panel.create(...)
    create(...)
    local State = getState()
    if not State.frame then return end
    local width, height = G.system.getScreenSize()
    local map = G.MapSetup.GetCurrentMapData().FilePath
    local function changed(_, checked)
        M.select(checked)
    end
    group = Options.create(State.frame, {x=780,y=height-330}, width-1110, 90,
        nil, G.font.small, Options.groupType.checkBoxGroup,
        Options.arrangeOrder.heightFirst, 1, Options.buttonPlacement.toLeft, changed)
    item = group.addItem("NEO-Logistics",
        "Warentransport nach Auftragsalter. Gruppenprioritaeten haben keine Wirkung. Ausdrueckliche Einzelprioritaeten von Spieler und KI bleiben erhalten.")
    group.setSelectedItem(item, M.setup(map))
    local supported = M.available()
    G.ui.enable(group.getMainFrame(), supported)
    G.label.create(State.frame, {x=820,y=height-282},
        {supported and "FIFO-Transport; Einzelprioritaeten bleiben aktiv."
            or "Fuer diesen Spielmodus nicht verfuegbar.", true},
        G.font.tiny, G.UI.Anchor.Top, G.UI.Anchor.Left, width-1150)
end
function Panel.destroy(...)
    group, item = nil, nil
    return destroy(...)
end
