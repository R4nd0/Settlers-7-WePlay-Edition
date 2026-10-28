"""One-way, restartable removal of the known failing D3D11 backend.

Call only while the bundle session lock is held and S7 is closed. An interrupted
transition is completed on the next launch; unknown intermediate bytes stop it.
The backups and fixed allowlist never authorize changing another game file.
"""
import hashlib
import json
from pathlib import Path
import prepare
from memory_guard import save

FILES = ('Base/Bin/d3d9.dll', 'Base/Bin/dgVoodoo.conf',
         'compatibility.json', 'd3d11-experiment.json')
STATE = 'LocalData/native-fallback-v1'


def encoded(value):
    return (json.dumps(value,indent=2)+'\n').encode('utf-8')


def replace(path, data):
    import os, tempfile
    with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    try:
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def ensure_native(root):
    plain = prepare.content.plain
    folder = plain(root / STATE)
    folder.mkdir(parents=True,exist_ok=True)
    ready = plain(folder / 'ready.json')
    paths = {name:plain(root/name) for name in FILES}
    backups = {name:plain(folder/str(i)) for i,name in enumerate(FILES)}
    if not ready.exists():
        # No mutation until a complete restartable journal exists.
        expected = prepare.content.load(paths['compatibility.json'])
        if expected['files'].get('base/bin/d3d9.dll') != prepare.DLL:
            raise ValueError('Keine bekannte D3D11-Kopie fuer den Rueckfallweg.')
        for name in FILES[:2]:
            if prepare.content.digest(paths[name]) != expected['files'].get(name.lower()):
                raise ValueError('Grafikdateien wurden extern veraendert.')
        for name in FILES:
            data = paths[name].read_bytes()
            if backups[name].exists() and backups[name].read_bytes() != data:
                raise ValueError('Unvollstaendige Sicherung stimmt nicht mehr ueberein.')
            replace(backups[name],data)
        save(ready,{'format':'s7-native-fallback-v1'})
    if ready.stat().st_size > 1024 or json.loads(ready.read_text()) != {'format':'s7-native-fallback-v1'}:
        raise ValueError('Unbekanntes Rueckfalljournal.')
    before = {}
    for name,path in backups.items():
        if path.stat().st_size > 4*1024*1024:
            raise ValueError('Ungueltige Rueckfallsicherung.')
        before[name] = path.read_bytes()
    if hashlib.sha256(before[FILES[0]]).hexdigest() != prepare.DLL:
        raise ValueError('Falsche gesicherte Grafikbibliothek.')
    manifest = prepare.content.validate(json.loads(before['compatibility.json']))
    receipt = json.loads(before['d3d11-experiment.json'])
    if (receipt.get('format') != 's7-d3d11-experiment-v1'
            or receipt.get('package_sha256') != prepare.ARCHIVE
            or receipt.get('backend') != 'd3d11_fl11_0'
            or manifest['files'].get('base/bin/settlers7r.exe') != prepare.EXE):
        raise ValueError('Unbekannte Rueckfallquelle.')
    for name in FILES[:2]:
        if hashlib.sha256(before[name]).hexdigest() != manifest['files'].pop(name.lower(),None):
            raise ValueError('Rueckfallsicherung und Inventar widersprechen sich.')
    manifest['content_id'] = prepare.content.fingerprint(manifest['files'])
    receipt.update(backend='native_d3d9',d3d11_blocked_reason='map-load-out-of-memory',
                   runtime_verified=False,lan_verified=False)
    after = {FILES[0]:None,FILES[1]:None,'compatibility.json':encoded(manifest),
             'd3d11-experiment.json':encoded(receipt)}
    # Check ALL intermediates before changing ANY file.
    for name,path in paths.items():
        actual = path.read_bytes() if path.exists() else None
        if actual not in (before[name],after[name]):
            raise ValueError('Unbekannte Aenderung waehrend des Grafik-Rueckfalls.')
    for name,path in paths.items():
        if after[name] is None:
            path.unlink(missing_ok=True)
        else:
            replace(path,after[name])
    return receipt
