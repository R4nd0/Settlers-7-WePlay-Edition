param(
    [ValidateSet('CreateKey','Sign')][string]$Action,
    [Parameter(Mandatory=$true)][string]$KeyPath,
    [string]$PublicPath,
    [string]$PayloadPath
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Security
$entropy=[Text.Encoding]::UTF8.GetBytes('Settlers7WePlay/UpdateSigning/v1')
function PlainPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $cursor=$full
    while($cursor) {
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked paths are refused.' }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
$KeyPath=PlainPath $KeyPath
$rsa=$null
try {
    if($Action -eq 'CreateKey') {
        $PublicPath=PlainPath $PublicPath
        if((Test-Path -LiteralPath $KeyPath) -or (Test-Path -LiteralPath $PublicPath)) { throw 'Signing key or public pin already exists; refusing replacement.' }
        $rsa=[Security.Cryptography.RSACng]::new(3072)
        $raw=$rsa.Key.Export([Security.Cryptography.CngKeyBlobFormat]::Pkcs8PrivateBlob)
        try { $protected=[Security.Cryptography.ProtectedData]::Protect($raw,$entropy,[Security.Cryptography.DataProtectionScope]::CurrentUser) }
        finally { [Array]::Clear($raw,0,$raw.Length) }
        $stream=[IO.File]::Open($KeyPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $stream.Write($protected,0,$protected.Length) } finally { $stream.Dispose() }
        $parameters=$rsa.ExportParameters($false)
        $sha=[Security.Cryptography.SHA256]::Create()
        try { $identifier=[BitConverter]::ToString($sha.ComputeHash($parameters.Modulus)).Replace('-','').ToLowerInvariant() }
        finally { $sha.Dispose() }
        $value=@{format='weplay-update-key-v1';key_id=$identifier;n=[BitConverter]::ToString($parameters.Modulus).Replace('-','').ToLowerInvariant();e=65537}
        [IO.File]::WriteAllText($PublicPath,($value|ConvertTo-Json)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
        Write-Output 'Created CurrentUser DPAPI-protected RSA signing key and public pin. No private key was printed.'
    } else {
        $PayloadPath=PlainPath $PayloadPath
        if((Get-Item -LiteralPath $KeyPath).Length -gt 16384 -or (Get-Item -LiteralPath $PayloadPath).Length -gt 32768) { throw 'Signing input is too large.' }
        $protected=[IO.File]::ReadAllBytes($KeyPath)
        $raw=[Security.Cryptography.ProtectedData]::Unprotect($protected,$entropy,[Security.Cryptography.DataProtectionScope]::CurrentUser)
        try { $key=[Security.Cryptography.CngKey]::Import($raw,[Security.Cryptography.CngKeyBlobFormat]::Pkcs8PrivateBlob) }
        finally { [Array]::Clear($raw,0,$raw.Length) }
        try {
            $rsa=[Security.Cryptography.RSACng]::new($key)
            $signature=$rsa.SignData([IO.File]::ReadAllBytes($PayloadPath),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
            Write-Output ([Convert]::ToBase64String($signature).TrimEnd('=').Replace('+','-').Replace('/','_'))
        } finally { $key.Dispose() }
    }
} finally { if($rsa) { $rsa.Dispose() } }
