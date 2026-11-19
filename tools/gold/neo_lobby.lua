-- Gold host choice. Native ownership and the final peer agreement are required.
local G, Panel, getState, mixed = ...
local Options = G.require("local.ui.optionsGroup")
local create, update, destroy = Panel.create, Panel.update, Panel.destroy
local group, item, syncing, defaultRequested
local teamsRequested, startReady, savedSource
local mode = G.UI.WriteToConsole("weplay-crossplay-v1:mode")
if mixed == nil then
    mixed = mode == "host" or mode == "guest" or mode == "room-host" or mode == "room-guest"
end
local function syncRoomStart()
    if mode ~= "room-host" then return end
    local state = getState()
    if not state or not state.startButton or not G.Network.IsHost() then return end
    local prefix = "weplay-crossplay-v1:"
    local saved = G.UI.WriteToConsole(prefix .. "room-saved-state")
    if saved == "selection-available" and savedSource then
        local source = savedSource
        savedSource = nil
        local result = G.Network.ChangeSavegame(source)
        if not result or result == "" then
            G.Network.SessionLeave()
            G.ui.messageBox("WePlay", "Der Mehrspieler-Spielstand konnte nicht geladen werden.")
        end
        return
    end
    local ready = saved == "ready-verified"
    if saved == "waiting" then
        if not teamsRequested and G.UI.WriteToConsole(prefix .. "room-all-ready") == "1" then
            teamsRequested = G.UI.WriteToConsole(prefix .. "room-sync-teams") == "1"
        end
        ready = G.UI.WriteToConsole(prefix .. "room-teams-confirmed") == "1"
    end
    if not ready then
        G.ui.enable(state.startButton, false)
    elseif not startReady then
        Panel.updatePlayerSlots()
    end
    startReady = ready
end
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
    syncRoomStart()
end
-- Pair automation uses the same host action as the visible checkbox.
function Panel.weplaySelectNEO(value)
    if not group or not G.ui.isEnabled(group.getWidget(item)) or _G.type(value) ~= "boolean" then return false end
    local okay = change(item, value)
    refresh()
    return okay
end
function Panel.weplayPrepareSavedRoom(source)
    if mode ~= "room-host" or savedSource or _G.type(source) ~= "string" or source == "" then return false end
    savedSource = source
    return true
end
function Panel.destroy(...)
    group, item, syncing, defaultRequested = nil, nil, nil, nil
    teamsRequested, startReady, savedSource = nil, nil, nil
    return destroy(...)
end
