param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repo 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a new work output directory.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Compiler environment initialization failed.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$sources=@{}
foreach($name in @('shadow2048_filter_policy.h','shadow2048_filter_policy_tests.cpp','shadow_filter_effects.h','shadow_filter_effects.cpp','shadow_filter_effects_fixture.cpp')){
    $sources[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $output $name)
}
Push-Location $output
try{
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'shadow2048_filter_policy_tests.cpp') /Fe:policy_tests.exe /link /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Policy compile failed.'}
    $result=& (Join-Path $output 'policy_tests.exe')
    if($LASTEXITCODE){throw 'Policy checks failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /c (Join-Path $output 'shadow_filter_effects.cpp') /Fo:shadow_filter_effects.production.obj
    if($LASTEXITCODE){throw 'Production runtime component compile failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /DS7_SHADOW_FILTER_FIXTURE (Join-Path $output 'shadow_filter_effects.cpp') (Join-Path $output 'shadow_filter_effects_fixture.cpp') /Fe:ShadowFilterEffectsFixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Authored CPU COM fixture compile failed.'}
    $runtime=& (Join-Path $output 'ShadowFilterEffectsFixture.exe')
    if($LASTEXITCODE){throw 'Authored CPU COM checks failed.'}
    foreach($name in $sources.Keys){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sources[$name]){throw 'Source changed during build.'}}
    $receipt=@{format='s7-shadow2048-private-build-v1';sources=$sources;result=($result|ConvertFrom-Json);runtime_result=($runtime|ConvertFrom-Json);binary_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'ShadowFilterEffectsFixture.exe') -Algorithm SHA256).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText((Join-Path $output 'build.private.json'),($receipt|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    $result
    $runtime
}finally{Pop-Location}
