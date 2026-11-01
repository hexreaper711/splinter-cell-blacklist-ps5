#!/usr/bin/env python3
"""Decodes the PS5 build's GPU frame report ("gpu frame:" lines, L3+R3 in the game: the SDK's
graphics/vulkan/command_processor.cpp) into readable lines: per run of draws the render targets,
depth, blending and textures; per resolve what is copied where.

  python3 tools/ps5_gpu_frame.py build-logs/console/blacklist_027.log [--frame N]
"""
from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

COLOR_RT = {0: "8888", 1: "8888_GAMMA", 2: "2_10_10_10", 3: "2_10_10_10_FLOAT(7e3)", 4: "16_16",
            5: "16_16_16_16", 6: "16_16_FLOAT", 7: "16_16_16_16_FLOAT", 10: "2_10_10_10_AS_10",
            12: "7e3_AS_16_16_16_16", 14: "32_FLOAT", 15: "32_32_FLOAT"}
TEX = {2: "8", 4: "565", 6: "8888", 7: "2_10_10_10", 10: "88", 15: "4444", 16: "10_11_11",
       17: "11_11_10", 18: "DXT1", 19: "DXT2_3", 20: "DXT4_5", 22: "24_8", 23: "24_8_FLOAT",
       24: "16", 25: "16_16", 26: "16_16_16_16", 30: "16_FLOAT", 31: "16_16_FLOAT",
       32: "16_16_16_16_FLOAT", 33: "32", 36: "32_FLOAT", 37: "32_32_FLOAT",
       38: "32_32_32_32_FLOAT", 49: "DXN", 50: "8888_AS_16", 54: "2_10_10_10_AS_16",
       58: "DXT3A", 59: "DXT5A", 60: "CTX1", 62: "8888_GAMMA_EDRAM", 63: "7e3_EDRAM"}
ZFUNC = ["never", "less", "equal", "lequal", "greater", "notequal", "gequal", "always"]
BLEND = ["0", "1", "?", "?", "sc", "1-sc", "sa", "1-sa", "dc", "1-dc", "da", "1-da", "cc",
         "1-cc", "ca", "1-ca", "sat"]


def rt(info: int, surface: int) -> str:
    return f"{COLOR_RT.get((info >> 16) & 15, (info >> 16) & 15)}@{info & 0xFFF}"


def tex(d: list[int]) -> str:
    kind = d[0] & 3
    fmt = d[1] & 0x3F
    base = d[1] & 0xFFFFF000
    w = (d[2] & 0x1FFF) + 1
    h = ((d[2] >> 13) & 0x1FFF) + 1
    dim = (d[5] >> 9) & 3
    tiled = d[0] >> 31
    sign = "" if kind == 2 else ("INVALID" if kind in (0, 1) else "vtx")
    return f"{TEX.get(fmt, fmt)} {w}x{h}{'' if dim == 1 else ' dim' + str(dim)} @{base:08X}" \
           f"{' tiled' if tiled else ''}{' ' + sign if sign else ''}"


def blend(b: int) -> str:
    src, comb, dst = b & 31, (b >> 5) & 7, (b >> 8) & 31
    if src == 1 and dst == 0 and comb == 0:
        return "opaque"
    return f"{BLEND[src] if src < len(BLEND) else src}*s+{BLEND[dst] if dst < len(BLEND) else dst}*d"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("log", type=Path)
    ap.add_argument("--frame", type=int, default=1, help="which report (1 = the first)")
    args = ap.parse_args()
    frame = 0
    draw_re = re.compile(r"gpu frame: draw x(\d+) \((\d+) indices\) (.*)$")
    for line in args.log.read_text(errors="replace").splitlines():
        if "gpu frame: start" in line:
            frame += 1
            if frame == args.frame:
                print(f"=== frame {frame} {line[12:24]}")
            continue
        if frame != args.frame or "gpu frame:" not in line:
            continue
        m = draw_re.search(line)
        if m:
            count, indices, rest = m.groups()
            f = rest.split()
            v = dict(zip(f[0::2][:8], f[1::2][:8]))
            # positional: prim P vs V ps P surface S mode M color c0 c1 c2 c3 mask M depth D
            # depthctl C blend b0 b1 b2 b3 then textures
            prim = int(f[1]); vs = f[3]; ps = f[5]; surface = int(f[7], 16); mode = int(f[9], 16)
            colors = [int(x, 16) for x in f[11:15]]
            mask = int(f[16], 16); depth = int(f[18], 16); depthctl = int(f[20], 16)
            blends = [int(x, 16) for x in f[22:26]]
            texs = []
            i = 26
            while i + 6 < len(f) and not f[i].startswith("pc"):
                texs.append((f[i], [int(x, 16) for x in f[i + 1:i + 7]]))
                i += 7
            # Then the pixel shader's float constants, if logged: pc<n> and 4 words.
            consts = []
            while i + 4 < len(f) and f[i].startswith("pc"):
                words = [struct.unpack(">f", int(x, 16).to_bytes(4, "big"))[0]
                         for x in f[i + 1:i + 5]]
                consts.append(f"c{f[i][2:]}=({', '.join(f'{w:g}' for w in words)})")
                i += 5
            targets = [rt(colors[n], surface) for n in range(4) if (mask >> (4 * n)) & 15]
            z = ""
            if depthctl & 2:
                z = f"z {ZFUNC[(depthctl >> 4) & 7]}{' write' if depthctl & 4 else ''}" \
                    f" {'D24FS8' if (depth >> 16) & 1 else 'D24S8'}@{depth & 0xFFF}"
            msaa = (surface >> 16) & 3
            print(f"draw x{count:<4} prim {prim:<2} ps {ps[-6:] if ps != '0' * 16 else 'none  '}"
                  f" rt [{', '.join(targets) or 'none'}]{' msaa' + str(1 << msaa) if msaa else ''}"
                  f" {z} {blend(blends[0]) if targets else ''}")
            shown = set()
            for name, d in texs:
                if name not in shown:
                    shown.add(name)
                    print(f"      {name}: {tex(d)}")
            if consts:
                print(f"      {' '.join(consts)}")
            continue
        if "gpu frame: rt " in line or "gpu frame: resolve reads" in line:
            continue  # (the render target cache's ownership log)
        if "gpu frame: resolved" in line:
            print("        data " + line.split("gpu frame: resolved ")[1])
            continue
        if "gpu frame: resolve" in line:
            f = line.split("resolve ")[1].split()
            copy = int(f[1], 16); dest = int(f[3], 16); info = int(f[5], 16)
            pitch = int(f[7], 16); surface = int(f[9], 16)
            colors = [int(x, 16) for x in f[11:15]]; depth = int(f[16], 16)
            src = copy & 7
            source = f"depth@{depth & 0xFFF}" if src == 4 else rt(colors[src], surface)
            clear = []
            if copy & 0x100:
                clear.append("color")
            if copy & 0x200:
                clear.append("depth")
            print(f"RESOLVE {source} -> {TEX.get((info >> 7) & 0x3F, (info >> 7) & 0x3F)} "
                  f"{pitch & 0x3FFF}x{(pitch >> 16) & 0x3FFF} @{dest:08X}"
                  f"{' clear ' + '+'.join(clear) if clear else ''}")
            continue
        if "gpu frame: memexport" in line:
            print("        writes memory " + line.split("gpu frame: memexport ")[1])
            continue
        if "gpu frame: swap" in line:
            print(line.split("gpu frame: ")[1])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
