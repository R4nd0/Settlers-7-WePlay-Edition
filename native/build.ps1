param([Parameter(Mandatory=$true)][string]$OutputDirectory, [switch]$LocalPairLab, [switch]$LocalNetworkPump, [switch]$CastlePrototype, [switch]$CastleWirePrototype=$true,
      [ValidateSet(2,4)][int]$MaximumPhysicalCores=4, [switch]$GameDirectory, [switch]$ResumeCatchup, [switch]$NeoFifoLab)
$ErrorActionPreference = 'Stop'
$nativeSource = $PSScriptRoot
$nativeOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $nativeOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'The x86/x64 C++ toolchain was not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x86 toolchain.' }
foreach ($line in $compilerEnvironment) {
    $position = $line.IndexOf('=')
    if ($position -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$position), $line.Substring($position+1), 'Process') }
}
$common = @('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS','/D_WINSOCK_DEPRECATED_NO_WARNINGS')
$common += "/DS7_MAX_PHYSICAL_CORES=$MaximumPhysicalCores"
if ($LocalPairLab) { $common += '/DS7_LOCAL_PAIR_LAB' }
if ($NeoFifoLab) {
    if (!$LocalPairLab -or $GameDirectory) { throw 'NeoFifoLab requires an isolated LocalPairLab build without GameDirectory.' }
    $common += '/DS7_NEO_FIFO_LAB'
}
if ($LocalNetworkPump -or $GameDirectory) { $common += '/DS7_LOCAL_NETWORK_PUMP' }
if ($GameDirectory) { $common += '/DS7_GAME_DIRECTORY' }
if ($ResumeCatchup) {
    if (!$LocalNetworkPump -and !$LocalPairLab -and !$GameDirectory) { throw 'ResumeCatchup requires the native network pump.' }
    $common += '/DS7_RESUME_CATCHUP'
}
if ($CastlePrototype -or $CastleWirePrototype) { $common += '/DS7_CASTLE_PROTOTYPE' }
if ($CastleWirePrototype) { $common += '/DS7_CASTLE_WIRE_PROTOTYPE' }
function Compile-Native([string[]]$Arguments) {
    if ($Arguments -notcontains '/link') { $Arguments += '/link' }
    # Avoid Windows' filename-based installer detection for test executables.
    $Arguments += @('/MANIFEST:EMBED', "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    & cl.exe @common @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
}
Push-Location $nativeOutput
try {
    Compile-Native @((Join-Path $nativeSource 'neo_fifo_policy_tests.cpp'), '/Fe:neo_fifo_policy_tests.exe')
    if ($NeoFifoLab) {
        Compile-Native @((Join-Path $nativeSource 'neo_fifo_integration_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:neo_fifo_integration_tests.exe')
        Compile-Native @((Join-Path $nativeSource 'neo_fifo_bridge_tests.cpp'), '/Fe:neo_fifo_bridge_tests.exe')
        Compile-Native @((Join-Path $nativeSource 'lab_priority_capture_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:lab_priority_capture_tests.exe')
        Compile-Native @((Join-Path $nativeSource 'lab_priority_audit_tests.cpp'), '/Fe:lab_priority_audit_tests.exe')
    }
    Compile-Native @((Join-Path $nativeSource 'castle_profile_tests.cpp'), '/Fe:castle_profile_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'castle_wire_tests.cpp'), '/Fe:castle_wire_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'local_platform.cpp'), (Join-Path $nativeSource 'local_platform_tests.cpp'), '/Fe:local_platform_tests.exe')
    Compile-Native @('/LD', '/DS7_GUARD_TESTING', (Join-Path $nativeSource 'network_guard.cpp'), (Join-Path $nativeSource 'network_guard_test_exports.cpp'), '/Fe:network_guard_test.dll', '/link','ws2_32.lib','winhttp.lib','shell32.lib')
    Compile-Native @((Join-Path $nativeSource 'network_guard_tests.cpp'), '/Fe:network_guard_tests.exe', '/link','network_guard_test.lib','ws2_32.lib','winhttp.lib','shell32.lib')
    Compile-Native @((Join-Path $nativeSource 'lan_membership_tests.cpp'), '/Fe:lan_membership_tests.exe', '/link','network_guard_test.lib','ws2_32.lib')
    Compile-Native @((Join-Path $nativeSource 'adapter_rejection_test.cpp'), '/Fe:adapter_rejection_test.exe', '/link','ws2_32.lib')
    Compile-Native @((Join-Path $nativeSource 'local_connect_tests.cpp'), '/Fe:local_connect_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'resume_pacing_tests.cpp'), '/Fe:resume_pacing_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'terrain_angle_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:terrain_angle_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'cpu_affinity.cpp'), (Join-Path $nativeSource 'cpu_affinity_tests.cpp'), '/Fe:cpu_affinity_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'bootstrap_patch_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:bootstrap_patch_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'steam_profile_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:steam_profile_tests.exe')
    if ($GameDirectory) { Compile-Native @((Join-Path $nativeSource 'game_directory_bridge_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:game_directory_bridge_tests.exe') }
    Compile-Native @((Join-Path $nativeSource 'lobby_team_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:lobby_team_tests.exe')
    Compile-Native @((Join-Path $nativeSource 'logistics_priority_tests.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:logistics_priority_tests.exe')
    Compile-Native @('/LD', (Join-Path $nativeSource 'local_platform.cpp'), (Join-Path $nativeSource 'network_guard.cpp'),
        (Join-Path $nativeSource 'offline_bootstrap.cpp'), (Join-Path $nativeSource 'runtime_config.cpp'), (Join-Path $nativeSource 'cpu_affinity.cpp'), '/Fe:uplay_r1_loader.dll',
        '/link', "/DEF:$(Join-Path $nativeSource 'local_platform.def')", 'ws2_32.lib','winhttp.lib','shell32.lib','bcrypt.lib','crypt32.lib',
        '/DYNAMICBASE','/NXCOMPAT')
} finally { Pop-Location }
Write-Output 'Built experimental x86 adapter and synthetic tests. No game was launched or installed.'
