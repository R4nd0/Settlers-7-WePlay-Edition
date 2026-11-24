param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {
    throw 'Use a new output directory inside repository work/.'
}
$sources=@('graphics_observation.h','graphics_observation.cpp','graphics_observation_tests.cpp','atmosphere_clock.h','atmosphere_fp.h','graphics_observation_build.ps1')
$before=@{}
foreach($name in $sources){$before[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the x86 toolchain.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $output
try {
    $common=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/arch:SSE2','/fp:strict',('/I'+$PSScriptRoot))
    & cl.exe @common /DGRAPHICS_OBSERVATION_FIXTURE (Join-Path $PSScriptRoot 'graphics_observation.cpp') (Join-Path $PSScriptRoot 'graphics_observation_tests.cpp') /Fe:graphics_observation_tests.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Graphics observation fixtures did not compile.'}
    & (Join-Path $output 'graphics_observation_tests.exe')
    if($LASTEXITCODE){throw 'Graphics observation fixtures failed.'}
    & cl.exe @common /c (Join-Path $PSScriptRoot 'graphics_observation.cpp') /Fo:graphics_observation.obj
    if($LASTEXITCODE){throw 'Normal graphics observation object did not compile.'}
    foreach($name in $sources){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $before[$name]){throw 'Observation source changed during compilation; rebuild into a fresh directory.'}}
    $value=@{format='s7-graphics-observation-build-v1';game_tested=$false;sources=$before;fixtures_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'graphics_observation_tests.exe') -Algorithm SHA256).Hash.ToLowerInvariant();object_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'graphics_observation.obj') -Algorithm SHA256).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText((Join-Path $output 'graphics-observation-build.json'),($value|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
} finally {Pop-Location}
