param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
$work=[IO.Path]::GetFullPath((Join-Path $repository 'work'))+[IO.Path]::DirectorySeparatorChar
if(!$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh directory below work/.'}
$names=@('water_shader_effects.h','water_shader_effects.cpp','water_shader_effects_fixture.cpp','water_shader_effects_load_fixture.cpp','water_shader_effects_build.ps1')
$hashes=@{}
foreach($name in $names){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
New-Item -ItemType Directory -Path $output | Out-Null
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name)}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
Push-Location $output
try {
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /c water_shader_effects.cpp /Fo:water_shader_effects.production.obj
 if($LASTEXITCODE){throw 'Production water module compile failed.'}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /DS7_WATER_SHADER_FIXTURE water_shader_effects.cpp water_shader_effects_fixture.cpp /Fe:WaterShaderEffectsFixture.exe /link bcrypt.lib
 if($LASTEXITCODE){throw 'Authored water COM fixture compile failed.'}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict water_shader_effects.cpp water_shader_effects_load_fixture.cpp /Fe:WaterShaderProductionLoadFixture.exe /link bcrypt.lib
 if($LASTEXITCODE){throw 'Production water config load fixture compile failed.'}
 & .\WaterShaderEffectsFixture.exe (Join-Path $output 'fixture-case') | Tee-Object -FilePath 'fixture.private.json'
 if($LASTEXITCODE){throw 'Authored water COM fixture failed.'}
 foreach($name in $names){
  if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name] -or
     (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw 'Water fixture source changed during build.'}
 }
 $binaries=@{}
 foreach($name in @('WaterShaderEffectsFixture.exe','WaterShaderProductionLoadFixture.exe','water_shader_effects.production.obj')){$binaries[$name]=(Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
 @{format='s7-water-shader-cpu-build-v1';sources=$hashes;binaries=$binaries;gpu=$false;game=$false} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'build.private.json' -Encoding utf8
} finally {Pop-Location}
