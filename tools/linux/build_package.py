"""Stage a Linux x64 package from verified Windows patch assets and native service.

No game data, Wine prefix, profile, signing key or local identity is packaged.
The native service must be published on Linux (glibc 2.35 or newer).
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/installer'))
import compatibility
import install


def build(patch_package, server, output, *, compression, easytier=None, version='2.0.0-rc3.18'):
    patch_package, server, output = map(compatibility.plain, (patch_package, server, output))
    source = install.package_manifest(patch_package)
    compression=compatibility.plain(compression)
    lock=json.loads((ROOT/'tools/lan/compression-runtime.json').read_text(encoding='utf-8'))
    if (lock.get('format')!='weplay-canonical-compressor-v1' or lock.get('version')!='2.2.4'
            or {path.name for path in compression.iterdir()}!=set(lock['files'])):
        raise ValueError('Incomplete canonical compressor runtime')
    for name,digest in lock['files'].items():
        if compatibility.digest(compatibility.plain(compression/name))!=digest:
            raise ValueError('Canonical compressor differs from the qualified build')
    if easytier is not None:
        from easytier_runtime import verify_directory
        easytier = verify_directory(easytier)
        if not (easytier / 'easytier-core').is_file():
            raise ValueError('Supply the pinned native Linux transport')
    from build_package import check_output_location
    check_output_location(ROOT, output, (patch_package, server))
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Output must be a new directory in an existing parent')
    apphost = server / 'S7Patcher'
    header = apphost.read_bytes()[:20]
    if header[:5] != b'\x7fELF\x02' or header[18:20] != b'\x3e\x00' or not (server / 'libcoreclr.so').is_file():
        raise ValueError('Supply a self-contained Linux x64 server publish directory')
    for name in ('LICENSE-runtime.txt', 'ThirdPartyNotices-runtime.txt', 'WePlay.LinuxHelper'):
        if not (server / name).is_file(): raise ValueError('Native helper or runtime notices missing')
    # Validate every input before creating the user-facing stage.
    compatibility.inventory(server, ('.',))
    output.mkdir()
    shutil.copytree(patch_package / 'Payload', output / 'Payload', ignore=shutil.ignore_patterns('Server', 'Helpers', 'S7GraphicsProbe.exe'))
    shutil.copytree(server, output / 'Payload/Server')
    shutil.copytree(patch_package / 'Tools', output / 'Tools')
    # Refresh only public source files already in the verified package. Patch
    # recipes, adapter, graphics DLL and license bytes retain their fixed pins.
    for path in (output / 'Tools').rglob('*.py'):
        original = ROOT / 'tools' / path.relative_to(output / 'Tools')
        if original.is_file(): shutil.copyfile(original, path)
    for name in ('portable_lock.py', 'host_runtime.py'):
        shutil.copyfile(ROOT / 'tools/installer' / name, output / 'Tools/installer' / name)
    for name in ('canonical_deflate.py','compression-runtime.json'):
        shutil.copyfile(ROOT/'tools/lan'/name,output/'Tools/lan'/name)
    linux = output / 'Tools/linux'; linux.mkdir(exist_ok=True)
    for name in ('launcher.py', 'linux_runtime.py', 'linux_network.py', 'linux_elevation.py', 'linux_profiles.py'):
        shutil.copyfile(ROOT / 'tools/linux' / name, linux / name)
    runtime = output / 'Runtime'; runtime.mkdir()
    shutil.copytree(compression,runtime/'compression')
    graphics = output / 'Payload/Graphics'
    if graphics.is_dir():
        shutil.copytree(graphics, runtime / 'Graphics')
    if easytier is not None:
        shutil.copytree(easytier, runtime / 'easytier')
        shutil.copyfile(ROOT / 'tools/installer/easytier-linux-runtime.json',
                        output / 'Tools/installer/easytier-linux-runtime.json')
    (runtime / 'README.txt').write_text('Native .NET runtime and licenses: Payload/Server; '
                                     'Python 3.10+ and Tk are supplied by the Linux distribution.\n', encoding='utf-8')
    manifest = dict(source)
    manifest.update(linux_native=True, windows_helpers=False, desktop_launcher=False, standalone_export=False,
                    update_patcher=False, discord_directory=False, internet_easytier=easytier is not None)
    manifest['files'] = compatibility.inventory(output, ('Payload', 'Runtime', 'Tools'))
    install.write_json(output / 'package.json', manifest)
    install.package_manifest(output)
    verification = ROOT / 'docs/verification/rc318-20261106.json'
    install.write_json(output / 'linux-release.json', dict(format='weplay-linux-release-v1',version=version,
        network_revision=8,content_contract=compatibility.NETWORK_FORMAT,game_bits=32,host='linux-x64',
        game_runtime='Wine/Proton',gameplay_qualified=False,
        qualification_scope='Ubuntu 24.04 WSL2, WineHQ 11 win32, Ubisoft History Edition',
        verification='verification.json' if verification.is_file() else None))
    if verification.is_file(): shutil.copyfile(verification, output / 'verification.json')
    shutil.copyfile(ROOT / 'LICENSE', output / 'LICENSE.txt')
    credits = (ROOT / 'CREDITS.md').read_text(encoding='utf-8')
    credits = re.sub(r'\]\((?!https?://|#)([^)]+)\)',
                     lambda match: '](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/v' + version + '/' + match[1] + ')',
                     credits)
    (output / 'CREDITS.md').write_text(credits, encoding='utf-8', newline='\n')
    starter = output / 'WePlay.sh'
    starter.write_text('#!/bin/sh\nset -eu\nROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                       'exec python3 -B "$ROOT/Tools/linux/launcher.py" --package "$ROOT" "$@"\n',
                       encoding='utf-8', newline='\n')
    starter.chmod(0o755)
    if (ROOT / 'docs/LINUX.md').is_file():
        guide = (ROOT / 'docs/LINUX.md').read_text(encoding='utf-8')
        (output / 'README.md').write_text(guide.replace('verification/linux-20261106.json', 'verification.json'),
                                         encoding='utf-8', newline='\n')
    return output


def archive(stage, destination):
    stage, destination = map(compatibility.plain, (stage, destination))
    if destination.exists() or destination.is_relative_to(stage):
        raise ValueError('Archive output must be new and outside the stage')
    install.package_manifest(stage)
    def permissions(info):
        info.uid = info.gid = 0; info.uname = info.gname = ''
        executable = ('/WePlay.sh', '/Server/S7Patcher', '/Server/WePlay.LinuxHelper', '/Server/createdump',
                      '/easytier/easytier-core', '/easytier/easytier-cli')
        info.mode = 0o755 if info.isdir() or info.name.endswith(executable) else 0o644
        return info
    with tarfile.open(destination, 'x:gz') as output:
        output.add(stage, arcname='WePlay-Linux', recursive=True, filter=permissions)
    return compatibility.digest(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch-package', type=Path, required=True)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--compression',type=Path,required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--easytier',type=Path)
    parser.add_argument('--version',default='2.0.0-rc3.18')
    args = parser.parse_args()
    result = build(args.patch_package,args.server,args.output,compression=args.compression,easytier=args.easytier,version=args.version)
    print(json.dumps({'stage': str(result), 'archive_sha256': archive(result, args.archive) if args.archive else None}))
