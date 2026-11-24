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
$names=@('supersampling_hook.cpp','supersampling_hook.h','supersampling_hook.def','supersampling_hook_fixture.cpp','supersampling_probe.cpp')
$leases=@()
$hashes=@{}
$sha=[Security.Cryptography.SHA256]::Create()
Push-Location $output
try {
    foreach($name in $names) {
        $bytes=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name))
        $hashes[$name]=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant()
        $snapshot=Join-Path $output $name
        [IO.File]::WriteAllBytes($snapshot,$bytes)
        $leases+=,[IO.File]::Open($snapshot,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    }
    $buildHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($PSCommandPath))).Replace('-','').ToLowerInvariant()
    $flags=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2')
    & cl.exe @flags /LD supersampling_hook.cpp /Fe:WePlaySupersamplingLab.dll /link /DEF:supersampling_hook.def bcrypt.lib user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Supersampling hook DLL compile failed.'}
    & cl.exe @flags /DS7_SUPERSAMPLING_FIXTURE supersampling_hook.cpp supersampling_hook_fixture.cpp /Fe:supersampling_hook_fixture.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Supersampling hook GPU fixture compile failed.'}
    $entries=@()
    foreach($name in $names) {
        $after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name)))).Replace('-','').ToLowerInvariant()
        if($after -ne $hashes[$name]){throw 'Source changed during build; rebuild.'}
        $entries+='"'+$name+'":"'+$hashes[$name]+'"'
    }
    foreach($name in @('WePlaySupersamplingLab.dll','supersampling_hook_fixture.exe')) {
        $hash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $output $name)))).Replace('-','').ToLowerInvariant()
        $entries+='"'+$name+'":"'+$hash+'"'
    }
    $manifest='{"format":"s7-supersampling-hook-build-v1","build_sha256":"'+$buildHash+'","files":{'+($entries -join ',')+'}}'
    [IO.File]::WriteAllText((Join-Path $output 'supersampling-hook-build.json'),$manifest,[Text.UTF8Encoding]::new($false))
    $manifest
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
