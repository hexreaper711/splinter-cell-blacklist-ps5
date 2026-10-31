#!/usr/bin/env bash
# Builds Splinter Cell: Blacklist for PS5 inside the port's build container:
#   ./dev.sh bash ps5/build.sh [ninja target]
#   ./dev.sh bash ps5/build.sh --check
# Configures ps5/ with the PS5 toolchain (out/ps5), builds the program and, when no target is
# given, links and packages the app into out/ps5/ps5-app/PPSA90827 (ps5/tools/ps5_package.sh).
# Needs ps5-plume-triangle built once (the payload SDK, RADV, app CRT and signer) and the
# recompiled code in generated/default (rexglue codegen). --check builds only what doesn't need
# the recompiled code (the SDK, the Xenos plugin, most of the launcher) in out/ps5-check: the
# GitHub check, which has no default.xex.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
tri=${PLUME_DIR:-/work/ps5-plume-triangle}
sdk=$tri/.deps/native/ps5-payload-sdk
# The check's compiled files cached by content (CCACHE_DIR, on the cache volume), when ccache
# is there: a fresh checkout's files all look new.
launcher=()
command -v ccache >/dev/null && launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)

if [[ ${1:-} == --check ]]; then
    build=$root/out/ps5-check
    cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$here/cmake/ps5-toolchain.cmake" -DPS5_PAYLOAD_SDK="$sdk" \
        -DSCB_WITHOUT_GAME=ON "${launcher[@]}"
    ninja -C "$build" -k 0 blacklist_check
    exit
fi

build=$root/out/ps5
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/ps5-toolchain.cmake" -DPS5_PAYLOAD_SDK="$sdk"
ninja -C "$build" -k 0 "${1:-blacklist}"
[[ $# -eq 0 ]] && bash "$here/tools/ps5_package.sh" "$build" "$tri"
