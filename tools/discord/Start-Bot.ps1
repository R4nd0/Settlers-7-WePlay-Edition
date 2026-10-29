param([string]$Feed)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$python=Join-Path $repo 'work\discord-bot-runtime\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw 'Install the documented private bot runtime first.' }
$arguments=@('-I','-B','-X','utf8',(Join-Path $PSScriptRoot 'bot.py'),'--settings',(Join-Path $PSScriptRoot 'settings.example.json'),
             '--state',(Join-Path $repo 'work\discord-bot-state'))
if ($Feed) { $arguments+=@('--feed',[IO.Path]::GetFullPath($Feed)) }
& $python @arguments
exit $LASTEXITCODE
