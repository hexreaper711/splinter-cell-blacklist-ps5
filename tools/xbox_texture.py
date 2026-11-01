#!/usr/bin/env python3
"""Xbox 360 texture bytes (tiled, 8in16 endianness: DXT1 / DXT5) <-> RGBA pixels, for replacement
textures: decode a texture the game dumped (texture_dump_dir), edit it, encode it back into the
same layout and size (texture_replace_dir). Standard library only.

  from xbox_texture import decode, encode
  w, h, rgba = decode(raw, w, h, fmt, endian)       # rgba: rows of (r, g, b, a)
  raw2 = encode(rgba, raw, w, h, fmt, endian)       # raw: the original, for its size and padding
"""
from __future__ import annotations

import struct

DXT1, DXT5 = 18, 20


def tiled_offset(x: int, y: int, pitch: int, bpp_log2: int) -> int:
    """GetTiledOffset2D of the SDK (graphics/pipeline/texture/util.cpp), in blocks."""
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpp_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpp_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def _swap16(b: bytes) -> bytes:
    return bytes(v for i in range(0, len(b), 2) for v in (b[i + 1], b[i]))


def _c565(c: int) -> tuple[int, int, int]:
    return ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def _to565(r: int, g: int, b: int) -> int:
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def _palette(c0: int, c1: int, dxt1: bool) -> list[tuple[int, int, int]]:
    p = [_c565(c0), _c565(c1)]
    if c0 > c1 or not dxt1:
        p += [tuple((2 * a + b) // 3 for a, b in zip(p[0], p[1])),
              tuple((a + 2 * b) // 3 for a, b in zip(p[0], p[1]))]
    else:
        p += [tuple((a + b) // 2 for a, b in zip(p[0], p[1])), (0, 0, 0)]
    return p


def _alphas(a0: int, a1: int) -> list[int]:
    if a0 > a1:
        return [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    return [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]


def _layout(w: int, h: int, fmt: int):
    dxt1 = fmt == DXT1
    bs = 8 if dxt1 else 16
    return dxt1, bs, (w + 3) // 4, (h + 3) // 4, 3 if dxt1 else 4


def decode(raw: bytes, w: int, h: int, fmt: int, endian: int):
    dxt1, bs, bw, bh, bpp_log2 = _layout(w, h, fmt)
    out = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for by in range(bh):
        for bx in range(bw):
            o = tiled_offset(bx, by, bw, bpp_log2)
            blk = raw[o:o + bs]
            if len(blk) < bs:
                continue
            if endian == 1:
                blk = _swap16(blk)
            c0, c1, bits = struct.unpack('<HHI', blk[-8:])
            pal = _palette(c0, c1, dxt1)
            if dxt1:
                al = [0 if (c0 <= c1 and (bits >> (2 * i)) & 3 == 3) else 255 for i in range(16)]
            else:
                t = _alphas(blk[0], blk[1])
                ab = int.from_bytes(blk[2:8], 'little')
                al = [t[(ab >> (3 * i)) & 7] for i in range(16)]
            for i in range(16):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                if x < w and y < h:
                    out[y][x] = pal[(bits >> (2 * i)) & 3] + (al[i],)
    return w, h, out


def _encode_block(px: list[tuple[int, int, int, int]], dxt1: bool) -> bytes:
    # Color: the endpoints spanning the box of the (visible) pixels, nearest of 4 for each.
    vis = [p for p in px if p[3] > 8] or px
    lo = tuple(min(p[k] for p in vis) for k in range(3))
    hi = tuple(max(p[k] for p in vis) for k in range(3))
    c0, c1 = _to565(*hi), _to565(*lo)
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        c1 = c1 - 1 if c1 else c1
        if c0 == c1:
            c0 = c0 + 1 if c0 < 0xFFFF else c0
    pal = _palette(c0, c1, False)
    bits = 0
    for i, p in enumerate(px):
        d = [sum((p[k] - c[k]) ** 2 for k in range(3)) for c in pal]
        bits |= d.index(min(d)) << (2 * i)
    color = struct.pack('<HHI', c0, c1, bits)
    if dxt1:
        return color
    a0, a1 = max(p[3] for p in px), min(p[3] for p in px)
    if a0 == a1:
        a1 = a0 - 1 if a0 else 0
        if a0 == a1:
            a0 = 1
    t = _alphas(a0, a1)
    ab = 0
    for i, p in enumerate(px):
        d = [abs(p[3] - v) for v in t]
        ab |= d.index(min(d)) << (3 * i)
    return bytes([a0, a1]) + ab.to_bytes(6, 'little') + color


def encode(rgba, raw: bytes, w: int, h: int, fmt: int, endian: int) -> bytes:
    dxt1, bs, bw, bh, bpp_log2 = _layout(w, h, fmt)
    out = bytearray(raw)
    for by in range(bh):
        for bx in range(bw):
            px = [rgba[min(by * 4 + i // 4, h - 1)][min(bx * 4 + i % 4, w - 1)] for i in range(16)]
            blk = _encode_block(px, dxt1)
            if endian == 1:
                blk = _swap16(blk)
            o = tiled_offset(bx, by, bw, bpp_log2)
            out[o:o + bs] = blk
    return bytes(out)
