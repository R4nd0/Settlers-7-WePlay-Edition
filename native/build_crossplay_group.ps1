param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$work=[IO.Path]::GetFullPath((Join-Path $repo 'work'))+[IO.Path]::DirectorySeparatorChar
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $out.StartsWith($work,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)){throw 'Use a fresh output directory under work'}
$found=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$setup=Join-Path $found 'VC/Auxiliary/Build/vcvarsall.bat'
$lines=& $env:ComSpec /d /c "call `"$setup`" x64_x86 >nul && set"
if($LASTEXITCODE){throw 'Toolchain initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $out
try{
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /O2 /Oy- ('/I'+$PSScriptRoot) (Join-Path $PSScriptRoot 'crossplay_group_roster_tests.cpp') /Fe:crossplay-group-roster.exe /link /DYNAMICBASE /NXCOMPAT
 if($LASTEXITCODE){throw 'Group roster fixture did not compile'}
 & ./crossplay-group-roster.exe
 if($LASTEXITCODE){throw 'Group roster fixture failed'}
 & cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $PSScriptRoot 'crossplay_channel.cpp') (Join-Path $PSScriptRoot 'crossplay_channel_tests.cpp') /Fe:crossplay_channel_tests.exe /link ws2_32.lib bcrypt.lib /DYNAMICBASE /NXCOMPAT
 if($LASTEXITCODE){throw 'Crossplay channel fixture did not compile'}
}finally{Pop-Location}
Write-Output 'Room roster verified. Run tests/crossplay_channel_tests.py against the built channel fixture for loopback coverage.'
