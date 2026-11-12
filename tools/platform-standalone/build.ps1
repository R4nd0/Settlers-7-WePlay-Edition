param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$destination = [IO.Path]::GetFullPath($OutputDirectory)
$work = [IO.Path]::GetFullPath((Join-Path $repository 'work')) + [IO.Path]::DirectorySeparatorChar
if (!$destination.StartsWith($work, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $destination)) {
    throw 'Use a fresh output beneath repository work/.'
}
$source = Join-Path $destination 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
# A source snapshot keeps this experiment separate from production and other
# native laboratory builds. No game file or installed adapter is modified.
Copy-Item -Path (Join-Path $repository 'native/*') -Destination $source -Recurse
foreach ($name in @('steam_local_facade.h','steam_local_api.cpp','steam_local_api.def','api_tests.cpp','facade_tests.cpp')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $source
}
$bootstrapPath = Join-Path $source 'offline_bootstrap.cpp'
$bootstrap = [IO.File]::ReadAllText($bootstrapPath)
$anchor = 'bool InitializeOfflineBootstrap(const OfflineBootstrapConfig& config) {'
if (($bootstrap.Split(@($anchor), [StringSplitOptions]::None).Length - 1) -ne 1) { throw 'Bootstrap entry changed.' }
# Include inside the same namespace as the existing guarded Stop/Error helpers.
$includeAnchor = '#include "visual_detail.inc"'
if (($bootstrap.Split(@($includeAnchor), [StringSplitOptions]::None).Length - 1) -ne 1) { throw 'Bootstrap include changed.' }
$bootstrap = $bootstrap.Replace($includeAnchor, "$includeAnchor`n#ifdef S7_STEAM_LOCAL_FACADE`n#include `"steam_local_facade.h`"`n#endif")
$installAnchor = '    if (lan_game) ReportLocalStatus("local-lan-developer-mode-disabled");'
if (($bootstrap.Split(@($installAnchor), [StringSplitOptions]::None).Length - 1) -ne 1) { throw 'Bootstrap install changed.' }
$bootstrap = $bootstrap.Replace($installAnchor, "#ifdef S7_STEAM_LOCAL_FACADE`n    if (!steam_history || !SteamLocalFacade::Install(image_base, bootstrap_thread.load())) Stop(PatchFailure);`n#endif`n$installAnchor")
[IO.File]::WriteAllText($bootstrapPath, $bootstrap)
$builderPath = Join-Path $source 'build.ps1'
$builder = [IO.File]::ReadAllText($builderPath)
$dllAnchor = "Compile-Native @('/LD', (Join-Path `$nativeSource 'local_platform.cpp')"
if (($builder.Split(@($dllAnchor), [StringSplitOptions]::None).Length - 1) -ne 1) { throw 'Native DLL build changed.' }
$builder = $builder.Replace($dllAnchor, "Compile-Native @('/LD', '/DS7_STEAM_LOCAL_FACADE', (Join-Path `$nativeSource 'local_platform.cpp')")
[IO.File]::WriteAllText($builderPath, $builder)
Get-ChildItem -LiteralPath $source -Recurse -File | ForEach-Object {
    $stream = [IO.File]::OpenRead($_.FullName)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
    [PSCustomObject]@{file=$_.FullName.Substring($source.Length+1); sha256=$digest}
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'source-checkpoint.private.json') -Encoding UTF8
& $builderPath -OutputDirectory (Join-Path $destination 'native') -LocalPairLab -MaximumPhysicalCores 2
if ($LASTEXITCODE -ne 0) { throw 'Standalone native build failed.' }
Push-Location (Join-Path $destination 'native')
try {
    & cl.exe /nologo /LD /MT /O2 /W4 /WX (Join-Path $source 'steam_local_api.cpp') /Fe:steam_api.dll /link "/DEF:$(Join-Path $source 'steam_local_api.def')" /DYNAMICBASE /NXCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Unavailable Steam boundary build failed.' }
    & cl.exe /nologo /MT /O2 /W4 /WX (Join-Path $source 'api_tests.cpp') /Fe:api_tests.exe /link steam_api.lib
    if ($LASTEXITCODE -ne 0) { throw 'API boundary checks failed to compile.' }
    & cl.exe /nologo /MT /O2 /W4 /WX (Join-Path $source 'facade_tests.cpp') /Fe:facade_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native facade checks failed to compile.' }
    & .\api_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'API boundary checks failed.' }
    foreach ($mode in 1..3) {
        & .\api_tests.exe $mode
        if ([uint32]($LASTEXITCODE -band 0xffffffffL) -ne 0xe0537011L) { throw "Unexpected API rejection result for $mode." }
    }
    & .\facade_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native facade checks failed.' }
    foreach ($mode in 1..7) {
        & .\facade_tests.exe $mode
        if ([uint32]($LASTEXITCODE -band 0xffffffffL) -ne 0xe0537012L) { throw "Unexpected facade rejection result for $mode." }
    }
    $receipt = @{native_tests_passed=$true; native_scenarios=12; files=@{}}
    foreach ($name in @('steam_api.dll','uplay_r1_loader.dll')) {
        $stream = [IO.File]::OpenRead((Join-Path $destination "native/$name"))
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $receipt.files[$name] = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
        finally { $sha.Dispose(); $stream.Dispose() }
    }
    $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'verified.private.json') -Encoding UTF8
} finally { Pop-Location }
Write-Output 'Built isolated Steam local-facade candidate. No game was launched or installed.'
