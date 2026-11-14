local ui = ...
local console, log = ui.WriteToConsole, ui.WriteToLog
console('WEPLAY_GOLD_CAPTURE_V1 console-test')
log('WEPLAY_GOLD_CAPTURE_V1 log-test')
console('Unapproved diagnostic text must remain private')
log('WEPLAY_GOLD_CAPTURE_V1 unapproved-marker')
console(string.rep('X', 512))
log(123)
console('WEPLAY_GOLD_CAPTURE_V1 complete')
