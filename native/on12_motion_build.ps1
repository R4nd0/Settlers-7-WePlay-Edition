param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$Packed)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh motion build below work/.'}
$names=@('native/on12_motion_gpu.cpp','native/water_on12_identity.h','tools/graphics_lab/on12_previous_source.py','tools/graphics_lab/on12_motion_program.py','tools/graphics_lab/on12_motion.py','tools/graphics_lab/on12_motion_pixel.py')
$files=@{}
foreach($name in $names){$files[$name]=(Get-FileHash -LiteralPath (Join-Path $repository $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
$arguments=@('--prepare',$output);if($Packed){$arguments+='--packed'}
& python -m tools.graphics_lab.on12_motion @arguments
if($LASTEXITCODE){throw 'Authored motion shader preparation failed.'}
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $repository $name) -Destination $output}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict on12_motion_gpu.cpp /Fe:on12_motion_gpu.exe /link bcrypt.lib user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Motion GPU fixture preparation failed.'}
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $repository $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]){throw 'Motion source changed during compilation.'}}
    foreach($name in @('current.vs.bin','combined.vs.bin','reference.vs.bin','color.ps.bin','previous.ps.bin','combined.ps.bin','motion-reference.ps.bin','motion-layout.bin','on12_motion_gpu.exe')){$files[$name]=(Get-FileHash -LiteralPath $name -Algorithm SHA256).Hash.ToLowerInvariant()}
    $manifest=@{format='s7-on12-motion-build-v1';build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$files;gpu_executed=$false;packed=[bool]$Packed}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'on12-motion-build.json' -Encoding utf8
    Write-Output 'Authored motion GPU fixture prepared without creating a GPU device.'
}finally{Pop-Location}
