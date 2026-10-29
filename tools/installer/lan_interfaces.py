"""Read only active private IPv4 interfaces; no route/firewall changes."""
import json
import subprocess

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
    from windows_system import command
    return parse_interfaces(command('interfaces',timeout=15))
