param([Parameter(Mandatory=$true)][string]$TemporalBuild,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=[IO.Path]::GetFullPath($TemporalBuild)
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh pipeline build below work/.'}
& python -c 'import sys; from pathlib import Path; from tools.graphics_lab.on12_temporal import verify_build; print(verify_build(Path(sys.argv[1]))[1].get(''architecture''))' $source
if($LASTEXITCODE){throw 'Temporal preparation is stale or unverified.'}
$base=Get-Content -LiteralPath (Join-Path $source 'on12-temporal-build.json') -Raw | ConvertFrom-Json
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$architecture=if($base.architecture -eq 'x64'){'amd64'}else{'x64_x86'}
$lines=& $env:ComSpec /d /c "call `"$setup`" $architecture >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the C++ compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$files=@{}
foreach($entry in $base.files.PSObject.Properties){Copy-Item -LiteralPath (Join-Path $source $entry.Name) -Destination $output;$files[$entry.Name]=$entry.Value}
Copy-Item -LiteralPath (Join-Path $source 'on12-temporal-build.json') -Destination $output
$names=@('on12_temporal_pipeline.h','on12_temporal_pipeline.cpp','on12_temporal_pipeline_gpu.cpp')
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output;$files[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict on12_temporal_pipeline.cpp on12_temporal_pipeline_gpu.cpp on12_temporal.cpp on12_temporal_pass.cpp on12_temporal_camera.cpp on12_temporal_camera_pass.cpp on12_temporal_graph.cpp /Fe:on12_temporal_pipeline_gpu.exe /link d3d12.lib d3dcompiler.lib dxgi.lib dxguid.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Temporal pipeline preparation failed.'}
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]){throw 'Pipeline source changed during compilation.'}}
    foreach($entry in $base.files.PSObject.Properties){if((Get-FileHash -LiteralPath $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw 'Temporal preparation changed during copying.'}}
    $files['on12_temporal_pipeline_gpu.exe']=(Get-FileHash -LiteralPath 'on12_temporal_pipeline_gpu.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest=@{format='s7-on12-temporal-pipeline-build-v1';architecture=$base.architecture;build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();temporal_build_sha256=(Get-FileHash -LiteralPath (Join-Path $source 'on12-temporal-build.json') -Algorithm SHA256).Hash.ToLowerInvariant();files=$files;gpu_executed=$false}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'on12-temporal-pipeline-build.json' -Encoding utf8
    Write-Output 'Temporal pipeline GPU fixture prepared; no GPU device created by this builder.'
}finally{Pop-Location}
