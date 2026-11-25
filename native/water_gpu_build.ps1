param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
$work=[IO.Path]::GetFullPath((Join-Path $repository 'work'))
if([IO.Path]::GetDirectoryName($output) -ne $work -or (Test-Path -LiteralPath $output)){throw 'Use a fresh direct child of repository work/.'}
$names=@('water_gpu_fixture.cpp','water_shader_effects.cpp','water_shader_effects.h','water_gpu_build.ps1')
$hashes=@{};$leases=@()
foreach($name in $names){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
New-Item -ItemType Directory -Path $output | Out-Null
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name)}
try {
 foreach($name in $names){$leases += [IO.File]::Open((Join-Path $output $name),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
 $finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
 $installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 $setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
 $lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
 if($LASTEXITCODE){throw 'Cannot initialize x86 compiler.'}
 foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
 Push-Location $output
 try {
  & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /DS7_WATER_SHADER_FIXTURE water_gpu_fixture.cpp water_shader_effects.cpp /Fe:WaterGpuFixture.exe /link bcrypt.lib d3dcompiler.lib user32.lib
  if($LASTEXITCODE){throw 'Authored water GPU fixture compile failed.'}
  foreach($name in $names){
   if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name] -or
      (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw 'Water GPU fixture source changed.'}
  }
  @{format='s7-water-gpu-build-v1';sources=$hashes;exe_sha256=(Get-FileHash -LiteralPath 'WaterGpuFixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant();executed=$false} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'build.private.json' -Encoding utf8
 } finally {Pop-Location}
} finally {foreach($lease in $leases){$lease.Dispose()}}
