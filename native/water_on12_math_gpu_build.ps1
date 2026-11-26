param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a fresh build directory below work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set";if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$source=Join-Path $PSScriptRoot 'water_on12_math_gpu.cpp';$pin=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item -LiteralPath $source -Destination $output
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict water_on12_math_gpu.cpp /Fe:water_on12_math_gpu.exe /link user32.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Water scalar reproduction compile failed.'}
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin) {throw 'Source changed during build.'}
    @{format='s7-water-on12-math-build-v1';source_sha256=$pin;binary_sha256=(Get-FileHash -LiteralPath 'water_on12_math_gpu.exe' -Algorithm SHA256).Hash.ToLowerInvariant();build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json | Set-Content -LiteralPath 'math-build.json' -Encoding utf8
} finally {Pop-Location}
