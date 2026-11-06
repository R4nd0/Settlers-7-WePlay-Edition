"""Read only active private IPv4 interfaces; no route/firewall changes."""
import json
import subprocess
import sys

from lobby_directory import LanInterface

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
    if sys.platform == 'linux':
        return linux_interfaces()
    from windows_system import command
    return parse_interfaces(command('interfaces',timeout=15))


def linux_interfaces():
    """iproute2 JSON only; no shell or privileged system changes."""
    def query(arguments):
        result = subprocess.run(['/usr/sbin/ip' if __import__('pathlib').Path('/usr/sbin/ip').exists() else '/usr/bin/ip',
                                 '-j', *arguments], capture_output=True, timeout=10)
        if result.returncode or len(result.stdout) > 131072:
            raise ValueError('Netzwerkschnittstellen konnten nicht gelesen werden.')
        value = json.loads(result.stdout)
        if not isinstance(value, list) or len(value) > 256:
            raise ValueError('Unbekannte Netzwerkausgabe.')
        return value
    routes = query(['-4', 'route', 'show', 'default'])
    defaults = {row.get('dev') for row in routes if isinstance(row, dict)}
    rows = []
    for interface in query(['-4', 'address', 'show', 'up']):
        if not isinstance(interface, dict) or 'UP' not in interface.get('flags', []):
            continue
        for address in interface.get('addr_info', []):
            if isinstance(address, dict) and address.get('family') == 'inet':
                rows.append(dict(address=address.get('local'), prefix_length=address.get('prefixlen'),
                                 name=interface.get('ifname'), default_route=interface.get('ifname') in defaults))
    return parse_interfaces(json.dumps(rows).encode('utf-8'))
