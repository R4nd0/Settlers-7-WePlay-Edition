-- Opt-in laboratory pacing, using the original x20 test API. This requests
-- native turn duration; it never advances turns, sets world time or edits CRCs.
local G, report = ...
local attempted, finished, began, baseline = false, false, nil, nil
return function()
    if finished or not G.game.started or not G.Game.InMapMode()
        or G.Game.GetCurrentTurn() < 1 then return end
    if not attempted then
        attempted, began = true, G.Game.GetRunTime()
        baseline = G.Game.GetGameTurnDuration()
        _G.assert(baseline == 100 or baseline == 5, "Unexpected original turn pacing")
        if baseline ~= 5 then G.Game.SetGameTurnDuration(5) end
    end
    local actual = G.Game.GetGameTurnDuration()
    -- The original setter queues the next duration. Readback may legitimately
    -- remain at 100 until the next engine update; never report immediate success.
    if actual ~= 5 and G.Game.GetRunTime()-began < 3000 then return end
    finished = true
    report({requested_speed=20,before_ms=baseline,actual_ms=actual,
        accepted=actual==5,turn=G.Game.GetCurrentTurn()})
end
