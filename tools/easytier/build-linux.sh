#!/bin/sh
# Rebuild the same reviewed transport/strict ACLs for Linux; no tunnel started.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo 'Usage: build-linux.sh SOURCE_ZIP NEW_DIRECTORY_UNDER_WORK [TARGET]' >&2
    exit 2
fi
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE=$(realpath -- "$1")
OUTPUT=$(realpath -m -- "$2")
TARGET=${3:-x86_64-unknown-linux-musl}
case "$TARGET" in x86_64-unknown-linux-musl|x86_64-unknown-linux-gnu) ;; *) echo 'Unsupported Linux target' >&2; exit 2;; esac
if [ "$TARGET" = x86_64-unknown-linux-musl ]; then
    command -v musl-gcc >/dev/null
    export CC_x86_64_unknown_linux_musl=musl-gcc
    export CARGO_TARGET_X86_64_UNKNOWN_LINUX_MUSL_LINKER=musl-gcc
fi
case "$OUTPUT" in "$REPO"/work/*) ;; *) echo 'Output must be under repository work/' >&2; exit 2;; esac
test ! -e "$OUTPUT"
printf '%s  %s\n' '57eb069465ad52bc328bec597c820163afc94d85469e9299f2b8b210c71897b4' "$SOURCE" | sha256sum -c -
mkdir -- "$OUTPUT"
python3 - "$SOURCE" "$OUTPUT/source" <<'PY'
from pathlib import Path
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    prefix = archive.namelist()[0].split('/')[0] + '/'
    for entry in archive.infolist():
        name = entry.filename.removeprefix(prefix)
        if not name or entry.is_dir(): continue
        if ':' in name or name.startswith(('/', '\\')) or any(p in ('.', '..') for p in name.split('/')):
            raise ValueError('Unsafe source archive member')
        target = Path(sys.argv[2]) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream: stream.write(archive.read(entry))
PY
cd -- "$OUTPUT/source"
git apply --check "$REPO/tools/easytier/no-npcap.patch"
git apply "$REPO/tools/easytier/no-npcap.patch"
git apply --check "$REPO/tools/easytier/strict-acl.patch"
git apply "$REPO/tools/easytier/strict-acl.patch"
cargo +stable build --target "$TARGET" --locked -j 1 --release -p easytier --bin easytier-core --bin easytier-cli \
    --no-default-features --features tun,aes-gcm,smoltcp
cargo +stable test --target "$TARGET" --locked -j 1 --release -p easytier --lib common::acl_processor:: \
    --no-default-features --features tun,aes-gcm,smoltcp
cargo +stable test --target "$TARGET" --locked -j 1 --release -p easytier --lib peers::acl_filter:: \
    --no-default-features --features tun,aes-gcm,smoltcp
echo 'Built Linux core/CLI with the reviewed strict ACL patch. No tunnel started.'
