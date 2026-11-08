-- Camera transitions must not replace the user's pending graphics choice.
-- In the verified HE build, GFX.ToggleDepthOfField writes that choice even
-- when the active option cannot change until the next map is initialized.
local G = ...
local option = G.UI.GfxOptions.DepthOfField
return function(requested)
 local selected = G.UI.GfxGetWriteOptionValue(option)
 _G.assert(selected == 0 or selected == 1, "Unknown depth-of-field selection")
 G.GFX.ToggleDepthOfField(requested and selected == 1)
 -- false changes only the pending value; it does not undo a temporary camera
 -- disable on a renderer which supports changing the active effect immediately.
 if G.UI.GfxGetWriteOptionValue(option) ~= selected then
  G.UI.GfxSetOption(option, selected, false)
 end
end
