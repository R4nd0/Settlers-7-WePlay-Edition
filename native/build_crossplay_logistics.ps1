param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=[IO.Path]::GetFullPath((Join-Path $repo 'work'))+[IO.Path]::DirectorySeparatorChar
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $out.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)){throw 'Use a fresh output directory under repository work'}
$found=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $found){throw 'Visual C++ x86 build tools are required'}
$setup=Join-Path $found 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Toolchain initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $out
try{
    foreach($name in @('model','gold_sort','gold_inline','history_abi','intent','intent_abi')){
        $file='crossplay_logistics_'+$name+'_tests'
        & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_CRT_SECURE_NO_WARNINGS ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot ($file+'.cpp')) ('/Fe:'+$file+'.exe') /link /DYNAMICBASE /NXCOMPAT
        if($LASTEXITCODE){throw ('Shared logistics fixture did not compile: '+$name)}
        & (Join-Path $out ($file+'.exe'))
        if($LASTEXITCODE){throw ('Shared logistics fixture failed: '+$name)}
    }
    # This fixed high-base image is for offline guard emulation only.
    # Never execute it natively at the absolute original-game addresses.
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- /D_CRT_SECURE_NO_WARNINGS ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_logistics_model_tests.cpp') /Fe:crossplay_logistics_guard_tests.exe /link /BASE:0x24000000 /FIXED /DYNAMICBASE:NO /NXCOMPAT
    if($LASTEXITCODE){throw 'Shared logistics guard image did not compile'}
}finally{Pop-Location}
Write-Output 'Shared logistics source fixtures passed. No original game code was executed, hooks installed, or game started.'
