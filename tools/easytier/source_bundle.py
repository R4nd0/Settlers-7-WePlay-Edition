"""Package reviewed corresponding source, excluding upstream native binaries."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[2]
NATIVE = {'.exe', '.dll', '.sys', '.lib', '.a', '.o', '.obj', '.so', '.dylib', '.pdb'}


def build(archive, destination):
    archive, destination = Path(archive).resolve(), Path(destination).resolve()
    lock = json.loads((REPO / 'tools/installer/easytier-runtime.json').read_text())
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != lock['source_sha256']:
            raise ValueError('Unreviewed EasyTier source archive')
    patch = REPO / 'tools/easytier/no-npcap.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != lock['patch_sha256']:
        raise ValueError('Unreviewed EasyTier patch')
    if destination.exists() or not destination.parent.is_dir():
        raise ValueError('Use a new source ZIP in an existing directory')
    with tempfile.TemporaryDirectory(prefix='easytier-source-', dir=REPO / 'work') as temporary:
        folder = Path(temporary)
        source = folder / 'source'; source.mkdir()
        excluded = []
        with zipfile.ZipFile(archive) as upstream:
            prefix = upstream.namelist()[0].split('/')[0] + '/'
            for entry in upstream.infolist():
                if not entry.filename.startswith(prefix): raise ValueError('Mixed archive roots')
                name = entry.filename[len(prefix):]
                if not name or entry.is_dir(): continue
                path = PurePosixPath(name)
                if path.is_absolute() or any(p in ('.', '..') for p in path.parts) or ':' in name or '\\' in name:
                    raise ValueError('Unsafe archive path')
                data = upstream.read(entry)
                if path.suffix.lower() in NATIVE or data.startswith(b'MZ'):
                    excluded.append(name); continue
                target = source / name; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        relative = source.relative_to(REPO).as_posix()
        subprocess.run(['git', 'apply', '--directory=' + relative, str(patch)], cwd=REPO, check=True,
                       capture_output=True)
        for name in ('no-npcap.patch', 'build-runtime.ps1', 'README.md'):
            shutil.copyfile(REPO / 'tools/easytier' / name, folder / name)
        (folder / 'SOURCE-BUNDLE.txt').write_text(
            'EasyTier 2.6.4-weplay-npcap-free corresponding source\n'
            'Upstream: https://github.com/EasyTier/EasyTier/tree/8428a89\n'
            'The source/ tree already contains no-npcap.patch. Cargo.lock is retained.\n'
            'Windows 10/11 x64, MSVC tools, Rust stable 1.99.0, protoc 26.0.\n'
            'Use build-runtime.ps1 with the pinned original source/protoc ZIPs for the complete build.\n'
            'It uses upstream import libraries privately at build time; they are not distributed here.\n'
            'After providing those upstream build inputs and PROTOC, the source/ command is:\n'
            'cargo +stable build -p easytier --bin easytier-core --bin easytier-cli '
            '--release --no-default-features --features tun,aes-gcm,smoltcp -j 1 --locked\n'
            'build-runtime.ps1 alternatively builds from the pinned original upstream ZIP.\n'
            'Prebuilt third-party native binaries are omitted; no Npcap binary is distributed.\n'
            'Wintun is distributed separately with its binary license.\n\n'
            'Omitted upstream files:\n' + '\n'.join(sorted(excluded)) + '\n', encoding='utf-8')
        with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    info = zipfile.ZipInfo(path.relative_to(folder).as_posix(), date_time=(2026, 10, 29, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    output.writestr(info, path.read_bytes())
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive'); parser.add_argument('destination')
    args = parser.parse_args()
    build(args.archive, args.destination)
