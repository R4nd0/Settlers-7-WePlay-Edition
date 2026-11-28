param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$output=[IO.Path]::GetFullPath($OutputDirectory)
if(!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Use a fresh identity build below work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ Build Tools are required.'}
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat';$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Cannot initialize the x86 compiler.'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('on12_resource_lifetime.h','on12_resource_lifetime_fixture.cpp','on12_doodad_lifetime.h','water_on12_identity.h');$files=@{}
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output;$files[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
Push-Location $output
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 on12_resource_lifetime_fixture.cpp /Fe:on12_resource_lifetime_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
    if($LASTEXITCODE){throw 'Resource lifetime fixture compile failed.'}
    $result=& .\on12_resource_lifetime_fixture.exe
    if($LASTEXITCODE){throw 'Resource lifetime fixture failed.'}
    $result | Set-Content -LiteralPath 'result.private.json' -Encoding utf8
    foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $files[$name]){throw 'Resource lifetime source changed during compilation.'}}
    $files['on12_resource_lifetime_fixture.exe']=(Get-FileHash -LiteralPath 'on12_resource_lifetime_fixture.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest=@{format='s7-on12-resource-lifetime-build-v1';build_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$files;cpu_only=$true}
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath 'on12-resource-lifetime-build.json' -Encoding utf8
    $result
}finally{Pop-Location}
