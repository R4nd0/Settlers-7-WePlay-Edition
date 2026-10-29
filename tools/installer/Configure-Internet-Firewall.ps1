param([Parameter(Mandatory=$true)][string]$Bundle,
      [Parameter(Mandatory=$true)][string]$RuleKey,
      [string]$Address, [string]$InterfaceName, [switch]$Remove)
$ErrorActionPreference = 'Stop'
if ($RuleKey -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid rule owner' }
$root = [IO.Path]::GetFullPath($Bundle).TrimEnd('\')
$programs = @('Base\Bin\Settlers7R.exe','Runtime\python\python.exe','Runtime\dotnet\dotnet.exe')
$names = @()
foreach ($index in 0..2) {
    foreach ($protocol in @('TCP','UDP')) { $names += "S7ET-$RuleKey-$index-$protocol" }
}
$names += @("S7ET-$RuleKey-core-TCP", "S7ET-$RuleKey-core-UDP")
if ($Remove) {
    foreach ($name in $names) {
        Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
    exit 0
}
if ($Address -cnotmatch '^(10\.[0-9]+\.[0-9]+|172\.(1[6-9]|2[0-9]|3[01])\.[0-9]+|192\.168\.[0-9]+)\.[0-9]+$' -or
        $InterfaceName -cnotmatch '^WePlay-[0-9a-f]{8}$') { throw 'Invalid overlay' }
$parsed = [ipaddress]::Parse($Address)
if ($parsed.ToString() -cne $Address) { throw 'Noncanonical address' }
$rows = @(Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias $InterfaceName -ErrorAction Stop)
if ($rows.Count -ne 1 -or $rows[0].IPAddress -cne $Address -or $rows[0].PrefixLength -ne 24) { throw 'Overlay changed' }
$cursor = Get-Item -LiteralPath $root
while ($cursor) {
    if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse path' }
    $cursor = $cursor.Parent
}
$paths = @($programs + 'Runtime\easytier\easytier-core.exe')
foreach ($relative in $paths) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Program missing' }
    $cursor = Get-Item -LiteralPath $path
    while ($cursor -and $cursor.FullName -cne $root) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse path' }
        $cursor = $cursor.Parent
    }
}
$parts = $Address.Split('.')
$subnet = ($parts[0..2] -join '.') + '.0/24'
try {
    foreach ($index in 0..2) {
        foreach ($protocol in @('TCP','UDP')) {
            New-NetFirewallRule -Name "S7ET-$RuleKey-$index-$protocol" -DisplayName 'WePlay Internet room' `
                -Direction Inbound -Action Allow -Profile Any -InterfaceAlias $InterfaceName `
                -LocalAddress $Address -RemoteAddress $subnet -Program (Join-Path $root $programs[$index]) `
                -Protocol $protocol -Enabled True | Out-Null
        }
    }
    # Encrypted outer transport. Management RPC stays bound to loopback; only
    # the actual core's TCP listener is exposed. UDP hole punching uses dynamic ports.
    New-NetFirewallRule -Name "S7ET-$RuleKey-core-TCP" -DisplayName 'WePlay EasyTier transport' `
        -Direction Inbound -Action Allow -Profile Any -Program (Join-Path $root $paths[-1]) `
        -Protocol TCP -LocalPort 11010 -Enabled True | Out-Null
    New-NetFirewallRule -Name "S7ET-$RuleKey-core-UDP" -DisplayName 'WePlay EasyTier transport' `
        -Direction Inbound -Action Allow -Profile Any -Program (Join-Path $root $paths[-1]) `
        -Protocol UDP -Enabled True | Out-Null
} catch {
    foreach ($name in $names) {
        Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
    throw
}
