param([Parameter(Mandatory=$true)][string]$OutputDirectory,
      [string]$IsolatedLabExecutable,
      [switch]$LocalSimulation, [switch]$LocalLogin, [switch]$AccountLogin, [switch]$PlatformIndependent,
      [switch]$CrossplayRoomLobbyLab)
$ErrorActionPreference = 'Stop'
$nativeSource = $PSScriptRoot
$nativeOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $nativeOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'The x86 C++ toolchain was not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x86 toolchain.' }
foreach ($line in $compilerEnvironment) {
    $position = $line.IndexOf('=')
    if ($position -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$position), $line.Substring($position+1), 'Process') }
}
$common = @('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS','/D_WINSOCK_DEPRECATED_NO_WARNINGS','/DS7_GOLD_STARTUP')
if ($CrossplayRoomLobbyLab) {
    if (!$PlatformIndependent) { throw 'CrossplayRoomLobbyLab requires the original standalone executable guard.' }
    $common += '/DS7_CROSSPLAY_ROOM_LOBBY_LAB'
}
if ($LocalSimulation) { $common += '/DS7_GOLD_LOCAL_SIMULATION' }
if ($LocalLogin -or $AccountLogin) { $common += '/DS7_GOLD_LOCAL_LOGIN' }
if ($AccountLogin) { $common += '/DS7_GOLD_ACCOUNT_LOGIN' }
$standaloneSources = @()
$productionSources = @()
if ($PlatformIndependent) {
    if ($IsolatedLabExecutable -or $AccountLogin -or $LocalLogin) { throw 'Standalone Gold uses the verified local simulation and original prepared executable only.' }
    if (!$LocalSimulation) { throw 'Standalone Gold requires LocalSimulation.' }
    $common += @('/DS7_PLATFORM_INDEPENDENT','/O2','/Oy-')
    foreach ($name in @('crossplay_production_launch.cpp','crossplay_production_context.cpp','crossplay_production_runtime.cpp','crossplay_channel.cpp','crossplay_participant.cpp','crossplay_admission.cpp')) {
        $productionSources += (Join-Path $nativeSource $name)
    }
    foreach ($name in @('standalone_entry.cpp','standalone_image.cpp','standalone_profile.cpp','standalone_network_fixture.cpp')) {
        $standaloneSources += (Join-Path $nativeSource $name)
    }
    $standaloneSources += '/DS7_KERNEL_PROBE_LIBRARY'
    $standaloneSources += $productionSources
}
if ($IsolatedLabExecutable) {
    $labPath = (Resolve-Path -LiteralPath $IsolatedLabExecutable).Path
    $workspace = [System.IO.Path]::GetFullPath((Join-Path $nativeSource '..\work\'))
    if (!$labPath.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) { throw 'Gold lab inputs must remain under work.' }
    $labHash = (Get-FileHash -LiteralPath $labPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $labHeader = Join-Path $nativeOutput 'gold-lab-pin.private.h'
    [System.IO.File]::WriteAllText($labHeader, "#define S7_GOLD_LAB_SHA256 `"$labHash`"`r`n")
    $common += "/FI$labHeader"
}
Push-Location $nativeOutput
try {
    if ($PlatformIndependent) {
        & cl.exe /nologo /c /TC /O2 /MT /W3 (Join-Path $nativeSource 'third_party/puff/puff.c') /Fo:crossplay_resume_puff.obj
        if ($LASTEXITCODE) { throw 'Crossplay saved archive inflater build failed.' }
        $productionSources += (Join-Path $nativeOutput 'crossplay_resume_puff.obj')
        $standaloneSources += (Join-Path $nativeOutput 'crossplay_resume_puff.obj')
    }
    & cl.exe @common (Join-Path $nativeSource 'ai_catalog_unlock_tests.cpp') /Fe:ai_catalog_unlock_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /INCREMENTAL:NO
    if ($LASTEXITCODE -ne 0) { throw 'AI catalog offline guard fixture build failed.' }
    & cl.exe @common (Join-Path $nativeSource 'gold_startup_tests.cpp') (Join-Path $nativeSource 'network_guard.cpp') @productionSources /Fe:gold_startup_tests.exe /link bcrypt.lib ws2_32.lib winhttp.lib shell32.lib user32.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold native fixture build failed.' }
    & (Join-Path $nativeOutput 'gold_startup_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Gold native startup fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common (Join-Path $nativeSource 'gold_movement_precision_tests.cpp') /Fe:gold_movement_precision_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold movement precision fixture build failed.' }
    & (Join-Path $nativeOutput 'gold_movement_precision_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Gold movement precision fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common (Join-Path $nativeSource 'gold_logistics_binding_tests.cpp') /Fe:gold_logistics_binding_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold logistics binding fixture build failed.' }
    & (Join-Path $nativeOutput 'gold_logistics_binding_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Gold logistics binding fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common (Join-Path $nativeSource 'crossplay_neo_agreement_tests.cpp') /Fe:crossplay_neo_agreement_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Crossplay NEO agreement fixture build failed.' }
    & (Join-Path $nativeOutput 'crossplay_neo_agreement_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Crossplay NEO agreement fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common (Join-Path $nativeSource 'crossplay_priority_command_tests.cpp') /Fe:crossplay_priority_command_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Crossplay priority command fixture build failed.' }
    & (Join-Path $nativeOutput 'crossplay_priority_command_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Crossplay priority command fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common /LD /DS7_GUARD_TESTING (Join-Path $nativeSource 'network_guard.cpp') (Join-Path $nativeSource 'network_guard_test_exports.cpp') /Fe:network_guard_test.dll /link ws2_32.lib winhttp.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Gold network fixture DLL build failed.' }
    foreach ($testName in @('network_guard_tests','gold_network_tests')) {
        & cl.exe @common (Join-Path $nativeSource "$testName.cpp") "/Fe:$testName.exe" /link network_guard_test.lib ws2_32.lib winhttp.lib wininet.lib shell32.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
        if ($LASTEXITCODE -ne 0) { throw "Gold $testName build failed." }
        & (Join-Path $nativeOutput "$testName.exe")
        if ($LASTEXITCODE -ne 0) { throw "Gold $testName failed ($LASTEXITCODE)." }
    }
    $standaloneExport = @()
    if ($PlatformIndependent) { $standaloneExport += @('/EXPORT:WePlayInitializeStandalone=_WePlayInitializeStandalone@4','/Brepro') }
    & cl.exe @common /LD (Join-Path $nativeSource 'gold_startup.cpp') (Join-Path $nativeSource 'network_guard.cpp') @standaloneSources /Fe:weplay_gold_startup.dll /link "/DEF:$(Join-Path $nativeSource 'gold_startup.def')" bcrypt.lib ws2_32.lib winhttp.lib shell32.lib user32.lib iphlpapi.lib /DYNAMICBASE /NXCOMPAT @standaloneExport
    if ($LASTEXITCODE -ne 0) { throw 'Gold native startup build failed.' }
} finally { Pop-Location }
Write-Output 'Built experimental Gold startup only. No game was launched or installed.'
