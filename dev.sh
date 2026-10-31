#!/usr/bin/env bash
# Runs a command in the port's Linux build container (built from docker/Dockerfile on first use).
#   ./dev.sh                         an interactive shell
#   ./dev.sh bash ps5/build.sh       the PS5 build (build.sh does every step)
# This folder is /work/scb. The ReXGlue SDK (thirdparty/rexglue-sdk, branch ps5-port) is mounted
# at /work/rexglue-sdk and ps5-plume-triangle (the PS5 payload SDK, RADV, the app CRT and the
# signer) at /work/ps5-plume-triangle. Dependencies and build trees live on Docker volumes
# (scbps5-*): a Linux filesystem, faster than the shared Mac folder, where tar and git archive
# also break. With SCB_STORAGE_DIR set they are folders in it instead (the GitHub check caches
# them between runs).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=${REXGLUE_SDK_DIR:-"$here/thirdparty/rexglue-sdk"}
plume=${PLUME_DIR:-"$here/thirdparty/ps5-plume-triangle"}
image=scbps5-dev

if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$image" "$here/docker"
fi

store() {  # a Docker volume, or a folder in SCB_STORAGE_DIR
    if [[ -n ${SCB_STORAGE_DIR:-} ]]; then
        mkdir -p "$SCB_STORAGE_DIR/$1"
        echo "$SCB_STORAGE_DIR/$1"
    else
        echo "scbps5-$1"
    fi
}
mounts=(-v "$here:/work/scb" -v "$(store cache):/cache"
        -v "$sdk:/work/rexglue-sdk"
        -v "$(store rexsdk-out):/work/rexglue-sdk/out"
        -v "$plume:/work/ps5-plume-triangle"
        -v "$(store plume-deps):/work/ps5-plume-triangle/.deps"
        -v "$(store plume-build):/work/ps5-plume-triangle/build"
        -v "$(store out):/work/scb/out"
        -v "$(store generated):/work/scb/generated")

# The Linux rexglue tool (codegen), built from the same SDK by build.sh.
tool=/work/rexglue-sdk/out/linux-amd64/Release
tty=()
[[ -t 0 && -t 1 ]] && tty=(-it)
exec docker run --rm ${tty[@]+"${tty[@]}"} --platform linux/amd64 "${mounts[@]}" \
    -e LD_LIBRARY_PATH=$tool -e REXGLUE=$tool/rexglue \
    -w /work/scb "$image" "${@:-bash}"
