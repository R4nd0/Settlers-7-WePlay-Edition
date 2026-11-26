param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a fresh build directory below work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$files=@{};$names=@('water_on12_probe.cpp','water_on12_probe.def','water_on12_identity.h','water_on12_pow_cap.h','water_on12_probe_gpu.cpp')
foreach($name in $names) {Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output;$files[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /LD water_on12_probe.cpp /Fe:WePlayWaterOn12Probe.dll /link /DEF:water_on12_probe.def bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Water observer DLL compile failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict /DS7_WATER_ON12_PROBE_FIXTURE water_on12_probe.cpp /Fe:water_on12_probe_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Water observer CPU fixture compile failed.'}
    & .\water_on12_probe_fixture.exe
    if ($LASTEXITCODE) {throw 'Water observer CPU fixture failed.'}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict water_on12_probe_gpu.cpp /Fe:water_on12_probe_gpu.exe /link bcrypt.lib user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Water observer GPU fixture compile failed.'}
    foreach($name in $names) {if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]) {throw 'Source changed during build.'}}
    foreach($name in @('WePlayWaterOn12Probe.dll','water_on12_probe_fixture.exe','water_on12_probe_gpu.exe')) {$files[$name]=(Get-FileHash -LiteralPath $name -Algorithm SHA256).Hash.ToLowerInvariant()}
    $manifest=@{format='s7-water-on12-probe-build-v2';config_bytes=1036;supports_pow_cap=$true;build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$files}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'water-probe-build.json' -Encoding utf8
    $manifest | ConvertTo-Json -Depth 4 -Compress
} finally {Pop-Location}
