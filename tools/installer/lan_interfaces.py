"""Read only active private IPv4 interfaces; no route/firewall changes."""
import json
import subprocess

from lobby_directory import LanInterface

SCRIPT = r'''
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$adapters=@{}
Get-NetAdapter -ErrorAction Stop | Where-Object { $_.Status -eq 'Up' } | ForEach-Object { $adapters[[int]$_.ifIndex]=$_.Name }
$gateways=@{}
Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | ForEach-Object { $gateways[[int]$_.ifIndex]=$true }
$result=@(Get-NetIPAddress -AddressFamily IPv4 -AddressState Preferred -ErrorAction Stop | Where-Object { $adapters.ContainsKey([int]$_.InterfaceIndex) } | Select-Object -First 64 | ForEach-Object {
 [pscustomobject]@{address=$_.IPAddress;prefix_length=[int]$_.PrefixLength;name=$adapters[[int]$_.InterfaceIndex];default_route=$gateways.ContainsKey([int]$_.InterfaceIndex)}
})
ConvertTo-Json -InputObject $result -Compress
'''


def parse_interfaces(data):
    if not isinstance(data, bytes) or len(data) > 32768:
        raise ValueError('Netzwerkschnittstellen konnten nicht gelesen werden.')
    value = json.loads(data.decode('utf-8-sig'))
    if not isinstance(value, list) or len(value) > 64:
        raise ValueError('Unbekannte Netzwerkausgabe.')
    found = {}
    for item in value:
        if not isinstance(item, dict) or set(item) != {'address', 'prefix_length', 'name', 'default_route'}:
            continue
        try:
            interface = LanInterface(**item)
            found[(interface.address, interface.prefix_length)] = interface
        except (ValueError, TypeError):
            continue
    if not found:
        raise ValueError('Kein aktives privates LAN gefunden. Bitte Netzwerkverbindung pruefen.')
    return sorted(found.values(), key=lambda x: (not x.default_route, x.name.casefold(), x.address))[:16]


def enumerate_interfaces():
    import base64
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-EncodedCommand',
        base64.b64encode(SCRIPT.encode('utf-16-le')).decode('ascii')], capture_output=True,
        timeout=15, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise ValueError('Aktive LAN-Netzwerke konnten nicht ermittelt werden.')
    return parse_interfaces(result.stdout)
