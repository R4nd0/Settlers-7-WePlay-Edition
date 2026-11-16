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
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_neo_snapshot_tests.cpp') /Fe:crossplay_neo_snapshot_tests.exe /link /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if($LASTEXITCODE){throw 'Native NEO snapshot fixture did not compile'}
    & (Join-Path $out 'crossplay_neo_snapshot_tests.exe')
    if($LASTEXITCODE){throw 'Native NEO snapshot fixture failed'}
}finally{Pop-Location}
Write-Output 'Native NEO snapshot models passed. No game, native save or reload was executed.'
