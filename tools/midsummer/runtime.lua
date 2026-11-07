-- Local graphics only: no random numbers, script commands, game/save state.
local G,options,settings = ...
local weather=G.weather
local original=weather.update
local enabled=false
local handle=nil
local failed=false
local function selectedInGame()
 return settings.get(settings.id.weather)==options.weatherModes.midsummer
  and G.game and G.game.started and G.Game.InMapMode()
  and (weather.mode==weather.modes.skirmish or weather.mode==weather.modes.campaign)
end
-- Campaign weather cues must not reintroduce a dark night while this visual
-- preference is active. Menu/intro cues and all original off-mode calls remain.
for _,name in _G.ipairs({"set","setByName"}) do
 local originalSet=weather[name]
 if originalSet then
  weather[name]=function(...)
   if enabled and selectedInGame() then return end
   return originalSet(...)
  end
 end
end
weather.update=function()
 local selected=settings.get(settings.id.weather)==options.weatherModes.midsummer
 local inGame=G.game and G.game.started and G.Game.InMapMode()
 local mode=weather.mode==weather.modes.skirmish or weather.mode==weather.modes.campaign
 if selected and inGame and mode and weather.isCreated() then
  if not enabled and not failed then
   handle=G.GFX.EnvSetLoad("WePlayMidsummer")
   -- A missing native resource silently resolves to Neutral. Detect that.
   if _G.type(handle)~="number" or handle<=0 or handle==weather.neutral then
    failed=true
    weather.set(weather.neutral,0)
    G.ui.messageBox("Mittsommer / Midsummer","Das Lichtprofil fehlt. Bitte diese Spielkopie mit dem aktuellen Paket reparieren. / The lighting profile is missing; repair this copy with the current package.")
   else
    G.GFX.EnvSetBlend(handle,5000)
    weather.id=handle
    enabled=true
   end
  end
  return
 end
 if enabled then
  enabled=false
  weather.id=nil
  if weather.isCreated() then weather.set(weather.neutral,5000) end
 end
 original()
end
