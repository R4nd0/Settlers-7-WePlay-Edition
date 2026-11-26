param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
function FileSha([string]$Path){
 $sha=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))).Replace('-','').ToLowerInvariant()}
 finally{$sha.Dispose()}
}
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
$work=[IO.Path]::GetFullPath((Join-Path $repository 'work'))+[IO.Path]::DirectorySeparatorChar
if(!$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase)-or(Test-Path -LiteralPath $output)){throw 'Use a fresh output directory below work/.'}
$ancestor=[IO.Path]::GetDirectoryName($output)
while($ancestor-and $ancestor.Length-ge($work.Length-1)){
 if(Test-Path -LiteralPath $ancestor){
  $item=Get-Item -LiteralPath $ancestor -Force
  if(!$item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Output ancestors must be ordinary work directories.'}
 }
 $ancestor=[IO.Path]::GetDirectoryName($ancestor)
}
$names=@('shader_debug_overlay.h','shader_debug_overlay.cpp','shader_debug_overlay_fixture.cpp','shader_debug_overlay_build.ps1')
$hashes=@{}
foreach($name in $names){$hashes[$name]=FileSha (Join-Path $PSScriptRoot $name)}
New-Item -ItemType Directory -Path $output|Out-Null
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c ('call "'+$setup+'" x64_x86 >nul && set')
if($LASTEXITCODE){throw 'Cannot initialize x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at-gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
Push-Location $output
try{
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /c shader_debug_overlay.cpp /Fo:shader_debug_overlay.production.obj
 if($LASTEXITCODE){throw 'Production overlay compile failed.'}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict shader_debug_overlay.cpp shader_debug_overlay_fixture.cpp /Fe:ShaderOverlayFixture.exe /link gdi32.lib
 if($LASTEXITCODE){throw 'Authored overlay COM fixture compile failed.'}
 $fixture=& .\ShaderOverlayFixture.exe
 if($LASTEXITCODE){throw 'Authored overlay COM fixture failed.'}
 $result=$fixture|ConvertFrom-Json
 if(!$result.passed-or$result.gpu_or_game-ne$false){throw 'Unexpected fixture result.'}
 foreach($name in $names){if((FileSha (Join-Path $PSScriptRoot $name))-ne$hashes[$name]){throw 'Source changed during build.'}}
 [ordered]@{format='s7-shader-overlay-cpu-v1';passed=$true;sources=$hashes;fixture=$result;
  executable_sha256=(FileSha (Join-Path $output 'ShaderOverlayFixture.exe'));gpu_or_game=$false
 }|ConvertTo-Json -Depth 6|Set-Content -LiteralPath 'receipt.private.json' -Encoding utf8
 $fixture
}finally{Pop-Location}
