#!/usr/bin/env bash
# Builds Splinter Cell: Blacklist for PS5 from scratch into dist/PPSA90827.zip, a homebrew app
# folder without any game files (you add your own discs on the PS5, see README.md).
#   ./build.sh               every step that isn't done yet
#   ./build.sh --clean       the game build and the recompiled code again (not the dependencies)
#   ./build.sh --with-discs  also copy your discs (disc-files/disc1, disc2) into the app folder
#                            dist/PPSA90827, for your own PS5 (the zip stays without them)
# Needs Docker, the submodules (git submodule update --init --recursive) and your own disc 1's
# default.xex in disc-files/disc1/. The first run builds the dependencies (PS5 payload SDK, RADV,
# the rexglue tool) and takes a few hours on a Mac; later runs only rebuild what changed.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$here"
dev=("$here/dev.sh")
title_id=PPSA90827

step() { printf '\n==> %s\n' "$*"; }

clean=0 with_discs=0
for arg in "$@"; do
    case $arg in
        --clean) clean=1 ;;
        --with-discs) with_discs=1 ;;
        *) echo "unknown option $arg (see the top of build.sh)" >&2; exit 2 ;;
    esac
done

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

step "Packaging dist/$title_id.zip"
rm -rf "dist/$title_id" "dist/$title_id.zip"
mkdir -p dist
"${dev[@]}" bash -c "cp -r out/ps5/ps5-app/$title_id dist/"
(cd dist && zip -qr "$title_id.zip" "$title_id")
echo "dist/$title_id.zip: copy its $title_id folder to /data/homebrew on the PS5, then add your discs (README.md)."

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
