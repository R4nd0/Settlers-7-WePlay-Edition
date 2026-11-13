-- Opt-in application-frame owner for pair automation. A menu can change its
-- update callback or disappear while native jobs are running. The test driver
-- must remain reachable from the original local/core frame entry point.
local originalUpdate, watchdog = ...
local driver
local function register(tick)
    _G.assert(_G.type(tick) == "function", "Pair driver must provide a frame callback")
    driver = tick
end
local function update()
    watchdog.tick()
    if watchdog.closing() then return end
    originalUpdate()
    if driver then driver() end
end
return {register=register,update=update}
