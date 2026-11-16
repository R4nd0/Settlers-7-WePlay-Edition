-- Gold host choice. Native ownership and the final peer agreement are required.
local G, Panel, getState, mixed = ...
local Options = G.require("local.ui.optionsGroup")
local create, update, destroy = Panel.create, Panel.update, Panel.destroy
local group, item, syncing, defaultRequested
local function bridge(command)
    if mixed then
        local prefix="weplay-crossplay-neo-v1:"
        if command=="setup-capabilities" then return G.UI.WriteToConsole(prefix.."capabilities") end
        if command=="setup-state" then
            local value=G.UI.WriteToConsole(prefix.."selection")
            local selectable=G.UI.WriteToConsole(prefix.."selectable")=="1"
            if value~="0" and value~="1" and selectable and G.Network.IsHost() and not defaultRequested then
                defaultRequested=true
                G.UI.WriteToConsole(prefix.."stage:0")
                value=G.UI.WriteToConsole(prefix.."selection")
            end
            if value~="0" and value~="1" then return false end
            return (selectable and "selected:" or "locked:")..value
        end
        if command=="setup:0" or command=="setup:1" then
            return G.UI.WriteToConsole(prefix.."stage:"..command:sub(-1))
        end
        return false
    end
    return G.UI.WriteToConsole("weplay-neo-lab-v1:" .. command)
end
local function change(_, checked)
    if syncing or not G.Network.IsHost() or _G.type(checked) ~= "boolean" then return false end
    return bridge("setup:" .. (checked and "1" or "0")) == "1"
end
local function refresh()
    if not group then return end
    local state = bridge("setup-state")
    local kind, value
    if _G.type(state) == "string" then kind, value = state:match("^(%a+):([01])$") end
    local supported = bridge("setup-capabilities") == "NEO-FIFO-INTENT-1"
    syncing = true
    group.setSelectedItem(item, value == "1")
    syncing = false
    G.ui.enable(group.getWidget(item), supported and G.Network.IsHost() and kind == "selected")
end
function Panel.create(...)
    create(...)
    local State = getState()
    if not State.frame then return end
    local width, height = G.system.getScreenSize()
    local size=width<540 and width-40 or 500
    group = Options.create(State.frame, {x=width-size-20,y=height-155}, size, 60, nil, G.font.small,
        Options.groupType.checkBoxGroup, Options.arrangeOrder.heightFirst, 1,
        Options.buttonPlacement.toLeft, change)
    item = group.addItem("NEO-Logistik",
        "FIFO-Transport mit Einzelprioritaeten. Die Auswahl gilt fuer alle Spieler dieser Partie.")
    refresh()
end
function Panel.update(...)
    update(...)
    refresh()
end
-- Pair automation uses the same host action as the visible checkbox.
function Panel.weplaySelectNEO(value)
    if not group or not G.ui.isEnabled(group.getWidget(item)) or _G.type(value) ~= "boolean" then return false end
    local okay = change(item, value)
    refresh()
    return okay
end
function Panel.destroy(...)
    group, item, syncing, defaultRequested = nil, nil, nil, nil
    return destroy(...)
end
