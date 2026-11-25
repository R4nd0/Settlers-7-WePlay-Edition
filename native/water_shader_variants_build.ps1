param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$ConfigA,[string]$ConfigB,[string]$Report)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=[IO.Path]::GetFullPath((Join-Path $repository 'work'))+[IO.Path]::DirectorySeparatorChar
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh work/ output.'}
if(($ConfigA -or $ConfigB -or $Report) -and !($ConfigA -and $ConfigB -and $Report)){throw 'Supply both staged configs and their trusted report together.'}
foreach($path in @($ConfigA,$ConfigB,$Report)){if($path -and ![IO.Path]::GetFullPath($path).StartsWith($work,[StringComparison]::OrdinalIgnoreCase)){throw 'CPU load inputs must stay in work/.'}}
$names=@('water_shader_effects.h','water_shader_effects.cpp','water_shader_variants_fixture.cpp','water_shader_variants_build.ps1');$hashes=@{};$leases=@()
New-Item -ItemType Directory -Path $output | Out-Null
foreach($name in $names){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant();Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name)}
try {
 foreach($name in $names){$leases += [IO.File]::Open((Join-Path $output $name),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
 $finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
 $installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 $setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
 $lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
 if($LASTEXITCODE){throw 'Cannot initialize x86 compiler'}
 foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
 $a=if($ConfigA){[IO.Path]::GetFullPath($ConfigA)}else{''};$b=if($ConfigB){[IO.Path]::GetFullPath($ConfigB)}else{''};$r=if($Report){[IO.Path]::GetFullPath($Report)}else{''}
 Push-Location $output
 try {
  & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict water_shader_effects.cpp water_shader_variants_fixture.cpp /Fe:WaterShaderVariantsFixture.exe /link bcrypt.lib
  if($LASTEXITCODE){throw 'Water variant production CPU fixture compile failed'}
  if($a){& .\WaterShaderVariantsFixture.exe $a $b $r | Tee-Object -FilePath 'fixture.private.json';if($LASTEXITCODE){throw 'Water variant production CPU fixture failed'}}
  foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name] -or
      (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw 'Variant fixture source changed'}}
  @{format='s7-water-variant-cpu-build-v1';sources=$hashes;exe_sha256=(Get-FileHash -LiteralPath 'WaterShaderVariantsFixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant();gpu=$false;game=$false} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'build.private.json' -Encoding utf8
 } finally {Pop-Location}
} finally {foreach($lease in $leases){$lease.Dispose()}}
