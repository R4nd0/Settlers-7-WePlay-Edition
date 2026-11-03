-- WePlay-owned roster metadata. Injected into the existing settings module.
-- Arguments are the game's globals, settings data and existing settings writer.
-- This module does not start worlds, acknowledge transfers or change simulation.
local G, settings, store = ...
local M = {cacheKey = "weplay_mp_resume_v1"}
local N, A, Game = G.Network, G.MapSetup, G.Game

local function integer(value, minimum, maximum)
    return type(value) == "number" and value >= minimum and value <= maximum
        and value == math.floor(value)
end
local function text(value, maximum, empty)
    return type(value) == "string" and #value <= maximum and (empty or #value > 0)
        and not value:find("[%z\1-\31\127]")
end
local function mapKey(value)
    if not text(value, 512, false) then return nil end
    value = value:gsub("\\", "/"):lower()
    if value:sub(1, 5) ~= "maps/" or value:find("//", 1, true)
        or value:find("/../", 1, true) or value:find("/./", 1, true)
        or value:find(":", 1, true) then return nil end
    for part in value:gmatch("[^/]+") do if part == "." or part == ".." then return nil end end
    return value
end
local function valid(record)
    if type(record) ~= "table" or not mapKey(record.map)
        or not text(record.template, 256, true) or not text(record.name, 128, true)
        or not integer(record.coop, 0, 1) or type(record.players) ~= "table"
        or not integer(#record.players, 2, 4) then return false end
    local positions, colors, humans = {}, {}, 0
    for i, player in ipairs(record.players) do
        if type(player) ~= "table" or not integer(player.role, 0, 2)
            or not integer(player.team, 1, 4) or not integer(player.position, 1, #record.players)
            or not integer(player.color, 0, 31) or not integer(player.profile, 0, 4294967295)
            or not integer(player.difficulty, 0, 2) or not integer(player.peaceful, 0, 1) then return false end
        if player.role == 1 then humans = humans + 1 end
        if player.role ~= 0 then
            if positions[player.position] or colors[player.color] then return false end
            positions[player.position], colors[player.color] = true, true
        end
        if player.role ~= 2 and (player.profile ~= 0 or player.difficulty ~= 0 or player.peaceful ~= 0) then return false end
        if i == 1 and player.role ~= 1 then return false end
    end
    return humans >= 2
end
local function hex(value)
    return (value:gsub(".", function(c) return string.format("%02x", string.byte(c)) end))
end
local function unhex(value, maximum)
    if #value > maximum * 2 or #value % 2 ~= 0 or value:find("[^0-9a-f]") then return nil end
    return (value:gsub("..", function(c) return string.char(tonumber(c, 16)) end))
end
local fields = {"role", "team", "position", "color", "profile", "difficulty", "peaceful"}
function M.encode(record)
    if not valid(record) then return nil end
    local lines = {"WPMPR1", hex(record.map), hex(record.template), tostring(record.coop), hex(record.name)}
    for _, player in ipairs(record.players) do
        local row = {}
        for _, key in ipairs(fields) do row[#row + 1] = tostring(player[key]) end
        lines[#lines + 1] = table.concat(row, ",")
    end
    return table.concat(lines, "\n")
end
function M.decode(value)
    if type(value) ~= "string" or #value > 4096 or value:find("\r", 1, true) then return nil end
    local lines = {}
    for line in (value .. "\n"):gmatch("(.-)\n") do lines[#lines + 1] = line end
    if #lines < 7 or #lines > 9 or lines[1] ~= "WPMPR1" then return nil end
    local record = {map = unhex(lines[2], 512), template = unhex(lines[3], 256),
        coop = tonumber(lines[4]), name = unhex(lines[5], 128), players = {}}
    if lines[4] ~= "0" and lines[4] ~= "1" then return nil end
    for i = 6, #lines do
        local player, count = {}, 0
        for number in (lines[i] .. ","):gmatch("(.-),") do
            count = count + 1
            if count > #fields or not number:match("^%d+$") then return nil end
            local converted = tonumber(number)
            if not converted or tostring(converted) ~= number then return nil end
            player[fields[count]] = converted
        end
        if count ~= #fields then return nil end
        record.players[#record.players + 1] = player
    end
    return valid(record) and record or nil
end
local function host()
    return N.InNetworkMode() and N.IsHost() and N.GetGameMode() == 2 and N.GetLocalPlayerSlot() == 1
end
local function slotValid(slot)
    --1001 is the native autosave.1000 belongs to the received MP world.
    return integer(slot, 1001, 1009)
end
local autosaveDue, autosaveLast
function M.resetAutosave()
    autosaveDue, autosaveLast = nil, nil
end
function M.tickHostAutosave(now, enabled, save)
    if not host() or not N.SessionIsInSession() or enabled ~= true
        or type(save) ~= "function" or type(now) ~= "number"
        or not (now >= 0 and now <= 4294967295) then
        M.resetAutosave()
        return false
    end
    -- The native game clock includes fractional frame interpolation.
    now = math.floor(now)
    if autosaveLast == nil or now < autosaveLast then autosaveDue = now + 300000 end
    autosaveLast = now
    if now < autosaveDue then return false end
    -- Advance before native saving: no catch-up burst or per-frame retry on failure.
    autosaveDue = now + 300000
    save()
    return true
end
local function activePlayers(count)
    local active = A.GetActivePlayers(true)
    if type(active) ~= "table" then return nil end
    for i = 1, count do if not integer(active[i], 0, 24) then return nil end end
    return active
end
function M.captureLobby()
    if not host() then return nil end
    local count = N.GetNbPlayerSlots()
    if not integer(count, 2, 4) then return nil end
    local active, map = activePlayers(count), A.GetCurrentMapData()
    if not active or type(map) ~= "table" then return nil end
    local record = {map = map.FilePath, template = N.GetMapForgeTemplateName() or "",
        coop = N.GetIsCoop() and 1 or 0, name = "", players = {}}
    local states = N.Services.SessionConfig.PlayerSlotState
    for i = 1, count do
        local state = N.GetPlayerSlotState(i)
        local role = state == states.Human and 1 or state == states.CPU and 2 or state == states.Closed and 0 or nil
        if role == nil then return nil end
        record.players[i] = {role = role, team = N.GetTeamID(i), position = N.GetStartPosition(i),
            color = N.GetPlayerColor(i), profile = role == 2 and A.GetProfileIDByPlayerID(active[i]) or 0,
            difficulty = role == 2 and A.GetAIDifficultyLevelByPlayerID(active[i]) or 0,
            peaceful = role == 2 and A.GetAIPeacefulFlagByPlayerID(active[i]) and 1 or 0}
    end
    return valid(record) and record or nil
end
function M.rememberLobby()
    local encoded = M.encode(M.captureLobby())
    settings.weplayMPResumeCurrent = encoded
    store()
    return encoded ~= nil
end
local function saveName(slot, name)
    if slot == 1001 and name == nil then return "" end
    return name
end
function M.cacheSave(slot, name)
    name = saveName(slot, name)
    -- Native automatic saves intentionally have an empty name.
    if not host() or not slotValid(slot) or not text(name, 128, slot == 1001) then return false end
    local record = M.decode(settings.weplayMPResumeCurrent)
    local map = Game.GetCurrentMapData()
    if not record or type(map) ~= "table" or mapKey(map.FilePath) ~= mapKey(record.map) then return false end
    record.name = name
    local encoded = M.encode(record)
    Game.CacheSaveGameValue(M.cacheKey, encoded)
    return true
end
local function entryFor(slot)
    for _, entry in pairs(Game.GetSaveGames(slot, 1)) do
        if entry.slot == slot then return entry end
    end
end
function M.saved(slot, name)
    if not host() or not slotValid(slot) or not Game.SaveGameExists(slot) then return false end
    name = saveName(slot, name)
    local record, entry = M.decode(settings.weplayMPResumeCurrent), entryFor(slot)
    if not record or not entry or saveName(slot, entry.name) ~= name
        or not text(name, 128, slot == 1001) or not text(entry.date, 128, false) then return false end
    record.name = name
    local encoded = M.encode(record)
    if not encoded then return false end
    if type(settings.weplayMPResumeSlots) ~= "table" then settings.weplayMPResumeSlots = {} end
    settings.weplayMPResumeSlots[tostring(slot)] = {metadata = encoded, date = entry.date, map = entry.map}
    store()
    return true
end
function M.lookup(slot)
    if not slotValid(slot) or type(settings.weplayMPResumeSlots) ~= "table" then return nil end
    local saved, entry = settings.weplayMPResumeSlots[tostring(slot)], entryFor(slot)
    if type(saved) ~= "table" or not entry or entry.date ~= saved.date or entry.map ~= saved.map then return nil end
    local record = M.decode(saved.metadata)
    if not record or record.name ~= saveName(slot, entry.name)
        or mapKey(A.GetMapPathBySavegame(slot)) ~= mapKey(record.map) then return nil end
    return record
end
function M.loadedRoster()
    if not N.InNetworkMode() or not Game.IsSaveGameCacheKey(M.cacheKey) then return nil end
    local record = M.decode(Game.GetCachedSaveGameValue(M.cacheKey))
    local map = Game.GetCurrentMapData()
    if not record or type(map) ~= "table" or mapKey(map.FilePath) ~= mapKey(record.map) then return nil end
    return record
end
function M.applyRoster(record)
    if not host() or not valid(record) or N.GetNbPlayerSlots() ~= #record.players then return false end
    local active, states = activePlayers(#record.players), N.Services.SessionConfig.PlayerSlotState
    if not active then return false end
    for i, player in ipairs(record.players) do
        if player.role ~= 1 and N.GetPlayerSlotState(i) == states.Human then return false end
    end
    A.SetCoopMode(record.coop)
    for i, player in ipairs(record.players) do
        if player.role == 2 then
            A.SetProfileID(i, player.profile)
            A.SetAIDifficultyLevelByPlayerID(active[i], player.difficulty)
            A.SetAIPeacefulFlagByPlayerID(active[i], player.peaceful == 1)
            N.SetPlayerSlotState(i, states.CPU)
            N.SetAIPlayerProfileID(i, player.profile)
        elseif player.role == 0 then N.SetPlayerSlotState(i, states.Closed)
        elseif N.GetPlayerSlotState(i) ~= states.Human then N.SetPlayerSlotState(i, states.Open) end
        N.SetTeamID(i, player.team)
        N.SetStartPosition(i, player.position)
        N.SetPlayerColor(i, player.color)
    end
    settings.weplayMPResumeCurrent = M.encode(record)
    store()
    return true
end
function M.ready(record)
    if not host() or not valid(record) or N.GetNbPlayerSlots() ~= #record.players then return false end
    local states = N.Services.SessionConfig.PlayerSlotState
    for i, player in ipairs(record.players) do
        local state = player.role == 1 and states.Human or player.role == 2 and states.CPU or states.Closed
        if N.GetPlayerSlotState(i) ~= state or N.GetTeamID(i) ~= player.team
            or N.GetStartPosition(i) ~= player.position or N.GetPlayerColor(i) ~= player.color then return false end
    end
    return true
end
return M
