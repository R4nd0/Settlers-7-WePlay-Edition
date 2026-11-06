#!/bin/sh
set -eu
test "$#" -eq 2 || { echo 'Usage: build_compressor.sh ZLIB_NG_2_2_4_TAR_GZ NEW_WORK_DIRECTORY' >&2; exit 2; }
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE=$(realpath -- "$1")
OUTPUT=$(realpath -m -- "$2")
case "$OUTPUT" in "$REPO"/work/*) ;; *) echo 'Output must be under repository work/' >&2; exit 2;; esac
test ! -e "$OUTPUT"
printf '%s  %s\n' a73343c3093e5cdc50d9377997c3815b878fd110bf6511c2c7759f2afb90f5a3 "$SOURCE" | sha256sum -c -
mkdir -p -- "$OUTPUT/source" "$OUTPUT/runtime"
tar -xzf "$SOURCE" --strip-components=1 -C "$OUTPUT/source"
cmake -S "$OUTPUT/source" -B "$OUTPUT/build" -DCMAKE_BUILD_TYPE=Release \
    -DZLIB_COMPAT=OFF -DBUILD_SHARED_LIBS=OFF -DZLIB_ENABLE_TESTS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DWITH_NATIVE_INSTRUCTIONS=OFF
cmake --build "$OUTPUT/build" --parallel 2
cc -O2 -fPIC -shared -Wl,-z,relro,-z,now \
    -I "$OUTPUT/build" -I "$OUTPUT/source" "$REPO/tools/linux/gzip_bridge.c" \
    "$OUTPUT/build/libz-ng.a" -o "$OUTPUT/runtime/libweplay-zlib.so"
cp -- "$OUTPUT/source/LICENSE.md" "$OUTPUT/runtime/LICENSE.txt"
printf '%s\n' 'zlib-ng 2.2.4, https://github.com/zlib-ng/zlib-ng/tree/2.2.4' \
    'Source SHA256 a73343c3093e5cdc50d9377997c3815b878fd110bf6511c2c7759f2afb90f5a3' \
    'Unmodified upstream, static native API; WePlay gzip_bridge.c supplies the bounded wrapper.' > "$OUTPUT/runtime/SOURCE.txt"
echo 'Built native canonical zlib-ng 2.2.4 compressor; no game data used.'
