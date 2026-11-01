#!/usr/bin/env bash
# Splinter Cell: Blacklist for PS5: link the PS5 build and package it as a PS5 homebrew app folder
# (eboot.bin, sce_module/libc.prx, sce_sys). From gta4-ps5's ps5/tools/ps5_package.sh (elripalda,
# GPL-3.0): the link and the signing are its recipe; the packaging is this game's.
#
#   ps5/tools/ps5_package.sh <cmake build dir> <ps5-plume-triangle checkout> [title id]
#
# The link and packaging reuse the PS5 tooling of Nazky's ps5-plume-triangle (mihawk-99's RADV
# link recipe, app CRT, libc.prx builder and FSELF signer): build that project once first (its
# build/ holds app_crt.o, app_cpp_runtime.o, the libSceAgc stubs and ps5-native-tool).
# No game files are packaged: they go in the app's game/ folder on the PS5 (game/README.txt).
set -euo pipefail
build=$(realpath "${1:?cmake build dir}")
tri=$(realpath "${2:?ps5-plume-triangle checkout}")
title_id=${3:-PPSA90827}
ps5dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk_root=${PS5_PAYLOAD_SDK:-$tri/.deps/native/ps5-payload-sdk}
radv_archive=${RADV_ARCHIVE:-$tri/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
tool=$tri/build/host/ps5-native-tool
for f in "$sdk_root/bin/prospero-lld" "$radv_archive" "$tool" "$tri/build/obj/app_crt.o" \
         "$tri/build/obj/app_cpp_runtime.o" "$tri/build/stubs/libSceAgc.so" "$tri/runtime/libc.prx"; do
    [[ -e $f ]] || { echo "missing $f (build ps5-plume-triangle first)" >&2; exit 2; }
done
# Staging root for radv_link_recipe: it reads tooling/ and vendor/ and writes build/.
stage=$build/ps5-link
mkdir -p "$stage/build"
ln -sfn "$tri/tooling" "$stage/tooling"
ln -sfn "$tri/vendor" "$stage/vendor"
# The objects and static libraries CMake links into blacklist, in its order.
link_cmd=$(ninja -C "$build" -t commands blacklist | tail -1)
mapfile -t inputs < <(cd "$build" && for w in $link_cmd; do
    case $w in
        */libz.a) ;;  # (RADV's archive has zlib in it)
        *.o|*.a) [[ $w = /* ]] && echo "$w" || echo "$build/$w" ;;
    esac
done)
echo "==> [link] ${#inputs[@]} objects and libraries from CMake"
export PS5_CLANG=${PS5_CLANG:-$(command -v clang-19 || command -v clang)}
source "$tri/tools/radv-link.sh"
radv_link_recipe "$stage" "$sdk_root" "$radv_archive"
# The recipe's emulated TLS with a thread-exit fix (ps5_emutls.c), compiled over the recipe's
# object, which the link uses.
"$PS5_CLANG" --target=x86_64-sie-ps5 -fPIC -fno-plt -fno-stack-protector \
    -isysroot "$sdk_root" -isystem "$sdk_root/target/include" \
    -O2 -c "$ps5dir/tools/ps5_emutls.c" -o "$stage/build/ps5-emutls-cxa.o"
# stat("") / lstat("") as on Linux (ps5_stat.c, through --wrap).
"$sdk_root/bin/prospero-clang" -c -O2 -fno-visibility-from-dllstorageclass -o "$stage/build/ps5_stat.o" "$ps5dir/tools/ps5_stat.c"
link_pie() {  # extra objects, if any
    "$sdk_root/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
        --version-script "$tri/tooling/native/app-symbols.map" --exclude-libs=ALL \
        --wrap=stat --wrap=lstat \
        -e _start -o "$stage/build/llvm-pie.elf" \
        "$tri/build/obj/app_crt.o" "$tri/build/obj/app_cpp_runtime.o" "$stage/build/ps5_stat.o" "$@" \
        --start-group "${inputs[@]}" --end-group \
        "$tri/build/stubs/libSceAgc.so" "$tri/build/stubs/libSceAgcDriver.so" \
        "${radv_link_inputs[@]}" \
        --as-needed "$sdk_root"/target/lib/*.so
}
make_eboot() {
    "$tool" link --in "$stage/build/llvm-pie.elf" --out "$stage/build/eboot.elf" \
        --stub-dir "$sdk_root/target/lib" --stub "$tri/build/stubs/libSceAgc.so" \
        --stub "$tri/build/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
        --companion-sdk 0x08050001 --file-name eboot.elf
}
echo "==> [link] Blacklist + RADV into a PS5 PIE ELF"
link_pie
echo "==> [package] eboot.bin for $title_id"
if ! make_eboot 2> "$stage/build/eboot.err"; then
    cat "$stage/build/eboot.err" >&2
    # The tool appends the process parameters right after the image's RELRO content, inside its
    # last page; when that content happens to end near the page boundary there is no room. Link
    # again with half a page of RELRO padding (ps5_relro_pad.c), which moves the end clear of it.
    grep -q "no room for PS5 process parameters" "$stage/build/eboot.err" || exit 1
    echo "==> [link] again with 2 KB of RELRO padding (room for the process parameters)"
    "$sdk_root/bin/prospero-clang" -c -O2 -fPIC -fno-visibility-from-dllstorageclass \
        -o "$stage/build/ps5_relro_pad.o" "$ps5dir/tools/ps5_relro_pad.c"
    link_pie "$stage/build/ps5_relro_pad.o"
    make_eboot
fi
app=$build/ps5-app/$title_id
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/game/disc1" "$app/game/disc2"
"$tool" self --sign --in "$stage/build/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$tri/runtime/libc.prx" "$app/sce_module/libc.prx"
# The app's parameters: ps5/package/sce_sys/param.json (copied with the rest of sce_sys below),
# its creation date set to today's. (build.sh sets contentVersion from VERSION.txt.)
# The home screen icon and anything else under ps5/package/sce_sys.
if [[ -d $ps5dir/package/sce_sys ]]; then
    cp -r "$ps5dir/package/sce_sys/." "$app/sce_sys/"
fi
sed -i "s/\"creationDate\": \"[^\"]*\"/\"creationDate\": \"$(date -u +%Y-%m-%d) 00:00:00\"/" \
    "$app/sce_sys/param.json"
# The launcher's background and font (src/disc_launcher.h).
mkdir -p "$app/launcher"
cp "$ps5dir/assets/launcher-background.png" "$ps5dir/assets/Roboto-Medium.ttf" "$app/launcher/"
# Replacement textures (PlayStation button prompts), <hash>.raw: texture_replace_dir.
mkdir -p "$app/launcher/textures"
if compgen -G "$ps5dir/assets/textures/*.raw" > /dev/null; then
    cp "$ps5dir"/assets/textures/*.raw "$app/launcher/textures/"
fi
cat > "$app/game/README.txt" <<EOF
Splinter Cell: Blacklist for PS5 - put your own extracted Xbox 360 discs here: disc 1 (Campaign
Part 1 & Multiplayer) in disc1/ and disc 2 (Campaign Part 2) in disc2/, each holding default.xex
and the rest of its disc's root. The launcher picks one at start.
No game files come with this app.
EOF
# The settings (ReXApp reads <exe folder>/blacklist.toml): ReXGlue 0.10.0 runs no GPU emulation
# unless gpu_plugin names one. The game folder is the launcher's choice (game/disc1 or game/disc2).
# Saves go to /app0/UserData/blacklist (both discs share them), the log to /app0/logs.
cat > "$app/blacklist.toml" <<'EOF'
gpu_plugin = "xenos"
occlusion_query_enable = false
log_level = "info"
# Wait for a new shader instead of drawing without it: with background compiling, scenes went
# black for seconds while their shaders compiled (the PC port did the same). Compiled shaders are
# kept in UserData/blacklist/cache, so a scene pauses only the first time.
async_shader_compilation = false
# The game's texture fetch constants often have the "invalid" type (thousands a minute in the
# story): skipped, those textures drew black, the 3D world a black screen with only its lights.
gpu_allow_invalid_fetch_constants = true
# Run screen-space draws' vertex shaders on the CPU to find how much of the EDRAM they use (Xenia's
# default). Without it a 4x MSAA stencil clear (the silent kill's effect) claimed the whole EDRAM
# and wiped the G-buffer: the world went black but for its lights, whole or half the screen.
execute_unclipped_draw_vs_on_cpu = true
# The Xbox button prompts drawn as PlayStation ones (ps5/assets/textures). To collect more of the
# game's small textures: texture_dump_dir = "/app0/logs/textures"
texture_replace_dir = "/app0/launcher/textures"
# EDRAM through fragment shader interlock (Broken Bond needed it for black 3D in its story mode):
# render_target_path_vulkan = "fsi"
EOF
"$tool" self --inspect --file "$app/eboot.bin" | tail -3
echo "==> $app ($(stat -c %s "$app/eboot.bin") bytes)"
