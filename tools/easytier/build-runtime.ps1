param([Parameter(Mandatory=$true)][string]$SourceZip,
      [Parameter(Mandatory=$true)][string]$ProtocZip,
      [Parameter(Mandatory=$true)][string]$OutputDirectory,
      [string]$Cargo = "$env:USERPROFILE\.cargo\bin\cargo.exe",
      [string]$Python = 'python.exe')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = (Join-Path $repo 'work') + '\'
if (-not $output.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {
    throw 'Use a new directory strictly inside repository work/'
}
$sourceHash = '57eb069465ad52bc328bec597c820163afc94d85469e9299f2b8b210c71897b4'
$protocHash = 'f5a0e21c09e823fe5cc18d6057f8db7a3d5df420c09314d548b9139c4193156d'
if ((Get-FileHash -LiteralPath $SourceZip -Algorithm SHA256).Hash.ToLowerInvariant() -cne $sourceHash -or
    (Get-FileHash -LiteralPath $ProtocZip -Algorithm SHA256).Hash.ToLowerInvariant() -cne $protocHash) {
    throw 'Source or protoc ZIP differs from the reviewed inputs'
}
New-Item -ItemType Directory -Path $output | Out-Null
@'
from pathlib import Path
import sys,zipfile
for source,target,strip in [(sys.argv[1],sys.argv[3]+'/source',True),(sys.argv[2],sys.argv[3]+'/protoc',False)]:
    with zipfile.ZipFile(source) as z:
        prefix=z.namelist()[0].split('/')[0]+'/' if strip else ''
        for entry in z.infolist():
            name=entry.filename.removeprefix(prefix)
            if not name or entry.is_dir():continue
            if ':' in name or name.startswith(('/','\\')) or any(p in ('.','..') for p in name.split('/')):
                raise ValueError('Unsafe ZIP path')
            out=Path(target)/name;out.parent.mkdir(parents=True,exist_ok=True)
            with out.open('xb') as stream:stream.write(z.read(entry))
'@ | & $Python - $SourceZip $ProtocZip $output
if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed' }
$relative = $output.Substring($repo.Length + 1).Replace('\','/') + '/source'
Push-Location $repo
try {
    & git apply --check --directory=$relative (Join-Path $PSScriptRoot 'no-npcap.patch')
    if ($LASTEXITCODE -ne 0) { throw 'Source patch does not apply to reviewed release' }
    & git apply --directory=$relative (Join-Path $PSScriptRoot 'no-npcap.patch')
    if ($LASTEXITCODE -ne 0) { throw 'Source patch failed' }
    $env:PROTOC = Join-Path $output 'protoc\bin\protoc.exe'
    & $Cargo +stable build --manifest-path (Join-Path $output 'source\Cargo.toml') -p easytier `
        --bin easytier-core --bin easytier-cli --release --no-default-features --features tun,aes-gcm,smoltcp -j 1 --locked
    if ($LASTEXITCODE -ne 0) { throw 'EasyTier build failed' }
} finally { Pop-Location }
Write-Output 'Built the Windows 10/11 x64 core and CLI. No VPN or game was started.'
Write-Output 'Assemble notices and unmodified Wintun separately, then review/update the runtime pins.'
