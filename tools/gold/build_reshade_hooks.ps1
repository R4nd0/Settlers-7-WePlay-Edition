param([Parameter(Mandatory=$true)][string]$SourceDirectory,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [switch]$BuildRuntime,
      [switch]$DisableOverlay)
$ErrorActionPreference='Stop'
if($DisableOverlay -and !$BuildRuntime){throw 'DisableOverlay requires BuildRuntime.'}
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
  $properties=Join-Path $output 'intermediates.props'
  $escapedOutput=[System.Security.SecurityElement]::Escape($output)
  $extraSource=''
  if($DisableOverlay){
   Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'reshade_no_overlay.cpp') -Destination (Join-Path $output 'reshade_no_overlay.cpp')
   $extraSource='<ItemGroup Condition="''$(MSBuildProjectName)''==''ReShade''"><ClCompile Include="'+$escapedOutput+'\reshade_no_overlay.cpp"><ObjectFileName>$(IntDir)reshade_no_overlay.obj</ObjectFileName></ClCompile></ItemGroup>'
  }
  $compilerProperties='<ItemDefinitionGroup><ClCompile><DebugInformationFormat>OldStyle</DebugInformationFormat>'
  if($DisableOverlay){$compilerProperties+='<UndefinePreprocessorDefinitions>RESHADE_GUI;%(UndefinePreprocessorDefinitions)</UndefinePreprocessorDefinitions>'}
  $compilerProperties+='</ClCompile></ItemDefinitionGroup>'
  ('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><IntDir>'+ $escapedOutput +'\obj\$(MSBuildProjectName)\</IntDir></PropertyGroup>'+$compilerProperties+$extraSource+'</Project>') | Set-Content -LiteralPath $properties -Encoding utf8
  $previousCompilerTail=$env:_CL_
  try {
   # MSVC appends _CL_ after project options. /U takes precedence over /D;
   # the upstream RESHADE_GUI guards remove menu, splash, statistics, input
   # capture and GUI rendering, while the effect renderer/add-on API remain.
   # Embedded compiler debug info avoids sharing an MSVC PDB manager between
   # the independent dependency projects (including on mapped workspaces).
   $env:_CL_=($previousCompilerTail+' /Z7 /MP2'+$(if($DisableOverlay){' /URESHADE_GUI'}else{''})).Trim()
   # Import after Common.props so it cannot redirect IntDir into the upstream
   # shared cache. GUI/no-GUI objects have different runtime class layouts.
   & $msbuild (Join-Path $source 'ReShade.vcxproj') /m:2 /nodeReuse:false /nologo /v:minimal /p:Configuration=Release /p:Platform=Win32 ('/p:SolutionDir='+$source+'\') ('/p:OutDir='+$output+'\runtime\') ('/p:ForceImportBeforeCppTargets='+$properties)
   if($LASTEXITCODE){throw 'ReShade build failed'}
  } finally {$env:_CL_=$previousCompilerTail}
  if($DisableOverlay){
   $objects=@(Get-ChildItem -LiteralPath (Join-Path $output 'obj/ReShade/source') -Filter '*.obj' -File -Recurse)
   if($objects.Count -lt 40){throw 'Expected a fresh private build of all runtime source files.'}
   $bytes=[IO.File]::ReadAllBytes((Join-Path $output 'runtime/ReShade32.dll'))
   $text=[Text.Encoding]::ASCII.GetString($bytes)
   if($text.Contains('Splash Window') -or $text.Contains('Overlay shortcut key')){throw 'GUI strings survived the no-overlay build.'}
   'ReShade GUI excluded at compile time; effects and managed add-ons retained.' | Set-Content -LiteralPath (Join-Path $output 'no-overlay.txt') -Encoding ascii
  }
  $digest=[Security.Cryptography.SHA256]::Create()
  $stream=[IO.File]::OpenRead((Join-Path $output 'runtime/ReShade32.dll'))
  try {[BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
  finally {$stream.Dispose();$digest.Dispose()}
 }
} finally {Pop-Location}
Write-Output 'Developer fixtures completed. No runtime was installed or launched.'
