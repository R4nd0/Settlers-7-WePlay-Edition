param([Parameter(Mandatory=$true)][string]$CoreBuild,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=[IO.Path]::GetFullPath((Join-Path $repository 'work'))+[IO.Path]::DirectorySeparatorChar
$core=[IO.Path]::GetFullPath($CoreBuild);$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$core.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or !$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use retained core snapshots and a fresh work output.'}
$manifestPath=Join-Path $core 'supersampling-hook-build.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if($manifest.format -ne 's7-native-graphics-build-v2' -or $manifest.source_set -ne 'effects-v4' -or $manifest.abi.water -ne 1076){throw 'Exact integrated Water1076 core required.'}
$coreNames=@($manifest.files.psobject.Properties.Name | Where-Object {$_ -match '\.(cpp|h|def)$'})
if($coreNames.Count -ne 28){throw 'Unexpected retained core source set.'}
$ownNames=@('water_hook_gpu_fixture.cpp','water_hook_gpu_build.ps1');$ownHashes=@{};$leases=@();$coreHashes=@{}
foreach($name in $ownNames){$ownHashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
New-Item -ItemType Directory -Path $output | Out-Null
try {
 foreach($name in $coreNames){
  $pin=$manifest.files.$name
  if((Get-FileHash -LiteralPath (Join-Path $core $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin){throw 'Retained core source changed.'}
  Copy-Item -LiteralPath (Join-Path $core $name) -Destination (Join-Path $output $name)
  $coreHashes[$name]=$pin
 }
 foreach($name in $ownNames){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name)}
 foreach($name in @($coreNames)+$ownNames){$leases += [IO.File]::Open((Join-Path $output $name),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
 $finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
 $installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 $setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
 $lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
 if($LASTEXITCODE){throw 'Cannot initialize x86 compiler.'}
 foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
 Push-Location $output
 try {
  $components=@('water_shader_effects.cpp','supersampling_hook.cpp','supersampling_hook_capture.cpp','material_shader_observer.cpp','material_shader_effects.cpp','material_comparison.cpp','graphics_observation.cpp')
  & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /DS7_SUPERSAMPLING_FIXTURE /DS7_WATER_SHADER_FIXTURE @components water_hook_gpu_fixture.cpp /Fe:WaterHookGpuFixture.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib d3dcompiler.lib /DYNAMICBASE /NXCOMPAT /Brepro
  if($LASTEXITCODE){throw 'Integrated public Water GPU fixture compile failed.'}
  foreach($name in $coreNames){if((Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $coreHashes[$name]){throw 'Compiled core snapshot changed.'}}
  foreach($name in $ownNames){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ownHashes[$name] -or
      (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ownHashes[$name]){throw 'Fixture source changed during compile.'}}
  Copy-Item -LiteralPath $manifestPath -Destination 'core-manifest.private.json'
  @{format='s7-water-hook-gpu-build-v1';core_source_set=$manifest.source_set;core_sources=$coreHashes;sources=$ownHashes;
    core_manifest_sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant();
    exe_sha256=(Get-FileHash -LiteralPath 'WaterHookGpuFixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant();executed=$false} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath 'build.private.json' -Encoding utf8
 } finally {Pop-Location}
} finally {foreach($lease in $leases){$lease.Dispose()}}
