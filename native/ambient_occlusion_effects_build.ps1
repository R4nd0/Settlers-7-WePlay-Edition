param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=$PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repo 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a new owned work output directory.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Compiler initialization failed.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('ambient_occlusion_policy.h','ambient_occlusion_policy_tests.cpp','ambient_occlusion_effects.h','ambient_occlusion_effects.cpp','ambient_occlusion_effects_fixture.cpp','shadow_filter_effects.h','shadow2048_filter_policy.h')
function Digest([string]$Path){$sha=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
$hashes=@{};$leases=@()
Push-Location $output
try {
    foreach($name in $names){$hashes[$name]=Digest (Join-Path $source $name);Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $output $name);$leases+=,[IO.File]::Open((Join-Path $output $name),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
    $flags=@('/nologo','/std:c++17','/EHsc','/MT','/W4','/WX','/O2','/fp:strict')
    & cl.exe @flags ambient_occlusion_policy_tests.cpp /Fe:policy_tests.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'AO policy compile failed.'}
    $policy=& (Join-Path $output 'policy_tests.exe')
    if($LASTEXITCODE){throw 'AO policy checks failed.'}
    & cl.exe @flags /c ambient_occlusion_effects.cpp /Fo:ambient_occlusion_effects.production.obj
    if($LASTEXITCODE){throw 'AO production compile failed.'}
    & cl.exe @flags /DS7_AMBIENT_OCCLUSION_FIXTURE ambient_occlusion_effects.cpp ambient_occlusion_effects_fixture.cpp /Fe:AmbientOcclusionEffectsFixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'AO fake COM compile failed.'}
    $runtime=& (Join-Path $output 'AmbientOcclusionEffectsFixture.exe')
    if($LASTEXITCODE){throw 'AO fake COM checks failed.'}
    foreach($name in $names){if((Digest (Join-Path $source $name)) -ne $hashes[$name]){throw 'Source changed during build.'}}
    $receipt=@{format='s7-ambient-occlusion-cpu-build-v1';sources=$hashes;policy=($policy|ConvertFrom-Json);runtime=($runtime|ConvertFrom-Json);gpu_started=$false;game_started=$false;shared_sources_modified=$false}
    [IO.File]::WriteAllText((Join-Path $output 'build.private.json'),($receipt|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    $policy
    $runtime
}finally{foreach($lease in $leases){$lease.Dispose()};Pop-Location}
