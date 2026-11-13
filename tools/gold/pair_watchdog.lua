-- Development-only frame watchdog. Reads original engine counters; never
-- advances the simulation or alters loading, Ready, network or save state.
-- Installed in local/core.update, so changing menu states cannot remove it.
local G, options, report = ...
local began, sampledAt, samples, closing = nil, nil, 0, false
local function tick()
    if closing then return end
    local now = G.Game.GetRunTime()
    if not began then began = now end
    if not sampledAt or now - sampledAt >= 1000 then
        sampledAt, samples = now, samples + 1
        local inMap = G.Game.InMapMode()
        report({sample=samples,elapsed_ms=now-began,in_map=inMap,
            turn=inMap and G.Game.GetCurrentTurn() or nil,
            game_time_ms=inMap and G.Game.GetGameTime() or nil})
    end
    if now - began >= options.duration then
        -- Original normal application exit API; no process termination or OS
        -- input, and no state-manager callback retained after its destruction.
        closing = true
        G.system.exitApi()
    end
end
return {tick=tick,closing=function() return closing end}
