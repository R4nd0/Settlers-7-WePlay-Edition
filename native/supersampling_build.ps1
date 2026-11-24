param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {
    throw 'Use a new output directory inside repository work/.'
}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize the x86 toolchain.'}
foreach ($line in $lines) {
    $position=$line.IndexOf('=')
    if ($position -gt 0) {[Environment]::SetEnvironmentVariable($line.Substring(0,$position),$line.Substring($position+1),'Process')}
}
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $output
try {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {
        $sourceBefore=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'supersampling_probe.cpp')))).Replace('-','').ToLowerInvariant()
        $buildBefore=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($PSCommandPath))).Replace('-','').ToLowerInvariant()
    } finally {$sha.Dispose()}
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 (Join-Path $PSScriptRoot 'supersampling_probe.cpp') /Fe:supersampling_probe.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if ($LASTEXITCODE) {throw 'Supersampling GPU probe did not compile.'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try {
        $binaryHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $output 'supersampling_probe.exe')))).Replace('-','').ToLowerInvariant()
        $sourceHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'supersampling_probe.cpp')))).Replace('-','').ToLowerInvariant()
        $buildHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($PSCommandPath))).Replace('-','').ToLowerInvariant()
        if ($sourceBefore -ne $sourceHash -or $buildBefore -ne $buildHash) {throw 'Source/build script changed during compilation; discard this build.'}
        $manifest='{"format":"s7-supersampling-build-v1","probe_sha256":"'+$binaryHash+'","source_sha256":"'+$sourceHash+'","build_sha256":"'+$buildHash+'"}'
        [IO.File]::WriteAllText((Join-Path $output 'supersampling-build.json'),$manifest,[Text.UTF8Encoding]::new($false))
        $binaryHash
    } finally {$sha.Dispose()}
} finally {Pop-Location}
