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
    $sources=@('crossplay_participant.cpp','crossplay_admission.cpp','crossplay_channel.cpp') | ForEach-Object {Join-Path $PSScriptRoot $_}
    foreach($fixture in @('crossplay_history_host_choices','crossplay_gold_host_choices','crossplay_gold_guest_row','crossplay_gold_guest_identity','crossplay_history_host_configuration','crossplay_history_host_ready','crossplay_gold_guest_ready','crossplay_gold_sdk_ready_hold','crossplay_history_host_guest_row','crossplay_history_start_hold')){
        & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_WINSOCK_DEPRECATED_NO_WARNINGS ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot ($fixture+'_tests.cpp')) @sources ('/Fe:'+ $fixture+'_tests.exe') /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT
        if($LASTEXITCODE){throw "Reverse component fixture did not compile: $fixture"}
        & (Join-Path $out ($fixture+'_tests.exe'))
        if($LASTEXITCODE){throw "Reverse component fixture failed: $fixture"}
        # Build only: fixed original-code guard images must run in the bounded
        # offline emulator, never as native Windows game-address fixtures.
        & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_WINSOCK_DEPRECATED_NO_WARNINGS ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot ($fixture+'_tests.cpp')) @sources ('/Fe:'+ $fixture+'_guard_tests.exe') /link ws2_32.lib bcrypt.lib /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
        if($LASTEXITCODE){throw "Reverse offline guard fixture did not compile: $fixture"}
    }
}finally{Pop-Location}
Write-Output 'Reverse component fixtures passed. No reverse role admitted, adapter deployed or game started.'
