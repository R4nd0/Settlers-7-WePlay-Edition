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
    foreach($name in @('crossplay_resume_transfer','crossplay_resume_start','crossplay_saved_session_config','crossplay_resume_request','crossplay_resume_info','crossplay_resume_digest','crossplay_resume_neo')){
        & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot ($name+'_tests.cpp')) bcrypt.lib ('/Fe:'+$name+'.exe')
        if($LASTEXITCODE){throw "Fixture did not compile: $name"}
        & (Join-Path $out ($name+'.exe'))
        if($LASTEXITCODE){throw "Fixture failed: $name"}
    }
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_resume_file_guard_tests.cpp') /Fe:crossplay_resume_file_guard_tests.exe /link /BASE:0x24000000 /FIXED
    if($LASTEXITCODE){throw 'Ownership guard fixture did not compile'}
}finally{Pop-Location}
Write-Output 'Experimental resume protocol fixtures passed. No adapter was installed or game launched.'
