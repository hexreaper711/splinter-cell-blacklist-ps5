#!/usr/bin/env bash
# Builds Splinter Cell: Blacklist for PS5 from scratch into
# dist/splinter-cell-blacklist-ps5-<version>.zip, a homebrew app folder without any game files
# (you add your own discs on the PS5, see README.md).
#   ./build.sh                  every step that isn't done yet
#   ./build.sh --clean          the game build and the recompiled code again (not the dependencies)
#   ./build.sh --with-discs     also copy your discs (disc-files/disc1, disc2) into the app folder
#                               dist/PPSA90827, for your own PS5 (the zip stays without them)
#   ./build.sh --version 1.2.0  the version to stamp (default: git describe, e.g. v0.1.0 for a
#                               commit tagged v0.1.0, v0.1.0-3-gabc1234 three commits later)
# Needs Docker, the submodules (git submodule update --init --recursive) and your own disc 1's
# default.xex in disc-files/disc1/. The first run builds the dependencies (PS5 payload SDK, RADV,
# the rexglue tool) and takes a few hours on a Mac; later runs only rebuild what changed.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$here"
dev=("$here/dev.sh")
title_id=PPSA90827

step() { printf '\n==> %s\n' "$*"; }

clean=0 with_discs=0 version=
while (( $# )); do
    case $1 in
        --clean) clean=1 ;;
        --with-discs) with_discs=1 ;;
        --version) version=${2:?--version needs a value, e.g. 1.2.0}; version=v${version#v}; shift ;;
        *) echo "unknown option $1 (see the top of build.sh)" >&2; exit 2 ;;
    esac
    shift
done
if [[ -z $version ]]; then
    version=$(git describe --tags --always 2>/dev/null || echo dev)
    # (submodules don't count: the build itself patches FFmpeg and fills ps5-plume-triangle)
    [[ -z $(git status --porcelain --ignore-submodules=all 2>/dev/null) ]] || version+=-modified
fi
zip_name=splinter-cell-blacklist-ps5-$version.zip

if (( clean )); then
    step "Removing the game build and the recompiled code"
    "${dev[@]}" bash -c 'rm -rf out/* generated/*'
fi

xex=disc-files/disc1/default.xex
if [[ ! -f $xex ]]; then
    echo "No $xex: copy default.xex from your own extracted disc 1 there (README.md)." >&2
    exit 2
fi

if [[ ! -f thirdparty/rexglue-sdk/thirdparty/fmt/CMakeLists.txt ||
      ! -f thirdparty/ps5-plume-triangle/Makefile ]]; then
    step "Fetching the submodules"
    git submodule update --init --recursive
fi

# The SDK's FFmpeg has no PS5 configuration of its own.
ffmpeg=thirdparty/rexglue-sdk/thirdparty/FFmpeg
if [[ ! -f $ffmpeg/config_ps5_x86_64.h ]]; then
    step "Adding the PS5 configuration to FFmpeg"
    git -C "$ffmpeg" apply "$here/patches/ffmpeg-ps5-config.patch"
fi

step "PS5 payload SDK, RADV and the app runtime (ps5-plume-triangle, once)"
"${dev[@]}" bash -c '
    set -e
    cd /work/ps5-plume-triangle
    [[ -f .deps/native/radv-release/lib/libvulkan_radeon.ps5.a ]] || make deps
    [[ -x build/host/ps5-native-tool && -f build/obj/app_crt.o ]] || make app'

step "The rexglue tool for Linux (once)"
"${dev[@]}" bash -c '
    set -e
    [[ -x $REXGLUE ]] && exit 0
    cd /work/rexglue-sdk
    cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
        -DREXGLUE_ENABLE_TRACY=OFF
    cmake --build --preset linux-amd64-release --target rexglue'

step "Recompiling the game's code from default.xex (once, 10-15 minutes)"
"${dev[@]}" bash -c '
    set -e
    [[ -f generated/default/sources.cmake ]] && exit 0
    "$REXGLUE" codegen blacklist_manifest.toml'

step "Building the PS5 app"
"${dev[@]}" bash ps5/build.sh

step "Packaging dist/$zip_name"
rm -rf "dist/$title_id" "dist/$zip_name"
mkdir -p dist
"${dev[@]}" bash -c "cp -r out/ps5/ps5-app/$title_id dist/"
# The version: in VERSION.txt, and as the app's content version when it's x.y.z (00.010.000 is
# 0.10.0's).
printf '%s\nbuilt %s from %s\n' "$version" "$(date -u +%Y-%m-%d)" \
    "$(git rev-parse --short HEAD 2>/dev/null || echo '?')" > "dist/$title_id/VERSION.txt"
if [[ $version =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    content_version=$(printf '%02d.%03d.%03d' "$((10#${BASH_REMATCH[1]}))" \
        "$((10#${BASH_REMATCH[2]}))" "$((10#${BASH_REMATCH[3]}))")
    python3 -I - "dist/$title_id/sce_sys/param.json" "$content_version" <<'PY'
import json, sys
path, content_version = sys.argv[1], sys.argv[2]
with open(path) as f:
    param = json.load(f)
param["contentVersion"] = content_version
with open(path, "w") as f:
    json.dump(param, f, indent=2)
    f.write("\n")
PY
fi
(cd dist && zip -qr "$zip_name" "$title_id")
echo "dist/$zip_name: copy its $title_id folder to /data/homebrew on the PS5, then add your discs (README.md)."

if (( with_discs )); then
    step "Copying your discs into dist/$title_id/game (not into the zip)"
    for disc in disc1 disc2; do
        if [[ -f disc-files/$disc/default.xex ]]; then
            rsync -a --exclude '$SystemUpdate' "disc-files/$disc/" "dist/$title_id/game/$disc/"
            echo "  $disc: $(du -sh "dist/$title_id/game/$disc" | cut -f1)"
        else
            echo "  $disc: not in disc-files/$disc, skipped"
        fi
    done
    echo "dist/$title_id is the whole app with your discs: copy it to /data/homebrew on your PS5."
fi
