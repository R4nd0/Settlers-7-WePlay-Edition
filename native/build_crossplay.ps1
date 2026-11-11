param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$nativeSource = $PSScriptRoot
$nativeOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $nativeOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'The x86 C++ toolchain was not found.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the x86 toolchain.' }
foreach ($line in $compilerEnvironment) {
    $position = $line.IndexOf('=')
    if ($position -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$position),$line.Substring($position+1),'Process') }
}
Push-Location $nativeOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_channel.cpp') (Join-Path $nativeSource 'crossplay_channel_tests.cpp') /Fe:crossplay_channel_tests.exe /link ws2_32.lib bcrypt.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay channel test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_participant_tests.cpp') /Fe:crossplay_participant_tests.exe /link bcrypt.lib /BASE:0x18000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay participant test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_admission_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') (Join-Path $nativeSource 'crossplay_channel.cpp') /Fe:crossplay_admission_tests.exe /link ws2_32.lib bcrypt.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay admission test build failed.' }
} finally { Pop-Location }
Write-Output 'Built channel, participant and admission tests. No game adapter was installed or launched.'
