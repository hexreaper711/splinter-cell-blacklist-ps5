#!/usr/bin/env bash
# Builds Splinter Cell: Blacklist for PS5 inside the port's build container:
#   ./dev.sh bash ps5/build.sh [ninja target]
# Configures ps5/ with the PS5 toolchain (out/ps5), builds the program and, when no target is
# given, links and packages the app into out/ps5/ps5-app/PPSA90827 (ps5/tools/ps5_package.sh).
# Needs ps5-plume-triangle built once (the payload SDK, RADV, app CRT and signer) and the
# recompiled code in generated/default (rexglue codegen).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
tri=${PLUME_DIR:-/work/ps5-plume-triangle}
sdk=$tri/.deps/native/ps5-payload-sdk
build=$root/out/ps5

cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/ps5-toolchain.cmake" -DPS5_PAYLOAD_SDK="$sdk"
ninja -C "$build" -k 0 "${1:-blacklist}"
[[ $# -eq 0 ]] && bash "$here/tools/ps5_package.sh" "$build" "$tri"
