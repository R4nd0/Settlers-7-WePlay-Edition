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
$names=@('native_effects_wire.h','native_effects_wire_fixture.cpp','material_shader_config_v2.h','material_shader_effects.h')
$leases=@();$hashes=@{};$sha=[Security.Cryptography.SHA256]::Create()
Push-Location $output
try {
    foreach($name in $names) {$bytes=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name));$hashes[$name]=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant();$snapshot=Join-Path $output $name;[IO.File]::WriteAllBytes($snapshot,$bytes);$leases+=,[IO.File]::Open($snapshot,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 native_effects_wire_fixture.cpp /Fe:native_effects_wire_fixture.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Effects wire CPU fixture compile failed.'}
    & (Join-Path $output 'native_effects_wire_fixture.exe')
    if($LASTEXITCODE){throw 'Effects wire CPU fixture failed.'}
    foreach($name in $names){$after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name)))).Replace('-','').ToLowerInvariant();if($after -ne $hashes[$name]){throw 'Source changed during build; rebuild.'}}
    $manifest=@{format='s7-effects-wire-cpu-build-v1';sources=$hashes;gpu_started=$false;game_started=$false;cpu_checks_passed=$true}
    [IO.File]::WriteAllText((Join-Path $output 'build.private.json'),($manifest|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
