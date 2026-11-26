param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$SourceDirectory='',[string]$ConfigPath='',[string]$ConfigV1Path='',[string]$ConfigV3Path='',[string]$ConfigV4Path='')
$ErrorActionPreference='Stop'
if (!$SourceDirectory) {$SourceDirectory=$PSScriptRoot}
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
$source=[IO.Path]::GetFullPath($SourceDirectory)
$work=(Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar
if (!$output.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a new output directory inside repository work/.'}
if ($source -ne $PSScriptRoot -and !$source.StartsWith($work,[StringComparison]::OrdinalIgnoreCase)) {throw 'Source must be native/ or an owned private work draft.'}
if ($ConfigPath) {$ConfigPath=[IO.Path]::GetFullPath($ConfigPath);if(!$ConfigPath.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $ConfigPath -PathType Leaf)){throw 'Use an owned private work ConfigV2 file.'}}
if ($ConfigV1Path) {$ConfigV1Path=[IO.Path]::GetFullPath($ConfigV1Path);if(!$ConfigV1Path.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $ConfigV1Path -PathType Leaf)){throw 'Use an owned private work ConfigV1 file.'}}
if ($ConfigV3Path) {$ConfigV3Path=[IO.Path]::GetFullPath($ConfigV3Path);if(!$ConfigV3Path.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $ConfigV3Path -PathType Leaf)){throw 'Use an owned private work ConfigV3 file.'}}
if ($ConfigV4Path) {$ConfigV4Path=[IO.Path]::GetFullPath($ConfigV4Path);if(!$ConfigV4Path.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $ConfigV4Path -PathType Leaf)){throw 'Use an owned private work ConfigV4 file.'}}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$paths=@{}
foreach($name in @('material_shader_effects.cpp','material_shader_effects.h','material_shader_effects_fixture.cpp','material_shader_config_v2.h','material_shader_config_v3.h','material_shader_config_v4.h','terrain_macro_guard.h')) {$paths[$name]=Join-Path $source $name}
$paths['material_shader_effects_contract_v4.h']=Join-Path $source 'material_shader_effects_contract_v4.h'
$paths['material_shader_effects_v4_fixture.cpp']=Join-Path $PSScriptRoot 'material_shader_effects_v4_fixture.cpp'
$paths['material_shader_effects_v4_load_fixture.cpp']=Join-Path $PSScriptRoot 'material_shader_effects_v4_load_fixture.cpp'
$fixture=Join-Path $PSScriptRoot 'material_shader_effects_v2_fixture.cpp'
if(Test-Path -LiteralPath $fixture){$paths['material_shader_effects_v2_fixture.cpp']=$fixture}
$loadFixture=Join-Path $PSScriptRoot 'material_shader_effects_v2_load_fixture.cpp'
if(Test-Path -LiteralPath $loadFixture){$paths['material_shader_effects_v2_load_fixture.cpp']=$loadFixture}
$macroFixture=Join-Path $PSScriptRoot 'material_shader_effects_macro_fixture.cpp'
if(Test-Path -LiteralPath $macroFixture){$paths['material_shader_effects_macro_fixture.cpp']=$macroFixture}
$v3Load=Join-Path $PSScriptRoot 'material_shader_effects_v3_load_fixture.cpp'
if(Test-Path -LiteralPath $v3Load){$paths['material_shader_effects_v3_load_fixture.cpp']=$v3Load;foreach($name in @('material_shader_effects_contract_v3.h','material_shader_effects_contract_v2.h','supersampling_hook_config.h','supersampling_hook.h')){$paths[$name]=Join-Path $source $name}}
$leases=@();$hashes=@{};$sha=[Security.Cryptography.SHA256]::Create()
Push-Location $output
try {
    foreach($name in $paths.Keys) {$bytes=[IO.File]::ReadAllBytes($paths[$name]);$hashes[$name]=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant();$snapshot=Join-Path $output $name;[IO.File]::WriteAllBytes($snapshot,$bytes);$leases+=,[IO.File]::Open($snapshot,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
    $flags=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2')
    & cl.exe @flags /c material_shader_effects.cpp /Fo:material_shader_effects.production.obj
    if($LASTEXITCODE){throw 'Production V1/V2 effects compile failed.'}
    $unit=if($paths.ContainsKey('material_shader_effects_v2_fixture.cpp')){'material_shader_effects_v2_fixture.cpp'}else{'material_shader_effects_fixture.cpp'}
    & cl.exe @flags /DS7_MATERIAL_EFFECTS_FIXTURE material_shader_effects.cpp $unit /Fe:material_shader_effects_v2_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'V1/V2 material effects CPU fixture compile failed.'}
    & (Join-Path $output 'material_shader_effects_v2_fixture.exe') (Join-Path $output 'fixture')
    if($LASTEXITCODE){throw 'V1/V2 material effects CPU fixture failed.'}
    if($paths.ContainsKey('material_shader_effects_macro_fixture.cpp')){
        & cl.exe @flags /DS7_MATERIAL_EFFECTS_FIXTURE material_shader_effects.cpp material_shader_effects_macro_fixture.cpp /Fe:material_shader_effects_macro_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
        if($LASTEXITCODE){throw 'Macro material effects CPU fixture compile failed.'}
        & (Join-Path $output 'material_shader_effects_macro_fixture.exe') (Join-Path $output 'macro-fixture')
        if($LASTEXITCODE){throw 'Macro material effects CPU fixture failed.'}
    }
    & cl.exe @flags /DS7_MATERIAL_EFFECTS_FIXTURE material_shader_effects.cpp material_shader_effects_v4_fixture.cpp /Fe:material_shader_effects_v4_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Palette CPU fixture compile failed.'}
    & (Join-Path $output 'material_shader_effects_v4_fixture.exe') (Join-Path $output 'palette-fixture')
    if($LASTEXITCODE){throw 'Palette CPU fixture failed.'}
    & cl.exe @flags material_shader_effects.cpp material_shader_effects_v4_load_fixture.cpp /Fe:material_shader_effects_v4_load_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Production V4 Host+module load compile failed.'}
    if($ConfigV4Path){& (Join-Path $output 'material_shader_effects_v4_load_fixture.exe') $ConfigV4Path;if($LASTEXITCODE){throw 'Production V4 Host+module load failed.'}}
    if($paths.ContainsKey('material_shader_effects_v2_load_fixture.cpp')){
        & cl.exe @flags material_shader_effects.cpp material_shader_effects_v2_load_fixture.cpp /Fe:material_shader_effects_v2_load_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
        if($LASTEXITCODE){throw 'Production V2 pin-load CPU fixture compile failed.'}
        if($ConfigPath){& (Join-Path $output 'material_shader_effects_v2_load_fixture.exe') $ConfigPath;if($LASTEXITCODE){throw 'Production V2 pin-load CPU fixture failed.'}}
        if($ConfigV1Path){& (Join-Path $output 'material_shader_effects_v2_load_fixture.exe') $ConfigV1Path;if($LASTEXITCODE){throw 'Production V1 pin-load CPU fixture failed.'}}
    }
    if($paths.ContainsKey('material_shader_effects_v3_load_fixture.cpp')){
        & cl.exe @flags material_shader_effects.cpp material_shader_effects_v3_load_fixture.cpp /Fe:material_shader_effects_v3_load_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
        if($LASTEXITCODE){throw 'Production V3 Host+module load compile failed.'}
        if($ConfigV3Path){& (Join-Path $output 'material_shader_effects_v3_load_fixture.exe') $ConfigV3Path;if($LASTEXITCODE){throw 'Production V3 Host+module load failed.'}}
    }
    foreach($name in $paths.Keys){$after=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($paths[$name]))).Replace('-','').ToLowerInvariant();if($after -ne $hashes[$name]){throw 'Source changed during build; rebuild.'}}
    $manifest=@{format='s7-native-material-effects-v2-build-v1';gpu_started=$false;game_started=$false;cpu_checks_passed=$true;sources=$hashes;private_source_draft=($source -ne $PSScriptRoot);production_v2_pin_load_cpu_checked=[bool]$ConfigPath;production_v1_pin_load_cpu_checked=[bool]$ConfigV1Path}
    [IO.File]::WriteAllText((Join-Path $output 'material-effects-v2-build.private.json'),($manifest|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
} finally {foreach($lease in $leases){$lease.Dispose()};$sha.Dispose();Pop-Location}
