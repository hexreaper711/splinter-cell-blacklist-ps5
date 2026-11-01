#!/usr/bin/env python3
"""Makes the PlayStation button prompts: the game's Xbox button icons (64x64 DXT5 textures, as
saved by a frame report or texture_dump_dir: 16 KB each) redrawn with the PlayStation symbols,
written as ps5/assets/textures/<FNV-1a 64 of the original>.raw for texture_replace_dir.

  python3 -I tools/make_ps_buttons.py ORIGINALS_DIR ps5/assets/textures [preview.png]

ORIGINALS_DIR holds <name>.raw files named after the Xbox button: a, b, x, y, lb, rb, lt, rt.
Standard library only.
"""
from __future__ import annotations

import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbox_texture import DXT5, decode, encode  # noqa: E402

SIZE, ENDIAN = 64, 1
# Xbox face button -> PlayStation symbol and its color.
FACE = {"a": ("cross", (124, 178, 232)), "b": ("circle", (232, 92, 92)),
        "x": ("square", (226, 140, 200)), "y": ("triangle", (64, 196, 160))}
SHOULDER = {"lb": "L1", "rb": "R1", "lt": "L2", "rt": "R2"}
# Stroke font in a unit box (x right, y down): polylines.
GLYPHS = {
    "L": [[(0, 0), (0, 1), (0.8, 1)]],
    "R": [[(0, 1), (0, 0), (0.6, 0), (0.85, 0.13), (0.85, 0.37), (0.6, 0.5), (0, 0.5)],
          [(0.45, 0.5), (0.9, 1)]],
    "1": [[(0.15, 0.22), (0.55, 0), (0.55, 1)]],
    "2": [[(0.05, 0.22), (0.25, 0.03), (0.6, 0), (0.88, 0.17), (0.88, 0.38), (0.05, 1),
           (0.92, 1)]],
}


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def coverage(inside, x, y, n=4):
    """Fraction of an n x n grid of subsamples of pixel (x, y) for which inside(px, py)."""
    return sum(inside(x + (i + 0.5) / n, y + (j + 0.5) / n) for i in range(n) for j in range(n)) / (n * n)


def blend(dst, color, a):
    return tuple(round(d * (1 - a) + c * a) for d, c in zip(dst[:3], color)) + (dst[3],)


def luminance(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def face_button(img, symbol, color):
    # The disc: where the icon is opaque; its center and radius from that.
    pts = [(x + 0.5, y + 0.5) for y in range(SIZE) for x in range(SIZE) if img[y][x][3] > 128]
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    r = math.sqrt(len(pts) / math.pi)
    out = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            a = img[y][x][3]
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            # Dark grey body, lighter at the top, a light rim like PlayStation's own prompts.
            shade = 58 - 22 * (y - (cy - r)) / (2 * r)
            body = (round(shade), round(shade), round(shade + 3))
            rim = max(0.0, min(1.0, (d - (r - 3.2)) / 1.2))
            row.append(blend(body + (a,), (150, 150, 156), rim * 0.85))
        out.append(row)
    s = r * 0.5  # symbol half size
    t = r * 0.13  # stroke half width
    if symbol == "cross":
        inside = lambda px, py: min(seg_dist(px, py, cx - s, cy - s, cx + s, cy + s),
                                    seg_dist(px, py, cx - s, cy + s, cx + s, cy - s)) <= t
    elif symbol == "circle":
        inside = lambda px, py: abs(math.hypot(px - cx, py - cy) - s * 0.92) <= t
    elif symbol == "square":
        q = s * 0.82
        inside = lambda px, py: (max(abs(px - cx), abs(py - cy)) <= q + t and
                                 max(abs(px - cx), abs(py - cy)) >= q - t)
    else:
        h = s * 1.0
        tri = [(cx, cy - h), (cx + h * 0.95, cy + h * 0.68), (cx - h * 0.95, cy + h * 0.68)]
        inside = lambda px, py: min(seg_dist(px, py, *tri[i], *tri[(i + 1) % 3])
                                    for i in range(3)) <= t
    for y in range(SIZE):
        for x in range(SIZE):
            c = coverage(inside, x, y)
            if c:
                out[y][x] = blend(out[y][x], color, c)
    return out


def shoulder_button(img, text):
    # The old letters: bright pixels inside the shape. Painted over with the body's color, the new
    # label drawn in their place.
    opaque = [img[y][x] for y in range(SIZE) for x in range(SIZE) if img[y][x][3] > 200]
    body = sorted(opaque, key=luminance)[len(opaque) // 2]
    letters = [(x, y) for y in range(SIZE) for x in range(SIZE)
               if img[y][x][3] > 200 and luminance(img[y][x]) > luminance(body) + 60]
    x0 = min(p[0] for p in letters); x1 = max(p[0] for p in letters) + 1
    y0 = min(p[1] for p in letters); y1 = max(p[1] for p in letters) + 1
    out = [list(row) for row in img]
    for y in range(max(0, y0 - 2), min(SIZE, y1 + 2)):
        for x in range(max(0, x0 - 2), min(SIZE, x1 + 2)):
            if img[y][x][3] > 200 and luminance(img[y][x]) > luminance(body) + 15:
                out[y][x] = body[:3] + (img[y][x][3],)
    hgt = (y1 - y0) * 0.9
    wid = hgt * 0.62
    gap = hgt * 0.22
    total = wid * len(text) + gap * (len(text) - 1)
    ox = (x0 + x1) / 2 - total / 2
    oy = (y0 + y1) / 2 - hgt / 2
    t = hgt * 0.1
    segs = []
    for i, ch in enumerate(text):
        bx = ox + i * (wid + gap)
        for line in GLYPHS[ch]:
            for (ax, ay), (bx2, by2) in zip(line, line[1:]):
                segs.append((bx + ax * wid, oy + ay * hgt, bx + bx2 * wid, oy + by2 * hgt))
    inside = lambda px, py: min(seg_dist(px, py, *sg) for sg in segs) <= t
    for y in range(SIZE):
        for x in range(SIZE):
            c = coverage(inside, x, y)
            if c:
                out[y][x] = blend(out[y][x], (240, 240, 240), c)
    return out


def png(path, imgs):
    gap = 4
    w = len(imgs) * (SIZE + gap)
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for img in imgs:
            for x in range(SIZE):
                r, g, b, a = img[y][x]
                bg = 90 if ((x // 8 + y // 8) & 1) else 60
                row += bytes((r * a + bg * (255 - a)) // 255 for r in (r, g, b))
            row += b"\x30" * (gap * 3)
        rows.append(b"\x00" + bytes(row))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, SIZE, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    previews = []
    for name in ["a", "b", "x", "y", "lb", "rb", "lt", "rt"]:
        path = os.path.join(src, name + ".raw")
        if not os.path.exists(path):
            print(f"{name}: no original, skipped")
            continue
        raw = open(path, "rb").read()
        _, _, img = decode(raw, SIZE, SIZE, DXT5, ENDIAN)
        new = (face_button(img, *FACE[name]) if name in FACE
               else shoulder_button(img, SHOULDER[name]))
        out = encode(new, raw, SIZE, SIZE, DXT5, ENDIAN)
        target = os.path.join(dst, f"{fnv1a64(raw):016X}.raw")
        with open(target, "wb") as f:
            f.write(out)
        _, _, check = decode(out, SIZE, SIZE, DXT5, ENDIAN)
        previews += [img, check]
        print(f"{name} -> {os.path.basename(target)}")
    if len(sys.argv) > 3 and previews:
        png(sys.argv[3], previews)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
