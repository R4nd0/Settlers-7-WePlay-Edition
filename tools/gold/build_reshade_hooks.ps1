param([Parameter(Mandatory=$true)][string]$SourceDirectory,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$BuildRuntime)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=[IO.Path]::GetFullPath($SourceDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$work=(Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar
if(!$source.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or
   !$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or
   (Test-Path -LiteralPath $output)){throw 'Use existing developer source and a new output directory inside work/.'}
& python (Join-Path $PSScriptRoot 'reshade_hooks.py') $source --apply
if($LASTEXITCODE){throw 'Exact upstream source/patch verification failed'}
$found=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$found){throw 'Visual Studio x86 C++ tools are required'}
$setup=Join-Path $found 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Toolchain initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
& python (Join-Path $PSScriptRoot 'reshade_hooks.py') $source --fixture (Join-Path $output 'fixtures.cpp')
if($LASTEXITCODE){throw 'Hook fixture extraction failed'}
Push-Location $output
try {
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 ('/I'+(Join-Path $source 'deps/glad/target/include')) fixtures.cpp /Fe:fixtures.exe
 if($LASTEXITCODE){throw 'Hook fixtures did not compile'}
 & ./fixtures.exe
 if($LASTEXITCODE){throw 'Hook fixtures failed'}
 if($BuildRuntime){
  $msbuild=Join-Path $found 'MSBuild/Current/Bin/MSBuild.exe'
  & $msbuild (Join-Path $source 'ReShade.vcxproj') /m:4 /nologo /v:minimal /p:Configuration=Release /p:Platform=Win32 ('/p:SolutionDir='+$source+'\') ('/p:OutDir='+$output+'\runtime\') ('/p:IntDir='+$output+'\obj\')
  if($LASTEXITCODE){throw 'ReShade build failed'}
  Get-FileHash -Algorithm SHA256 (Join-Path $output 'runtime/ReShade32.dll')
 }
} finally {Pop-Location}
Write-Output 'Developer fixtures completed. No runtime was installed or launched.'
