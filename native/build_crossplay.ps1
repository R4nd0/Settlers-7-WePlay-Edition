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
    # The fixed image above is an emulator fixture. Use a relocatable image
    # for Windows execution, where system DLLs can occupy the fixture base.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_participant_tests.cpp') /Fe:crossplay_participant_runtime_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native participant test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_admission_tests.cpp') (Join-Path $nativeSource 'crossplay_participant.cpp') (Join-Path $nativeSource 'crossplay_channel.cpp') /Fe:crossplay_admission_tests.exe /link ws2_32.lib bcrypt.lib "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay admission test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_chat_tests.cpp') /Fe:crossplay_chat_tests.exe /link "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay chat test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_event_codec_tests.cpp') /Fe:crossplay_event_codec_tests.exe /link bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Native crossplay event codec test build failed.' }
    # Keep the fixed analysis fixture separate from the relocatable executable
    # run on Windows, where a system DLL may already occupy that image base.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_event_codec_tests.cpp') /Fe:crossplay_event_codec_runtime_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Relocatable native codec test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_preparation_tests.cpp') /Fe:crossplay_preparation_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'History preparation test build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $nativeSource 'crossplay_session_config_tests.cpp') /Fe:crossplay_session_config_tests.exe /link /DYNAMICBASE /NXCOMPAT "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if ($LASTEXITCODE -ne 0) { throw 'Session configuration test build failed.' }
} finally { Pop-Location }
Write-Output 'Built channel, participant, admission, chat, native codec, History preparation and session configuration tests. No game adapter was installed or launched.'
