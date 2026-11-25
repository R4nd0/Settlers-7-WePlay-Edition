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
$names=@('water_shader_effects.h','water_shader_effects.cpp','native_material_draw.h','native_effects_wire.h','material_shader_config_v2.h','material_shader_effects_contract_v2.h','supersampling_hook.cpp','supersampling_hook.h','supersampling_hook_config.h','supersampling_hook_capture.h','supersampling_hook_capture.cpp','supersampling_hook_effects_contract.h','supersampling_hook_effects_draw.h','supersampling_hook_comparison_draw.h','supersampling_hook_effects_gpu.cpp','supersampling_hook.def','supersampling_hook_fixture.cpp','supersampling_probe.cpp','material_shader_observer.h','material_shader_observer.cpp','material_shader_effects.h','material_shader_effects.cpp','material_comparison.h','material_comparison.cpp','graphics_observation.h','graphics_observation.cpp','atmosphere_clock.h','atmosphere_fp.h')
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
    $components=@('water_shader_effects.cpp','supersampling_hook.cpp','supersampling_hook_capture.cpp','material_shader_observer.cpp','material_shader_effects.cpp','material_comparison.cpp','graphics_observation.cpp')
    & cl.exe @flags /LD @components /Fe:WePlaySupersamplingLab.dll /link /DEF:supersampling_hook.def bcrypt.lib user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Supersampling hook DLL compile failed.'}
    & cl.exe @flags /DS7_SUPERSAMPLING_FIXTURE @components supersampling_hook_fixture.cpp /Fe:supersampling_hook_fixture.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Supersampling hook GPU fixture compile failed.'}
    & cl.exe @flags /DS7_SUPERSAMPLING_FIXTURE /DS7_MATERIAL_EFFECTS_FIXTURE /DS7_MATERIAL_COMPARISON_FIXTURE @components supersampling_hook_effects_gpu.cpp /Fe:supersampling_effects_gpu.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib d3dcompiler.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Material effects GPU fixture compile failed.'}
    $entries=@()
    foreach($name in $names) {
        $after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name)))).Replace('-','').ToLowerInvariant()
        if($after -ne $hashes[$name]){throw 'Source changed during build; rebuild.'}
        $entries+='"'+$name+'":"'+$hashes[$name]+'"'
    }
    foreach($name in @('WePlaySupersamplingLab.dll','supersampling_hook_fixture.exe','supersampling_effects_gpu.exe')) {
        $hash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $output $name)))).Replace('-','').ToLowerInvariant()
        $entries+='"'+$name+'":"'+$hash+'"'
    }
    $manifest='{"format":"s7-native-graphics-build-v2","source_set":"effects-v4","compiled_capabilities":15,"abi":{"initialize_v1":1048,"initialize_v2":3132,"observation":952,"effects":4132,"effects_v2":6180,"water":1076,"capture":12},"diagnostic_abi":{"comparison":12},"build_sha256":"'+$buildHash+'","files":{'+($entries -join ',')+'}}'
    [IO.File]::WriteAllText((Join-Path $output 'supersampling-hook-build.json'),$manifest,[Text.UTF8Encoding]::new($false))
    $manifest
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
