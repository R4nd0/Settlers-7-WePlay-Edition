"""Live LAN picker: only display names and opaque local selections reach the UI."""
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import uuid

import compatibility
import player_settings
from lobby_directory import encode, json_record

FORMAT = 's7-lobby-picker-v1'


def snapshot(interfaces, rooms, expected, display_name, revision):
    return {'format': FORMAT, 'display_name': player_settings.player_name(display_name),
        'revision': revision, 'networks': [{'name': interface.name} for interface in interfaces],
        'rooms': [{'key': room.room_id, 'name': room.name, 'members': room.members,
                   'capacity': room.capacity, 'compatible': room.compatible(expected),
                   'network': interfaces.index(room.local_interface)} for room in rooms]}


def read_choice(raw, interfaces):
    value = json_record(raw, 4096)
    if (set(value) != {'format', 'action', 'network', 'room', 'display_name'}
            or value['format'] != 's7-lobby-choice-v1' or value['action'] not in ('create', 'join')
            or type(value['network']) is not int or not 0 <= value['network'] < len(interfaces)):
        raise ValueError('Ungueltige Lobby-Auswahl.')
    player_settings.player_name(value['display_name'])
    if value['action'] == 'create':
        if value['room'] is not None:
            raise ValueError('Ungueltige neue Lobby.')
    elif (not isinstance(value['room'], str) or len(value['room']) != 32
            or any(c not in '0123456789abcdef' for c in value['room'])):
        raise ValueError('Ungueltige gewaehlte Lobby.')
    return value


def choose(root, directory, interfaces, expected):
    root = compatibility.plain(root)
    data = compatibility.plain(root / 'LocalData'); data.mkdir(exist_ok=True)
    name = player_settings.load(root)
    path = data / ('lobby-picker-' + uuid.uuid4().hex + '.json')
    stop = threading.Event()
    lock = threading.Lock()

    def publish(rooms, revision):
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=data, prefix='lobby-view-', suffix='.tmp', delete=False) as output:
                temporary = Path(output.name)
                output.write(encode(snapshot(interfaces, rooms, expected, name, revision)))
                output.flush()
            os.replace(temporary, path)
        finally:
            if temporary and temporary.exists(): temporary.unlink()

    def refresh():
        revision = 0
        while not stop.is_set():
            try:
                with lock:
                    rooms = directory.list_rooms(timeout=1)
                revision += 1; publish(rooms, revision)
            except (OSError, ValueError):
                pass
            stop.wait(1)

    publish([], 0)
    thread = threading.Thread(target=refresh, name='s7-lan-discovery', daemon=True)
    thread.start()
    try:
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-STA',
            '-WindowStyle', 'Hidden', '-File', str(Path(__file__).with_name('Choose-Lobby.ps1')),
            '-Snapshot', str(path)], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode == 3:
            return None
        if result.returncode:
            raise ValueError('Die LAN-Auswahl konnte nicht geoeffnet werden.')
        choice = read_choice(result.stdout, interfaces)
        if choice['action'] == 'join':
            with lock:
                room = directory.resolve(choice['room'])
            if room.local_interface != interfaces[choice['network']] or not room.compatible(expected):
                raise ValueError('Die ausgewaehlte Lobby ist nicht mehr verfuegbar oder passt nicht zum Patchstand.')
            choice['endpoint'] = room.endpoint
        player_settings.save(root, choice['display_name'])
        return choice
    finally:
        stop.set(); thread.join(timeout=4)
        if not thread.is_alive() and path.exists():
            compatibility.plain(path).unlink()
