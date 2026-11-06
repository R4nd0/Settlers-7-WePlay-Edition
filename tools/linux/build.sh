#!/bin/sh
# Build native host tools only. The guarded x86 PE adapter is reused unchanged.
set -eu
test "$#" -eq 1 || { echo 'Usage: build.sh NEW_DIRECTORY_UNDER_WORK' >&2; exit 2; }
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUTPUT=$(realpath -m -- "$1")
case "$OUTPUT" in "$REPO"/work/*) ;; *) echo 'Output must be under repository work/' >&2; exit 2;; esac
test ! -e "$OUTPUT"
DOTNET=$(command -v dotnet)
DOTNET_ROOT_DIR=$(dirname -- "$(readlink -f -- "$DOTNET")")
test -f "$DOTNET_ROOT_DIR/LICENSE.txt"
test -f "$DOTNET_ROOT_DIR/ThirdPartyNotices.txt"
mkdir -- "$OUTPUT"
dotnet publish "$REPO/S7Patcher.csproj" -c Release -r linux-x64 --self-contained true \
    -p:PublishAot=false -p:PublishTrimmed=false -p:PublishSingleFile=false \
    -p:BaseIntermediateOutputPath="$OUTPUT/obj-main/" -o "$OUTPUT/server"
dotnet publish "$REPO/tools/linux/JoinCrypto/WePlay.LinuxHelper.csproj" -c Release -r linux-x64 \
    --self-contained true -p:DebugType=None -p:BaseIntermediateOutputPath="$OUTPUT/obj-crypto/" -o "$OUTPUT/server"
cp -- "$DOTNET_ROOT_DIR/LICENSE.txt" "$OUTPUT/server/LICENSE-runtime.txt"
cp -- "$DOTNET_ROOT_DIR/ThirdPartyNotices.txt" "$OUTPUT/server/ThirdPartyNotices-runtime.txt"
echo 'Native Linux service and cryptography helper built, with runtime notices.'
