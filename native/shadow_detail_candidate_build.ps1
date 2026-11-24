param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a new output directory under repository work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('shadow_detail_candidate.h','shadow_detail_candidate_tests.cpp')
$hashes=@{}
foreach($name in $names){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try{
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'shadow_detail_candidate_tests.cpp') /Fe:shadow_detail_candidate_tests.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Shadow candidate CPU fixtures did not compile.'}
    & (Join-Path $output 'shadow_detail_candidate_tests.exe')
    if($LASTEXITCODE){throw 'Shadow candidate CPU fixtures failed.'}
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw 'Candidate source changed during build.'}}
    $value=@{format='s7-shadow-detail-candidate-build-v1';gpu_tested=$false;game_tested=$false;sources=$hashes;binary_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'shadow_detail_candidate_tests.exe') -Algorithm SHA256).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText((Join-Path $output 'shadow-detail-candidate-build.json'),($value|ConvertTo-Json -Depth 3),[Text.UTF8Encoding]::new($false))
}finally{Pop-Location}
