param([Parameter(Mandatory=$true)][string]$OutputDirectory, [string]$MilesOriginal,
      [switch]$GameDirectory,[switch]$NeoLogistics,[switch]$CrossplayRoom,
      [ValidateSet('ImageTests','PolicyTests','History','Gold','All')][string]$Target='ImageTests')
$ErrorActionPreference = 'Stop'
if ($CrossplayRoom -and $Target -in @('ImageTests','PolicyTests')) {
    throw 'CrossplayRoom requires a standalone game adapter target.'
}
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
    Compile-Standalone @('/DS7_NETWORK_POLICY_LIBRARY',(Join-Path $source 'standalone_peer_policy_tests.cpp'),
        (Join-Path $source 'standalone_network_policy.cpp'),(Join-Path $source 'standalone_image.cpp'),
        '/Fe:standalone_peer_policy_tests.exe','/link','fwpuclnt.lib','bcrypt.lib','advapi32.lib','rpcrt4.lib',
        '/MANIFEST:EMBED',"/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    & .\standalone_peer_policy_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Standalone peer lease tests failed.' }
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
        # Each adapter initializes its own toolchain environment. Keep the
        # first adapter's vcvars changes out of the second adapter's process.
        $adapterPowerShell = (Get-Process -Id $PID).Path
        $roomOptions = @()
        if ($CrossplayRoom) { $roomOptions += '-CrossplayRoom' }
        if ($Target -in @('History','All')) {
            $historyOptions = $roomOptions
            if ($GameDirectory) { $historyOptions += '-GameDirectory' }
            if ($NeoLogistics) { $historyOptions += '-NeoLogistics' }
            & $adapterPowerShell -NoProfile -File (Join-Path $source 'build.ps1') -OutputDirectory (Join-Path $destination 'History') -PlatformIndependent -MaximumPhysicalCores 4 @historyOptions
            if ($LASTEXITCODE -ne 0) { throw 'Standalone History build failed.' }
        }
        if ($Target -in @('Gold','All')) {
            & $adapterPowerShell -NoProfile -File (Join-Path $source 'build_gold.ps1') -OutputDirectory (Join-Path $destination 'Gold') -PlatformIndependent -LocalSimulation @roomOptions
            if ($LASTEXITCODE -ne 0) { throw 'Standalone Gold build failed.' }
        }
    }
} finally { Pop-Location }
Write-Output 'Built and ran standalone image tests. No game was started.'
