param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a fresh directory below work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize the x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('on12_frame_context.h','on12_frame_context.cpp','on12_spatial_pass.h','on12_spatial_pass.cpp','on12_spatial_pass.hlsl','on12_spatial_gpu.cpp','on12_spatial_hook.cpp','on12_spatial_hook.def','water_on12_identity.h','on12_output_window.h','on12_output_window.cpp','on12_output_window_fixture.cpp','on12_scene_targets.h','on12_scene_targets.cpp','on12_scene_boundary.h','on12_scene_abi_fixture.cpp','on12_temporal_inputs.h','on12_temporal_inputs_fixture.cpp')
$files=@{}
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output;$files[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try {
    $shader=[IO.File]::ReadAllText((Join-Path $output 'on12_spatial_pass.hlsl'))
    if($shader.Contains(')WEPLAY"')){throw 'Shader contains the embedding delimiter.'}
    [IO.File]::WriteAllText((Join-Path $output 'on12_spatial_shader.generated.h'),'constexpr char On12SpatialShader[]=R"WEPLAY('+$shader+')WEPLAY";')
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 on12_scene_abi_fixture.cpp /Fe:on12_scene_abi_fixture.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'On12 scene SDK ABI fixture compile failed.'}
    & .\on12_scene_abi_fixture.exe
    if($LASTEXITCODE){throw 'On12 scene SDK ABI fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 on12_temporal_inputs_fixture.cpp /Fe:on12_temporal_inputs_fixture.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'On12 temporal input fixture compile failed.'}
    & .\on12_temporal_inputs_fixture.exe
    if($LASTEXITCODE){throw 'On12 temporal input fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 on12_output_window.cpp on12_output_window_fixture.cpp /Fe:on12_output_window_fixture.exe /link user32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'On12 output window fixture compile failed.'}
    & .\on12_output_window_fixture.exe
    if($LASTEXITCODE){throw 'On12 output window fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /LD on12_frame_context.cpp on12_spatial_pass.cpp on12_spatial_hook.cpp on12_output_window.cpp on12_scene_targets.cpp /Fe:WePlayOn12SpatialLab.dll /link /DEF:on12_spatial_hook.def d3d12.lib d3dcompiler.lib bcrypt.lib user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'On12 spatial host compile failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /DS7_ON12_SPATIAL_GPU_FIXTURE on12_frame_context.cpp on12_spatial_pass.cpp on12_spatial_hook.cpp on12_output_window.cpp on12_scene_targets.cpp on12_spatial_gpu.cpp /Fe:on12_spatial_gpu.exe /link d3d12.lib d3dcompiler.lib user32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'On12 spatial GPU fixture compile failed.'}
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]){throw 'Spatial source changed during compilation.'}}
    $files['on12_spatial_gpu.exe']=(Get-FileHash -LiteralPath 'on12_spatial_gpu.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $files['WePlayOn12SpatialLab.dll']=(Get-FileHash -LiteralPath 'WePlayOn12SpatialLab.dll' -Algorithm SHA256).Hash.ToLowerInvariant()
    $files['on12_output_window_fixture.exe']=(Get-FileHash -LiteralPath 'on12_output_window_fixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $files['on12_scene_abi_fixture.exe']=(Get-FileHash -LiteralPath 'on12_scene_abi_fixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $files['on12_temporal_inputs_fixture.exe']=(Get-FileHash -LiteralPath 'on12_temporal_inputs_fixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest=@{format='s7-on12-spatial-build-v1';settings_bytes=16;build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$files}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'on12-spatial-build.json' -Encoding utf8
    $manifest | ConvertTo-Json -Depth 4 -Compress
} finally {Pop-Location}
