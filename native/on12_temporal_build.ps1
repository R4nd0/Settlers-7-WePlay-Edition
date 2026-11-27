param([Parameter(Mandatory=$true)][string]$OutputDirectory,[ValidateSet('x86','x64')][string]$Architecture='x86')
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh temporal build below work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$compilerArchitecture=if($Architecture -eq 'x64'){'amd64'}else{'x64_x86'}
$lines=& $env:ComSpec /d /c "call `"$setup`" $compilerArchitecture >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('on12_temporal.h','on12_temporal.cpp','on12_temporal.hlsl','on12_temporal_pass.h','on12_temporal_pass.cpp','on12_temporal_fixture.cpp','on12_temporal_gpu.cpp','on12_temporal_inputs.h','on12_temporal_inputs_fixture.cpp','on12_temporal_camera.h','on12_temporal_camera.cpp','on12_temporal_camera.hlsl','on12_temporal_camera_fixture.cpp','on12_temporal_camera_pass.h','on12_temporal_camera_pass.cpp')
$files=@{}
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output;$files[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try{
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 on12_temporal_inputs_fixture.cpp /Fe:on12_temporal_inputs_fixture.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Temporal input CPU fixture compile failed.'}
    & .\on12_temporal_inputs_fixture.exe
    if($LASTEXITCODE){throw 'Temporal input CPU fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict on12_temporal_camera.cpp on12_temporal_camera_pass.cpp on12_temporal_camera_fixture.cpp /Fe:on12_temporal_camera_fixture.exe /link d3d12.lib d3dcompiler.lib dxguid.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Temporal camera CPU fixture compile failed.'}
    $camera=& .\on12_temporal_camera_fixture.exe
    if($LASTEXITCODE){throw 'Temporal camera projection or shader fixture failed.'}
    $camera | Set-Content -LiteralPath 'camera-result.private.json' -Encoding utf8
    $camera
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict on12_temporal.cpp on12_temporal_pass.cpp on12_temporal_fixture.cpp /Fe:on12_temporal_fixture.exe /link d3d12.lib d3dcompiler.lib dxguid.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Temporal CPU fixture compile failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict on12_temporal.cpp on12_temporal_pass.cpp on12_temporal_camera.cpp on12_temporal_camera_pass.cpp on12_temporal_gpu.cpp /Fe:on12_temporal_gpu.exe /link d3d12.lib d3dcompiler.lib dxgi.lib dxguid.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Temporal GPU fixture preparation failed.'}
    $result=& .\on12_temporal_fixture.exe
    if($LASTEXITCODE){throw 'Temporal CPU sequence or shader fixture failed.'}
    $result | Set-Content -LiteralPath 'cpu-result.private.json' -Encoding utf8
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]){throw 'Temporal source changed during compilation.'}}
    foreach($name in @('on12_temporal_fixture.exe','on12_temporal_gpu.exe','on12_temporal_vs.bin','on12_temporal_ps.bin','cpu-result.private.json','on12_temporal_inputs_fixture.exe','on12_temporal_camera_fixture.exe','on12_temporal_camera_vs.bin','on12_temporal_camera_ps.bin','camera-result.private.json')){$files[$name]=(Get-FileHash -LiteralPath $name -Algorithm SHA256).Hash.ToLowerInvariant()}
    $manifest=@{format='s7-on12-temporal-build-v1';architecture=$Architecture;cpu_only=$true;parameter_bytes=64;build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$files}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'on12-temporal-build.json' -Encoding utf8
    $result
}finally{Pop-Location}
