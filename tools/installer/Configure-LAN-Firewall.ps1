param([Parameter(Mandatory=$true)][string]$Bundle, [switch]$Remove)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Bundle).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $root 'compatibility.json') -PathType Leaf)) { throw 'Keine vorbereitete S7-Kopie.' }
$cursor = Get-Item -LiteralPath $root -Force
while ($null -ne $cursor) {
    if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Verknuepfungen werden nicht freigegeben.' }
    $cursor = $cursor.Parent
}
$programs = @('Base\Bin\Settlers7R.exe', 'Runtime\dotnet\dotnet.exe', 'Runtime\python\python.exe')
foreach ($relative in $programs) {
    $path = Join-Path $root $relative
    $cursor = Get-Item -LiteralPath $path -Force
    while ($null -ne $cursor -and $cursor.FullName -cne $root) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Verknuepfungen werden nicht freigegeben.' }
        $cursor = $cursor.Parent
    }
}
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    # The user deliberately opened this helper; Windows displays its UAC prompt.
    # Encoded arguments preserve literal paths without command interpolation.
    $payload = @{bundle=$root;remove=[bool]$Remove} | ConvertTo-Json -Compress
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
    $script = $PSCommandPath.Replace("'", "''")
    $command = '$x=([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $encoded + '''))|ConvertFrom-Json); & ''' + $script + ''' -Bundle $x.bundle -Remove:$x.remove'
    $arguments = '-NoLogo -NoProfile -EncodedCommand ' + [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $child = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    if ($child.ExitCode -ne 0) { throw 'Windows hat die LAN-Freigabe nicht eingerichtet.' }
    Write-Host 'LAN-Freigabe abgeschlossen.'
    exit 0
}
$hasher = [Security.Cryptography.SHA256]::Create()
try { $key = ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())))).Replace('-', '').Substring(0,16) }
finally { $hasher.Dispose() }
foreach ($index in 0..($programs.Count - 1)) {
    foreach ($protocol in @('UDP', 'TCP')) {
        $rule = 'S7LAN-' + $key + '-' + $index + '-' + $protocol
        $old = Get-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue
        if ($old) { $old | Remove-NetFirewallRule }
        if (-not $Remove) {
            New-NetFirewallRule -Name $rule -DisplayName ('S7 LAN - ' + [IO.Path]::GetFileName($programs[$index])) `
                -Direction Inbound -Action Allow -Profile Private -RemoteAddress LocalSubnet `
                -Program (Join-Path $root $programs[$index]) -Protocol $protocol -Enabled True | Out-Null
        }
    }
}
Write-Host $(if ($Remove) { 'Private S7-LAN-Freigaben entfernt.' } else { 'Private S7-LAN-Freigaben fuer diese Kopie eingerichtet.' })
$marker = Join-Path $root 'LocalData\desktop-firewall.json'
if ($Remove) {
    if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker }
} else {
    $folder = Join-Path $root 'LocalData'
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    @{format='s7-private-firewall-v1';key=$key} | ConvertTo-Json -Compress | Set-Content -LiteralPath $marker -Encoding UTF8
}
