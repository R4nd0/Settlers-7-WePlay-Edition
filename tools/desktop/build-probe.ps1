param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$target=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $target -Force | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'C++ build tools missing'}
$vcvars=Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$compilerEnvironment=& $env:ComSpec /d /c "call `"$vcvars`" x64_x86 >nul && set"
if($LASTEXITCODE -ne 0){throw 'Compiler environment failed'}
foreach($line in $compilerEnvironment){$p=$line.IndexOf('=');if($p -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$p),$line.Substring($p+1),'Process')}}
Push-Location $target
try {
    & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /D_CRT_SECURE_NO_WARNINGS (Join-Path $PSScriptRoot 'graphics_probe.cpp') /Fe:S7GraphicsProbe.exe /link user32.lib /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    if($LASTEXITCODE -ne 0){throw 'Graphics probe build failed'}
} finally {Pop-Location}
