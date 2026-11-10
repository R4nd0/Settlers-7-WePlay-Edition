param([Parameter(Mandatory=$true)][string]$OutputDirectory,
      [string]$IsolatedLabExecutable,
      [switch]$LocalSimulation, [switch]$LocalLogin, [switch]$AccountLogin)
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
if ($LocalSimulation) { $common += '/DS7_GOLD_LOCAL_SIMULATION' }
if ($LocalLogin -or $AccountLogin) { $common += '/DS7_GOLD_LOCAL_LOGIN' }
if ($AccountLogin) { $common += '/DS7_GOLD_ACCOUNT_LOGIN' }
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
    & cl.exe @common (Join-Path $nativeSource 'gold_startup_tests.cpp') (Join-Path $nativeSource 'network_guard.cpp') /Fe:gold_startup_tests.exe /link bcrypt.lib ws2_32.lib winhttp.lib shell32.lib user32.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Gold native fixture build failed.' }
    & (Join-Path $nativeOutput 'gold_startup_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Gold native startup fixtures failed ($LASTEXITCODE)." }
    & cl.exe @common /LD /DS7_GUARD_TESTING (Join-Path $nativeSource 'network_guard.cpp') (Join-Path $nativeSource 'network_guard_test_exports.cpp') /Fe:network_guard_test.dll /link ws2_32.lib winhttp.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Gold network fixture DLL build failed.' }
    foreach ($testName in @('network_guard_tests','gold_network_tests')) {
        & cl.exe @common (Join-Path $nativeSource "$testName.cpp") "/Fe:$testName.exe" /link network_guard_test.lib ws2_32.lib winhttp.lib shell32.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
        if ($LASTEXITCODE -ne 0) { throw "Gold $testName build failed." }
        & (Join-Path $nativeOutput "$testName.exe")
        if ($LASTEXITCODE -ne 0) { throw "Gold $testName failed ($LASTEXITCODE)." }
    }
    & cl.exe @common /LD (Join-Path $nativeSource 'gold_startup.cpp') (Join-Path $nativeSource 'network_guard.cpp') /Fe:weplay_gold_startup.dll /link "/DEF:$(Join-Path $nativeSource 'gold_startup.def')" bcrypt.lib ws2_32.lib winhttp.lib shell32.lib user32.lib /DYNAMICBASE /NXCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Gold native startup build failed.' }
} finally { Pop-Location }
Write-Output 'Built experimental Gold startup only. No game was launched or installed.'
