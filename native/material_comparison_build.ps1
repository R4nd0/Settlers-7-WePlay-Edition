param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a new directory inside repository work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('material_comparison.cpp','material_comparison.h','material_comparison_fixture.cpp')
$leases=@();$hashes=@{};$sha=[Security.Cryptography.SHA256]::Create()
Push-Location $output
try {
    foreach($name in $names) {$bytes=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name));$hashes[$name]=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant();$snapshot=Join-Path $output $name;[IO.File]::WriteAllBytes($snapshot,$bytes);$leases+=,[IO.File]::Open($snapshot,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
    $flags=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/fp:strict')
    & cl.exe @flags /c material_comparison.cpp /Fo:material_comparison.production.obj
    if($LASTEXITCODE){throw 'Production comparison compile failed.'}
    & cl.exe @flags /DS7_MATERIAL_COMPARISON_FIXTURE material_comparison.cpp material_comparison_fixture.cpp /Fe:material_comparison_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Comparison fixture compile failed.'}
    $receipt=& (Join-Path $output 'material_comparison_fixture.exe')
    if($LASTEXITCODE){throw 'Comparison CPU fixture failed.'}
    foreach($name in $names){$after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name)))).Replace('-','').ToLowerInvariant();if($after -ne $hashes[$name]){throw 'Source changed during build; rebuild.'}}
    $manifest=@{format='s7-material-comparison-build-v1';gpu_started=$false;game_started=$false;cpu_checks_passed=$true;sources=$hashes;fixture=($receipt|ConvertFrom-Json)}
    [IO.File]::WriteAllText((Join-Path $output 'comparison-build.private.json'),($manifest|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    Write-Output $receipt
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
