param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=[IO.Path]::GetFullPath((Join-Path $repo 'work'))+[IO.Path]::DirectorySeparatorChar
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $out.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)){
    throw 'Use a fresh output directory under repository work'
}
$found=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $found){throw 'Visual C++ x86 build tools are required'}
$setup=Join-Path $found 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Toolchain initialization failed'}
foreach($line in $lines){
    $at=$line.IndexOf('=')
    if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}
}
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $out
try{
    $runtimeSources=@('crossplay_production_runtime.cpp','crossplay_channel.cpp','crossplay_participant.cpp','crossplay_admission.cpp') | ForEach-Object {Join-Path $PSScriptRoot $_}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /D_WINSOCK_DEPRECATED_NO_WARNINGS ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_production_launch_tests.cpp') (Join-Path $PSScriptRoot 'crossplay_production_launch.cpp') (Join-Path $PSScriptRoot 'crossplay_production_context.cpp') @runtimeSources /Fe:crossplay_production_launch_tests.exe /link bcrypt.lib ws2_32.lib /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production launch contract fixture did not compile'}
    & (Join-Path $out 'crossplay_production_launch_tests.exe')
    if($LASTEXITCODE){throw 'Production launch contract fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_gold_resume_postload_tests.cpp') /Fe:crossplay_gold_resume_postload_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Gold saved request policy/ABI fixture did not compile'}
    & (Join-Path $out 'crossplay_gold_resume_postload_tests.exe')
    if($LASTEXITCODE){throw 'Gold saved request policy/ABI fixture failed'}
    # Separate emulator-only guard image. NEVER execute a fixed high-base guard
    # image natively: its absolute original-game addresses are intentionally real.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_gold_resume_postload_tests.cpp') /Fe:crossplay_gold_resume_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Gold saved request original-code guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 (Join-Path $PSScriptRoot 'crossplay_production_gate_tests.cpp') /Fe:crossplay_production_gate_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production direct-load gate fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_production_observer_tests.cpp') (Join-Path $PSScriptRoot 'crossplay_production_launch.cpp') (Join-Path $PSScriptRoot 'crossplay_production_context.cpp') /Fe:crossplay_production_observer_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production session observer fixture did not compile'}
    & (Join-Path $out 'crossplay_production_observer_tests.exe')
    if($LASTEXITCODE){throw 'Production session observer fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_production_observer_tests.cpp') (Join-Path $PSScriptRoot 'crossplay_production_launch.cpp') (Join-Path $PSScriptRoot 'crossplay_production_context.cpp') /Fe:crossplay_production_observer_guard_tests.exe /link bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Production session observer emulator guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_outgoing_observer_tests.cpp') /Fe:crossplay_outgoing_observer_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production outgoing observer fixture did not compile'}
    & (Join-Path $out 'crossplay_outgoing_observer_tests.exe')
    if($LASTEXITCODE){throw 'Production outgoing observer fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_outgoing_observer_tests.cpp') /Fe:crossplay_outgoing_observer_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Production outgoing observer emulator guard fixture did not compile'}
    $configurationObserverSources=@('crossplay_production_observer_tests.cpp','crossplay_production_launch.cpp','crossplay_production_context.cpp') | ForEach-Object {Join-Path $PSScriptRoot $_}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /DS7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING ('/I'+$PSScriptRoot) @configurationObserverSources /Fe:crossplay_configuration_observer_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production configuration consumer observer fixture did not compile'}
    & (Join-Path $out 'crossplay_configuration_observer_tests.exe')
    if($LASTEXITCODE){throw 'Production configuration consumer observer fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /DS7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING ('/I'+$PSScriptRoot) @configurationObserverSources /Fe:crossplay_configuration_observer_guard_tests.exe /link bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Production configuration consumer emulator guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_configuration_receipt_tests.cpp') /Fe:crossplay_configuration_receipt_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production configuration receipt fixture did not compile'}
    & (Join-Path $out 'crossplay_configuration_receipt_tests.exe')
    if($LASTEXITCODE){throw 'Production configuration receipt fixture failed'}
    $uiSources=@('crossplay_production_ui_tests.cpp','crossplay_production_launch.cpp','crossplay_production_context.cpp') | ForEach-Object {Join-Path $PSScriptRoot $_}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_WINSOCK_DEPRECATED_NO_WARNINGS ('/I'+$PSScriptRoot) @uiSources @runtimeSources /Fe:crossplay_production_ui_tests.exe /link bcrypt.lib ws2_32.lib /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Production UI ABI fixture did not compile'}
    & (Join-Path $out 'crossplay_production_ui_tests.exe')
    if($LASTEXITCODE){throw 'Production UI ABI fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_WINSOCK_DEPRECATED_NO_WARNINGS ('/I'+$PSScriptRoot) @uiSources @runtimeSources /Fe:crossplay_production_ui_guard_tests.exe /link bcrypt.lib ws2_32.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Production UI emulator guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_bootstrap_tests.cpp') /Fe:crossplay_bootstrap_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Host bootstrap fixture did not compile'}
    & (Join-Path $out 'crossplay_bootstrap_tests.exe')
    if($LASTEXITCODE){throw 'Host bootstrap fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_neo_selection_tests.cpp') /Fe:crossplay_neo_selection_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Fresh NEO selection fixture did not compile'}
    & (Join-Path $out 'crossplay_neo_selection_tests.exe')
    if($LASTEXITCODE){throw 'Fresh NEO selection fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_history_claim_tests.cpp') /Fe:crossplay_history_claim_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'History preparation claim fixture did not compile'}
    & (Join-Path $out 'crossplay_history_claim_tests.exe')
    if($LASTEXITCODE){throw 'History preparation claim fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_history_claim_tests.cpp') /Fe:crossplay_history_claim_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'History preparation claim emulator guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_history_choices_tests.cpp') /Fe:crossplay_history_choices_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'History actual choice observation fixture did not compile'}
    & (Join-Path $out 'crossplay_history_choices_tests.exe')
    if($LASTEXITCODE){throw 'History actual choice observation fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_history_choices_tests.cpp') /Fe:crossplay_history_choices_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'History actual choice emulator guard fixture did not compile'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_gold_host_choices_tests.cpp') /Fe:crossplay_gold_host_choices_tests.exe /link /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw 'Gold host choice fixture did not compile'}
    & (Join-Path $out 'crossplay_gold_host_choices_tests.exe')
    if($LASTEXITCODE){throw 'Gold host choice fixture failed'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_gold_host_choices_tests.cpp') /Fe:crossplay_gold_host_choices_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Gold host choice emulator guard fixture did not compile'}
}finally{Pop-Location}
Write-Output 'Production handoff fixtures passed. No adapter was deployed or game started.'
