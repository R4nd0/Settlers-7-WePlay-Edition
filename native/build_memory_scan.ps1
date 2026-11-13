param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$scanOutput = [IO.Path]::GetFullPath($OutputDirectory)
$scanRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$scanOutput.StartsWith((Join-Path $scanRepo 'work') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use a new private directory under work.'
}
if (Test-Path -LiteralPath $scanOutput) { throw 'Output already exists.' }
New-Item -ItemType Directory -Path $scanOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ Build Tools are required.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$scanEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize x64 compiler.' }
foreach ($line in $scanEnvironment) {
    $position = $line.IndexOf('=')
    if ($position -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$position),$line.Substring($position+1),'Process') }
}
Push-Location $scanOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /O2 /W4 /WX /LD /Brepro (Join-Path $PSScriptRoot 'memory_scan.cpp') /Fe:memory_scan.dll /link /Brepro /DYNAMICBASE /NXCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Memory scanner build failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /O2 /W4 /WX /Brepro (Join-Path $PSScriptRoot 'memory_scan_tests.cpp') /Fe:memory_scan_tests.exe /link /Brepro
    if ($LASTEXITCODE -ne 0) { throw 'Memory scanner test build failed.' }
    & (Join-Path $scanOutput 'memory_scan_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Memory scanner tests failed.' }
    $stream = [IO.File]::OpenRead((Join-Path $scanOutput 'memory_scan.dll'))
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $scanHash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
    $scanCache = Join-Path $scanRepo ('work/memory-scan/' + $scanHash)
    New-Item -ItemType Directory -Force -Path $scanCache | Out-Null
    $scanCachedDll = Join-Path $scanCache 'memory_scan.dll'
    if (!(Test-Path -LiteralPath $scanCachedDll)) {
        [IO.File]::Copy((Join-Path $scanOutput 'memory_scan.dll'), $scanCachedDll, $false)
    }
    $stream = [IO.File]::OpenRead($scanCachedDll)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $cachedHash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
    if ($cachedHash -ne $scanHash) { throw 'Existing private scanner cache differs; it was not overwritten.' }
    Write-Output $scanHash
} finally { Pop-Location }
