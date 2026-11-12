param([Parameter(Mandatory=$true)][string]$OutputDirectory, [string]$MilesOriginal,
      [switch]$GameDirectory,[switch]$NeoLogistics,
      [ValidateSet('ImageTests','PolicyTests','History','Gold','All')][string]$Target='ImageTests')
$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Use a fresh standalone build directory.' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'The x86 C++ toolchain is required.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x86 toolchain.' }
foreach ($line in $compilerEnvironment) {
    $at = $line.IndexOf('=')
    if ($at -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process') }
}
$common = @('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/D_CRT_SECURE_NO_WARNINGS')
function Compile-Standalone([string[]]$Arguments) {
    if ($Arguments -notcontains '/link') { $Arguments += '/link' }
    & cl.exe @common @Arguments /Brepro
    if ($LASTEXITCODE -ne 0) { throw 'Standalone build failed.' }
}
Push-Location $destination
try {
    if ($Target -ne 'PolicyTests') {
    Compile-Standalone @((Join-Path $source 'standalone_image.cpp'),(Join-Path $source 'standalone_image_tests.cpp'),
        '/Fe:standalone_image_tests.exe','/link','bcrypt.lib', '/MANIFEST:EMBED',"/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    & .\standalone_image_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Standalone image tests failed.' }
    Compile-Standalone @((Join-Path $source 'standalone_profile.cpp'),(Join-Path $source 'standalone_profile_tests.cpp'),
        '/Fe:standalone_profile_tests.exe','/link','shell32.lib', '/MANIFEST:EMBED',"/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    & .\standalone_profile_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Standalone profile tests failed.' }
    Compile-Standalone @('/LD','/DS7_ENTRY_FIXTURE_DLL',(Join-Path $source 'standalone_entry_fixture.cpp'),
        '/Fe:standalone_entry_fixture.dll','/link','/EXPORT:WePlayInitializeStandalone=_WePlayInitializeStandalone@4')
    Compile-Standalone @((Join-Path $source 'standalone_entry_fixture.cpp'),'/Fe:standalone_entry_fixture.exe')
    }
    Compile-Standalone @((Join-Path $source 'standalone_network_policy.cpp'),(Join-Path $source 'standalone_peer_policy.cpp'),(Join-Path $source 'standalone_image.cpp'),
        '/Fe:weplay_network_policy.exe','/link','fwpuclnt.lib','bcrypt.lib','advapi32.lib','rpcrt4.lib')
    Compile-Standalone @('/DS7_NETWORK_POLICY_LIBRARY',(Join-Path $source 'standalone_network_fixture.cpp'),
        (Join-Path $source 'standalone_network_policy.cpp'),(Join-Path $source 'standalone_image.cpp'),
        '/Fe:standalone_network_fixture.exe','/link','fwpuclnt.lib','bcrypt.lib','advapi32.lib','rpcrt4.lib','ws2_32.lib','iphlpapi.lib')
    if ($Target -notin @('ImageTests','PolicyTests')) {
        if (!$MilesOriginal) { throw 'The original Miles DLL is required to preserve its exact export ordinals.' }
        Compile-Standalone @('/LD',(Join-Path $source 'standalone_forward_gate.cpp'),'/Fe:mss32.dll',
            '/link')
        & python.exe (Join-Path $source '../tools/installer/miles_gate.py') --append (Join-Path $destination 'mss32.dll') ([IO.Path]::GetFullPath($MilesOriginal))
        if ($LASTEXITCODE -ne 0) { throw 'Original Miles export verification failed.' }
        Compile-Standalone @('/LD','/DS7_PLATFORM_INDEPENDENT',(Join-Path $source '../tools/platform-standalone/steam_local_api.cpp'),
            '/Fe:steam_api.dll','/link',"/DEF:$(Join-Path $source '../tools/platform-standalone/steam_local_api.def')")
        if ($Target -in @('History','All')) {
            & (Join-Path $source 'build.ps1') -OutputDirectory (Join-Path $destination 'History') -PlatformIndependent -MaximumPhysicalCores 2 -GameDirectory:$GameDirectory -NeoLogistics:$NeoLogistics
            if ($LASTEXITCODE -ne 0) { throw 'Standalone History build failed.' }
        }
        if ($Target -in @('Gold','All')) {
            & (Join-Path $source 'build_gold.ps1') -OutputDirectory (Join-Path $destination 'Gold') -PlatformIndependent -LocalSimulation
            if ($LASTEXITCODE -ne 0) { throw 'Standalone Gold build failed.' }
        }
    }
} finally { Pop-Location }
Write-Output 'Built and ran standalone image tests. No game was started.'
