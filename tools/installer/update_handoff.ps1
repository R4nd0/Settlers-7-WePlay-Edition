param([Parameter(Mandatory=$true)][string]$Job,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$updateMutex=$null;$ownedMutex=$false;$stage='validate';$app=$null;$record=$null;$parentProcess=$null;$backendProcess=$null
function PlainPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $cursor=$full
    while($cursor) {
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'update_invalid' }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
function Hash([string]$Path) {
    $stream=[IO.File]::OpenRead((PlainPath $Path));$sha=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose();$sha.Dispose() }
}
function ReadJson([string]$Path,[int64]$Limit) {
    $Path=PlainPath $Path
    if((Get-Item -LiteralPath $Path).Length -gt $Limit) { throw 'update_invalid' }
    return ([IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8)|ConvertFrom-Json)
}
function Decode64([string]$Value) {
    if($Value -cnotmatch '^[A-Za-z0-9_-]+$') { throw 'update_signature_invalid' }
    $padded=$Value.Replace('-','+').Replace('_','/')
    while($padded.Length % 4) { $padded+='=' }
    $bytes=[Convert]::FromBase64String($padded)
    if([Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_') -cne $Value) { throw 'update_signature_invalid' }
    return ,$bytes
}
function WriteStatus([string]$Phase,[string]$Code) {
    if(-not $app -or $ValidateOnly) { return }
    $status=PlainPath (Join-Path $app 'State\Updates\status.json')
    $temporary=$status+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $value=@{format='weplay-update-status-v1';phase=$Phase;code=$Code;version=$record.version;sequence=$record.sequence}
    [IO.File]::WriteAllText($temporary,($value|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $status) { [IO.File]::Replace($temporary,$status,[System.Management.Automation.Language.NullString]::Value) }
    else { [IO.File]::Move($temporary,$status) }
}
function OpenOwned($Identity,[string]$ExpectedPath) {
    try { $process=[Diagnostics.Process]::GetProcessById([int]$Identity.pid) }
    catch [ArgumentException] { throw 'update_parent_invalid' }
    try {
        if($process.MainModule.FileName -ine $ExpectedPath -or $process.StartTime.ToUniversalTime().Ticks -ne [int64]$Identity.started_ticks) { throw 'update_parent_invalid' }
        return $process
    } catch { $process.Dispose();throw }
}
try {
    $Job=PlainPath $Job;$jobDir=Split-Path -Parent $Job
    $updates=Split-Path -Parent $jobDir;$state=Split-Path -Parent $updates;$app=Split-Path -Parent $state
    if((Split-Path -Leaf $Job) -cne 'handoff.json' -or (Split-Path -Leaf $jobDir) -cnotmatch '^[0-9a-f]{32}$' -or
        (Split-Path -Leaf $updates) -ine 'Updates' -or (Split-Path -Leaf $state) -ine 'State') { throw 'update_invalid' }
    $record=ReadJson $Job 4096
    if($record.format -cne 'weplay-update-handoff-v1' -or (PlainPath $record.app) -ine $app) { throw 'update_invalid' }
    foreach($name in @('update_handoff.ps1','update-key.json')) {
        if((Hash (Join-Path $jobDir $name)) -cne (Hash (Join-Path $app ('Package\Tools\installer\'+$name)))) { throw 'update_invalid' }
    }
    if((PlainPath $PSCommandPath) -ine (Join-Path $jobDir 'update_handoff.ps1')) { throw 'update_invalid' }
    $pin=ReadJson (Join-Path $jobDir 'update-key.json') 4096
    $envelope=ReadJson (Join-Path $jobDir 'weplay-update.json') 49152
    if($pin.format -cne 'weplay-update-key-v1' -or $pin.e -ne 65537 -or $pin.n -cnotmatch '^[0-9a-f]{768}$' -or
        $envelope.format -cne 'weplay-signed-update-v1' -or $envelope.key_id -cne $pin.key_id) { throw 'update_signature_invalid' }
    $modulus=New-Object byte[] 384
    for($i=0;$i -lt 384;$i++) { $modulus[$i]=[Convert]::ToByte($pin.n.Substring($i*2,2),16) }
    $payload=Decode64 $envelope.payload;$signature=Decode64 $envelope.signature
    if($payload.Length -gt 32768 -or $signature.Length -ne 384) { throw 'update_signature_invalid' }
    $rsa=[Security.Cryptography.RSACng]::new()
    try {
        $parameters=[Security.Cryptography.RSAParameters]::new();$parameters.Modulus=$modulus;$parameters.Exponent=[byte[]](1,0,1)
        $rsa.ImportParameters($parameters)
        if(-not $rsa.VerifyData($payload,$signature,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)) { throw 'update_signature_invalid' }
    } finally { $rsa.Dispose() }
    $manifest=[Text.Encoding]::UTF8.GetString($payload)|ConvertFrom-Json
    $expectedUrl='https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/'+$manifest.tag+'/Setup.exe'
    if($manifest.format -cne 'weplay-update-v1' -or $manifest.repository -cne 'R4nd0/Settlers-7-WePlay-Edition' -or
        $manifest.channel -cne 'preview' -or $manifest.setup.name -cne 'Setup.exe' -or $manifest.setup.url -cne $expectedUrl -or
        $manifest.setup.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $manifest.setup.size -lt 1 -or $manifest.setup.size -gt 805306368 -or
        $manifest.sequence -ne $record.sequence -or $manifest.version -cne $record.version) { throw 'update_invalid' }
    $local=ReadJson (Join-Path $app 'desktop-release.json') 65536
    $highest=ReadJson (Join-Path $updates 'highest.json') 4096
    if($manifest.sequence -le $local.update_sequence -or $manifest.sequence -ne $highest.sequence) { throw 'update_downgrade' }
    $installer=PlainPath (Join-Path $jobDir 'Setup.exe')
    if((Get-Item -LiteralPath $installer).Length -ne $manifest.setup.size -or (Hash $installer) -cne $manifest.setup.sha256) { throw 'update_hash_invalid' }
    if($ValidateOnly) { Write-Output 'PASS: signed update and immutable installer verified; validation mode never installs.'; exit 0 }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $appKey=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($app.ToLowerInvariant()))).Replace('-','').Substring(0,24) }
    finally { $sha.Dispose() }
    $created=$false;$updateMutex=[Threading.Mutex]::new($true,('Local\S7Launcher-Update-'+$appKey),[ref]$created)
    if(-not $created) { throw 'update_busy' };$ownedMutex=$true
    $stage='waiting';WriteStatus 'waiting' ''
    $parentProcess=OpenOwned $record.parent (Join-Path $app 'S7Launcher.exe')
    $backendProcess=OpenOwned $record.backend (Join-Path $app 'Package\Runtime\python\python.exe')
    $ready=@{format='weplay-update-ready-v1';helper_pid=$PID;version=$record.version;sequence=$record.sequence}
    $readyTemporary=Join-Path $jobDir 'ready.tmp'
    [IO.File]::WriteAllText($readyTemporary,($ready|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($readyTemporary,(Join-Path $jobDir 'ready.json'))
    if(-not $parentProcess.WaitForExit(180000) -or -not $backendProcess.WaitForExit(180000)) { throw 'update_busy' }
    # Do not overwrite any running game or local coordinator's dependencies.
    if(Get-Process -Name Settlers7R,Settlers7,S7Patcher -ErrorAction SilentlyContinue) { throw 'update_busy' }
    Add-Type -AssemblyName System.Management
    $searcher=[Management.ManagementObjectSearcher]::new("SELECT CommandLine FROM Win32_Process WHERE Name='dotnet.exe'")
    try { foreach($service in $searcher.Get()) { if($service.CommandLine -match 'S7Patcher\.dll') { throw 'update_busy' } } }
    finally { $searcher.Dispose() }
    $stage='installing';WriteStatus 'installing' ''
    # Verify again at the final process boundary, after waiting for the old app.
    if((Get-Item -LiteralPath $installer).Length -ne $manifest.setup.size -or (Hash $installer) -cne $manifest.setup.sha256) { throw 'update_hash_invalid' }
    $arguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="'+$app+'"'))
    $setup=Start-Process -FilePath $installer -ArgumentList $arguments -WorkingDirectory $jobDir -WindowStyle Hidden -Wait -PassThru
    if($setup.ExitCode -ne 0) { throw 'update_install_failed' }
    WriteStatus 'installed' ''
    $updateMutex.ReleaseMutex();$ownedMutex=$false;$updateMutex.Dispose();$updateMutex=$null
    Start-Process -FilePath (Join-Path $app 'S7Launcher.exe') -WorkingDirectory $app -WindowStyle Normal | Out-Null
} catch {
    $code=[string]$_.Exception.Message
    if($code -cnotmatch '^update_[a-z_]+$') { $code='update_handoff_failed' }
    try { WriteStatus 'failed' $code } catch { }
    if($ValidateOnly) { Write-Output ('FAIL: '+$code) }
    exit 2
} finally {
    if($parentProcess) { $parentProcess.Dispose() };if($backendProcess) { $backendProcess.Dispose() }
    if($updateMutex) { if($ownedMutex) { $updateMutex.ReleaseMutex() };$updateMutex.Dispose() }
}
