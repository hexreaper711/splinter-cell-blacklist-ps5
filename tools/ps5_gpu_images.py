#!/usr/bin/env python3
"""Turns the pictures the PS5 build writes during a GPU frame report (L3+R3 with GPU readback
Full: logs/frame/*.raw, the guest's tiled bytes of each big resolve, its registers in the name)
into PNGs, to see which pass of a frame goes wrong.

  python3 tools/ps5_gpu_images.py build-logs/console/frame build-logs/console/frame-png

The file name: NNN_a<written address>_n<length>_d<RB_COPY_DEST_BASE>_i<RB_COPY_DEST_INFO>_
p<RB_COPY_DEST_PITCH>_c<RB_COPY_CONTROL>.raw. Standard library only.
"""
from __future__ import annotations

import re
import struct
import sys
import zlib
from pathlib import Path

NAMES = {6: "8888", 7: "2_10_10_10", 25: "16_16", 26: "16_16_16_16", 31: "16_16_FLOAT",
         32: "16_16_16_16_FLOAT"}
BYTES = {26: 8, 32: 8}


def tiled_offset(x: int, y: int, pitch: int, bpp_log2: int) -> int:
    """GetTiledOffset2D of the SDK (graphics/pipeline/texture/util.cpp)."""
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpp_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpp_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(
            ">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def half(h: int) -> float:
    return struct.unpack("<e", struct.pack("<H", h))[0]


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    name_re = re.compile(r"(\d+)_a([0-9A-F]+)_n([0-9A-F]+)_d([0-9A-F]+)_i([0-9A-F]+)_"
                         r"p([0-9A-F]+)_c([0-9A-F]+)\.raw")
    for raw in sorted(src.glob("*.raw")):
        m = name_re.fullmatch(raw.name)
        if not m:
            continue
        seq = int(m[1])
        address, dest, info, pitch_reg, copy = (int(m[i], 16) for i in (2, 4, 5, 6, 7))
        data = raw.read_bytes()
        fmt = (info >> 7) & 0x3F
        depth = (copy & 7) == 4
        width, height = pitch_reg & 0x3FFF, (pitch_reg >> 16) & 0x3FFF
        bpp = BYTES.get(fmt, 4)
        bpp_log2 = bpp.bit_length() - 1
        base = dest - address  # where the picture starts in the bytes written
        pixels = []
        for y in range(height):
            for x in range(width):
                at = base + tiled_offset(x, y, width, bpp_log2)
                pixels.append(data[at:at + bpp] if 0 <= at and at + bpp <= len(data) else b"")
        # Values per channel, then scaled to the picture's own range so it shows.
        values = []
        for p in pixels:
            if len(p) < bpp:
                values.append((0.0, 0.0, 0.0))
            elif depth:
                d = int.from_bytes(p[:4], "big") >> 8
                values.append((d, d, d))
            elif fmt in (26, 32):
                ch = struct.unpack(">4H", p)
                if fmt == 32:
                    values.append(tuple(max(0.0, half(c)) for c in ch[:3]))
                else:
                    values.append(tuple(float(c if c < 32768 else 0) for c in ch[:3]))
            elif fmt in (31, 25):
                ch = struct.unpack(">2H", p)
                v = [max(0.0, half(c)) for c in ch] if fmt == 31 else [float(c) for c in ch]
                values.append((v[0], v[1], 0.0))
            else:
                values.append((float(p[2]), float(p[1]), float(p[0])))  # (B, G, R, A in memory)
        # Scaled to the 99th percentile, not the maximum: a few bright lights would leave
        # everything else in an HDR picture black.
        brightest = sorted(max(v) for v in values)
        top = (brightest[len(brightest) * 99 // 100] if brightest else 0.0) or \
            (brightest[-1] if brightest else 0.0) or 1.0
        rgb = bytes(min(255, int(255 * c / top)) for v in values for c in v)
        kind = "depth" if depth else NAMES.get(fmt, f"fmt{fmt}")
        name = f"{seq:03}_{kind}_{width}x{height}_d{dest:08X}.png"
        png(out / name, width, height, rgb)
        print(f"{name}  (max {top:g})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
