#!/usr/bin/env python3
"""Makes a replacement that blanks a texture (black, transparent): for effects drawn additively,
such as the lens flare's halo ring, which then adds nothing. ORIGINAL is the texture as a frame
report saved it (tex_<base>_<dword1>_<dword2>.raw: its first 64 KB) or texture_dump_dir did.

  python3 -I tools/make_blank_texture.py ORIGINAL OUT_DIR

Writes OUT_DIR/<FNV-1a 64 of the original>.raw for texture_replace_dir. Standard library only.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ps_buttons import fnv1a64  # noqa: E402
from xbox_texture import DXT1, DXT5, encode, tiled_offset  # noqa: E402


def guest_base_size(width: int, height: int, fmt: int) -> int:
    """The texture cache's base extent of a tiled DXT texture (GetTiledAddressUpperBound2D)."""
    bw, bh = (width + 3) // 4, (height + 3) // 4
    bpp_log2 = 3 if fmt == DXT1 else 4
    pitch = (bw + 31) & ~31
    return tiled_offset((bw - 1) & ~31, (bh - 1) & ~31, pitch, bpp_log2) + (0x400 << bpp_log2)


def main() -> int:
    original, out_dir = sys.argv[1], sys.argv[2]
    parts = os.path.basename(original).split("_")
    dword1, dword2 = int(parts[2], 16), int(parts[3][:8], 16)
    fmt, endian = dword1 & 0x3F, (dword1 >> 6) & 3
    width, height = (dword2 & 0x1FFF) + 1, ((dword2 >> 13) & 0x1FFF) + 1
    if fmt not in (DXT1, DXT5):
        raise SystemExit(f"format {fmt}: only DXT1 and DXT5")
    size = guest_base_size(width, height, fmt)
    raw = open(original, "rb").read()
    if len(raw) < size:
        raise SystemExit(f"{original} has {len(raw)} bytes, the texture {size}")
    raw = raw[:size]
    blank = [[(0, 0, 0, 0)] * width for _ in range(height)]
    out = encode(blank, raw, width, height, fmt, endian)
    os.makedirs(out_dir, exist_ok=True)
    target = os.path.join(out_dir, f"{fnv1a64(raw):016X}.raw")
    with open(target, "wb") as f:
        f.write(out)
    print(f"{width}x{height} format {fmt}, {size} bytes -> {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
