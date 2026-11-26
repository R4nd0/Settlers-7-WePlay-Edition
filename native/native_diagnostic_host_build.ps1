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
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('terrain_macro_guard.h','water_shader_effects.h','water_shader_config_palette.h','water_shader_palette_contract.h','native_water_wire.h','shader_debug_overlay.h','shader_debug_overlay.cpp','water_shader_effects.cpp','native_material_draw.h','native_effects_wire.h','material_shader_config_v2.h','material_shader_effects_contract_v2.h','material_shader_config_v3.h','material_shader_effects_contract_v3.h','material_shader_config_v4.h','material_shader_effects_contract_v4.h','material_shader_config_v5.h','material_shader_effects_contract_v5.h','supersampling_hook.cpp','supersampling_hook.h','supersampling_hook_config.h','supersampling_hook_capture.h','supersampling_hook_capture.cpp','supersampling_hook_effects_contract.h','supersampling_hook_effects_draw.h','supersampling_hook_comparison_draw.h','supersampling_hook_effects_gpu.cpp','supersampling_hook.def','supersampling_hook_fixture.cpp','supersampling_probe.cpp','material_shader_observer.h','material_shader_observer.cpp','material_shader_effects.h','material_shader_effects.cpp','material_comparison.h','material_comparison.cpp','graphics_observation.h','graphics_observation.cpp','atmosphere_clock.h','atmosphere_fp.h','shadow_filter_effects.h','shadow_filter_effects.cpp','shadow2048_filter_policy.h','up_draw_observer_protocol.h','ambient_occlusion_effects.h','ambient_occlusion_effects.cpp','ambient_occlusion_policy.h','native_diagnostic_host_fixture.cpp','native_up_observer_fixture.cpp','up_draw_observer_protocol_tests.cpp')
$leases=@();$hashes=@{};$sha=[Security.Cryptography.SHA256]::Create()
Push-Location $output
try {
 foreach($name in $names){
  $bytes=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name))
  $hashes[$name]=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant()
  $snapshot=Join-Path $output $name;[IO.File]::WriteAllBytes($snapshot,$bytes)
  $leases+=,[IO.File]::Open($snapshot,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 }
 $buildHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($PSCommandPath))).Replace('-','').ToLowerInvariant()
 $flags=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/fp:strict')
 # The host fixture includes Core/Shadow/AO directly for internal scalar state
 # tests; no public bypass/export, D3D device, window or process attach exists.
 $hostComponents=@('shader_debug_overlay.cpp','water_shader_effects.cpp','supersampling_hook_capture.cpp','material_shader_observer.cpp','material_shader_effects.cpp','material_comparison.cpp','graphics_observation.cpp')
 & cl.exe @flags native_diagnostic_host_fixture.cpp @hostComponents /Fe:native_diagnostic_host_fixture.exe /link bcrypt.lib user32.lib gdi32.lib /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'Diagnostic host CPU fixture compile failed.'}
 $hostProof=& (Join-Path $output 'native_diagnostic_host_fixture.exe')
 if($LASTEXITCODE){throw 'Diagnostic host CPU fixture failed.'}
 $hostProof | Write-Output
 # UP fixture includes Core/Shadow/Observer; link the independent AO module.
 $upComponents=@('shader_debug_overlay.cpp','ambient_occlusion_effects.cpp','water_shader_effects.cpp','supersampling_hook_capture.cpp','material_shader_effects.cpp','material_comparison.cpp','graphics_observation.cpp')
 & cl.exe @flags native_up_observer_fixture.cpp @upComponents /Fe:native_up_observer_fixture.exe /link bcrypt.lib user32.lib gdi32.lib /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'Integrated UP CPU fixture compile failed.'}
 $upProof=& (Join-Path $output 'native_up_observer_fixture.exe')
 if($LASTEXITCODE){throw 'Integrated UP CPU fixture failed.'}
 $upProof | Write-Output
 & cl.exe @flags up_draw_observer_protocol_tests.cpp /Fe:up_draw_observer_protocol_tests.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'UP SDK/protocol CPU fixture compile failed.'}
 $sdkProof=& (Join-Path $output 'up_draw_observer_protocol_tests.exe')
 if($LASTEXITCODE){throw 'UP SDK/protocol CPU fixture failed.'}
 $sdkProof | Write-Output
 foreach($name in $names){
  $after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name)))).Replace('-','').ToLowerInvariant()
  if($after -ne $hashes[$name]){throw 'Input changed during strict snapshot build.'}
 }
 if($buildHash -ne [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($PSCommandPath))).Replace('-','').ToLowerInvariant()){throw 'CPU build script changed during compilation.'}
 $binaries=@{}
 foreach($name in @('native_diagnostic_host_fixture.exe','native_up_observer_fixture.exe','up_draw_observer_protocol_tests.exe')){$binaries[$name]=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $output $name)))).Replace('-','').ToLowerInvariant()}
 $manifest=@{format='s7-native-diagnostic-host-cpu-v1';sources=$hashes;build_sha256=$buildHash;binaries=$binaries;host_proof=$hostProof;up_proof=$upProof;sdk_proof=$sdkProof;gpu_started=$false;game_started=$false}
 [IO.File]::WriteAllText((Join-Path $output 'build.private.json'),($manifest|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
