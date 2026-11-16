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
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_production_launch_tests.cpp') (Join-Path $PSScriptRoot 'crossplay_production_launch.cpp') /Fe:crossplay_production_launch_tests.exe /link /DYNAMICBASE /NXCOMPAT
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
}finally{Pop-Location}
Write-Output 'Production handoff fixtures passed. No adapter was deployed or game started.'
