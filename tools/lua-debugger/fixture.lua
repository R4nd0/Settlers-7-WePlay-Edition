-- Executed by the REAL game LuaJIT only when the explicit self-test is queued.
local function leaf(value)
  local doubled = value * 2
  return doubled
end
local function parent(seed)
  local first = seed + 1
  local second = leaf(first)
  local result = second + 3
  return result
end
local answer = parent(10)
return answer
