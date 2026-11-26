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
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$source=Join-Path $PSScriptRoot 'dxwrapper_device_fixture.cpp'
$pin=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $source -Destination $output
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 dxwrapper_device_fixture.cpp /Fe:dxwrapper_device_fixture.exe /link user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Device fixture compile failed.'}
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin) {throw 'Source changed during build.'}
    $manifest=@{format='s7-dxwrapper-device-fixture-v1';source_sha256=$pin;binary_sha256=(Get-FileHash -LiteralPath 'dxwrapper_device_fixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant();build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    $manifest | ConvertTo-Json | Set-Content -LiteralPath 'build.json' -Encoding utf8
    $manifest | ConvertTo-Json -Compress
} finally {Pop-Location}
