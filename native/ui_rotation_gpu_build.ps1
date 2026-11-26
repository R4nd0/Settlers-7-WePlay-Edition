param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith((Join-Path $repository 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {throw 'Use a new directory inside repository work/.'}
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if ($LASTEXITCODE) {throw 'Cannot initialize x86 compiler.'}
foreach ($line in $lines) {$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $output | Out-Null
$names=@('ui_rotation_fix.h','ui_rotation_fix.cpp','ui_rotation_program.h','ui_rotation_gpu_fixture.cpp','ui_rotation_cpu_fixture.cpp','supersampling_probe.cpp')
$hashes=@{};Push-Location $output
try {
 foreach($name in $names){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant();Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict ui_rotation_fix.cpp ui_rotation_gpu_fixture.cpp /Fe:ui_rotation_gpu_fixture.exe /link bcrypt.lib user32.lib gdi32.lib dwmapi.lib /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'UI rotation GPU fixture compile failed.'}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /fp:strict ui_rotation_fix.cpp ui_rotation_cpu_fixture.cpp /Fe:ui_rotation_cpu_fixture.exe /link bcrypt.lib /DYNAMICBASE /NXCOMPAT /Brepro
 if($LASTEXITCODE){throw 'UI rotation CPU fixture compile failed.'}
 foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw 'Source changed during build.'}}
 $manifest=@{format='s7-ui-rotation-gpu-build-v1';sources=$hashes;binary_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'ui_rotation_gpu_fixture.exe') -Algorithm SHA256).Hash.ToLowerInvariant();game_started=$false}
 [IO.File]::WriteAllText((Join-Path $output 'build.private.json'),($manifest|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
}finally{Pop-Location}
