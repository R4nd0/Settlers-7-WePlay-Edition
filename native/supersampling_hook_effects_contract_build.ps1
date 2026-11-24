param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a new directory inside work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS (Join-Path $PSScriptRoot 'supersampling_hook_effects_contract_tests.cpp') /Fe:supersampling_hook_effects_contract_tests.exe
    if($LASTEXITCODE){throw 'Effects host-contract fixture compile failed.'}
    & .\supersampling_hook_effects_contract_tests.exe
    if($LASTEXITCODE){throw 'Effects host-contract fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 (Join-Path $PSScriptRoot 'supersampling_hook_effects_draw_tests.cpp') /Fe:supersampling_hook_effects_draw_tests.exe
    if($LASTEXITCODE){throw 'Effects draw-protocol fixture compile failed.'}
    & .\supersampling_hook_effects_draw_tests.exe
    if($LASTEXITCODE){throw 'Effects draw-protocol fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 (Join-Path $PSScriptRoot 'supersampling_hook_comparison_draw_tests.cpp') /Fe:supersampling_hook_comparison_draw_tests.exe
    if($LASTEXITCODE){throw 'Nested comparison/effects fixture compile failed.'}
    & .\supersampling_hook_comparison_draw_tests.exe
    if($LASTEXITCODE){throw 'Nested comparison/effects fixture failed.'}
} finally {Pop-Location}
