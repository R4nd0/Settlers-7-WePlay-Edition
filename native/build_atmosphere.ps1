param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$Lab,[switch]$Gold,[switch]$GoldPreUI)
$ErrorActionPreference='Stop'
if($Gold -and $Lab){throw 'Gold uses its separate production-period, hash-locked candidate; no lab exception.'}
if($GoldPreUI -and (!$Gold -or $Lab)){throw 'GoldPreUI requires the exact Gold profile without Lab.'}
$source=$PSScriptRoot
$repository=[IO.Path]::GetFullPath((Join-Path $source '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a new directory inside repository work/.'}
$found=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $found 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'C++ toolchain initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $output
try {
 $common=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/arch:SSE2','/fp:strict','/DNDEBUG',('/I'+$source))
 & cl.exe @common (Join-Path $source 'atmosphere_clock_tests.cpp') /Fe:atmosphere_clock_tests.exe
 if($LASTEXITCODE){throw 'Atmosphere clock tests did not compile'}
 & .\atmosphere_clock_tests.exe
 if($LASTEXITCODE){throw 'Atmosphere clock regression failed'}
 if($GoldPreUI){
  $fixtureFlags=@($common | Where-Object {$_ -ne '/DNDEBUG'})
  foreach($fixture in @('gold_pre_ui_state_tests','gold_pre_ui_pipeline_tests')){
   & cl.exe @fixtureFlags (Join-Path $source ($fixture+'.cpp')) ('/Fe:'+ $fixture+'.exe')
   if($LASTEXITCODE){throw 'Gold Vulkan fixtures did not compile'}
   & (Join-Path $output ($fixture+'.exe'))
   if($LASTEXITCODE){throw 'Gold Vulkan regression failed'}
  }
 }
 if($Lab){$common+='/DWEPLAY_ATMOSPHERE_LAB'}
 if($Gold){$common+='/DWEPLAY_ATMOSPHERE_GOLD'}
 if($GoldPreUI){$common+='/DWEPLAY_GOLD_PRE_UI'}
 & cl.exe @common /LD (Join-Path $source 'atmosphere_addon.cpp') /Fe:WePlayAtmosphere.addon32 /link /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'Atmosphere add-on did not compile'}
 $sha=[Security.Cryptography.SHA256]::Create()
 try {[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $output 'WePlayAtmosphere.addon32')))).Replace('-','').ToLowerInvariant()} finally {$sha.Dispose()}
} finally {Pop-Location}
