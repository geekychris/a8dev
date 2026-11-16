#!/usr/bin/env python3
"""Miner Meteor: tiles and sprites -> src/art.c.

Tiles are drawn here for ANTIC 5 (a character is 4 colour clocks x 8 lines
of data, each line shown twice): a tile is two characters, 8 x 8 here and
8 colour clocks x 16 scan lines on the screen, the original's 16 x 16.
. = background, a = COLPF0 (the walls: the cavern's colour), b = COLPF1
(belts, crumbling floors), c = COLPF2 (spikes, the closed exit), d = COLPF3
(inverse characters: keys and the open exit, whose colour cycles).

The miner and the guardians are taken from the original's tile sheet
(tools/tilesheet.png, 100-pixel cells), 8 x 16 pixels for the players
(single-line resolution, one scan line an original pixel).
Run: python3 tools/mkart.py
"""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent.parent

SOLID = ["aaaaaaa.", "aaaaaaa.", "aaaaaaa.", "........", "aaa.aaaa", "aaa.aaaa", "aaa.aaaa", "........"]
COLLAPSE = ["bbbbbbbb", "b.b..b.b", "bb.bb.bb", "b..bb..b", "bb.bb.bb", "b.b..b.b", "bbbbbbbb", "........"]
CONV = ["bbbbbbbb", "b.bbb.bb", "..bb..b.", ".bb..bb.", "..bb..b.", "b.bbb.bb", "bbbbbbbb", "........"]
HAZARD = ["........", "........", "...c...c", "..cc..cc", "..cc..cc", ".ccc.ccc", "cccccccc", "cccccccc"]
KEY = ["..dd....", ".d..d...", ".d..d...", "..dd....", "...d....", "...dd...", "...d....", "...dd..."]
EXIT_C = [".cccccc.", "cc....cc", "c......c", "c..cc..c", "c.c..c.c", "c..cc..c", "c......c", "cccccccc"]
EXIT_O = [".dddddd.", "dd....dd", "d......d", "d......d", "d......d", "d......d", "d......d", "dddddddd"]
V = {".": 0, "a": 1, "b": 2, "c": 3, "d": 3}


def chars(art):
    """8 x 8 art -> (left char bytes, right char bytes, inverse?)"""
    out = []
    for half in (0, 4):
        out.append([sum(V[r[half + i]] << (6 - 2 * i) for i in range(4)) for r in art])
    return out[0], out[1], any("d" in r for r in art)


def mirror_conv(art):
    return [r[::-1] for r in art]


def sink(art, k):
    """the crumbling floor k lines down (the top k lines gone)"""
    return ["........"] * k + art[:8 - k]


tiles = {"SOLID": SOLID, "HAZARD": HAZARD, "KEY": KEY, "EXIT_C": EXIT_C, "EXIT_O": EXIT_O,
         "CONV_L": CONV, "CONV_R": mirror_conv(CONV)}
for k in range(8):
    tiles[f"COLLAPSE{k}"] = sink(COLLAPSE, k)

# belts: 8 frames each, the pattern turned a pixel a frame (left belt left)
belt_frames = []
for d, art in (("L", CONV), ("R", mirror_conv(CONV))):
    for f in range(8):
        a = [r[f:] + r[:f] if d == "L" else r[-f:] + r[:-f] if f else r for r in art]
        l, r_, _ = chars(a)
        belt_frames.append(l + r_)

# sprites from the sheet
sheet = Image.open(HERE / "tools" / "tilesheet.png").convert("RGB")


def sprite(col, row):
    cell = sheet.crop((col * 100, row * 100, col * 100 + 100, row * 100 + 100))
    px = cell.load()
    pts = [(x, y) for y in range(100) for x in range(100) if sum(px[x, y]) > 120]
    x0, x1 = min(x for x, _ in pts), max(x for x, _ in pts)
    y0, y1 = min(y for _, y in pts), max(y for _, y in pts)
    cx, w = (x0 + x1) / 2, max(x1 - x0 + 1, (y1 - y0 + 1) * 0.5)
    rows = []
    for ty in range(16):
        v = 0
        for tx in range(8):
            xa, xb = int(cx - w / 2 + tx * w / 8), int(cx - w / 2 + (tx + 1) * w / 8)
            ya, yb = int(y0 + ty * (y1 - y0 + 1) / 16), int(y0 + (ty + 1) * (y1 - y0 + 1) / 16)
            n = on = 0
            for yy in range(ya, max(yb, ya + 1)):
                for xx in range(xa, max(xb, xa + 1)):
                    if 0 <= xx < 100 and 0 <= yy < 100:
                        n += 1
                        on += sum(px[xx, yy]) > 120
            if n and on * 2 >= n:
                v |= 0x80 >> tx
        rows.append(v)
    return rows


miner = [sprite(c, 2) for c in range(8)]           # walk cycle (8 frames), facing right
guards = [[sprite(c, r) for c in (0, 4)] for r in (5, 6, 7)]

out = ["/* made by tools/mkart.py - edit the art there */", "#include <stdint.h>", '#include "art.h"', ""]
names = list(tiles)
out.append(f"/* the tiles' characters: two each (left, right), from CH_TILES */")
out.append(f"const uint8_t tile_chars[{len(names)} * 16] = {{")
inv = []
for n in names:
    l, r, i = chars(tiles[n])
    inv.append(i)
    out.append("    " + ", ".join(f"0x{v:02X}" for v in l + r) + f",   /* {n} */")
out.append("};")
out.append(f"const uint8_t belt_frames[16 * 16] = {{")
for b in belt_frames:
    out.append("    " + ", ".join(f"0x{v:02X}" for v in b) + ",")
out.append("};")
out.append("const uint8_t miner_gfx[8 * 16] = {")
for m in miner:
    out.append("    " + ", ".join(f"0x{v:02X}" for v in m) + ",")
out.append("};")
out.append("const uint8_t guard_gfx[3 * 2 * 16] = {")
for g in guards:
    for f in g:
        out.append("    " + ", ".join(f"0x{v:02X}" for v in f) + ",")
out.append("};")
(HERE / "src" / "art.c").write_text("\n".join(out) + "\n")
hdr = ["/* made by tools/mkart.py */", "#ifndef ART_H", "#define ART_H", "#include <stdint.h>",
       "#define CH_TILES 64          /* the first tile character */"]
for k, n in enumerate(names):
    hdr.append(f"#define A_{n} {k}{'   /* inverse: COLPF3 */' if inv[k] else ''}")
hdr += [f"#define A_COUNT {len(names)}",
        "extern const uint8_t tile_chars[A_COUNT * 16];",
        "extern const uint8_t belt_frames[16 * 16];      /* 8 frames left belt, 8 right: 16 bytes (2 chars) */",
        "extern const uint8_t miner_gfx[8 * 16];          /* walk cycle, facing right */",
        "extern const uint8_t guard_gfx[3 * 2 * 16];      /* colour 4, 5, 6 x 2 frames */",
        "#endif"]
(HERE / "src" / "art.h").write_text("\n".join(hdr) + "\n")
print("ok", len(names), "tiles")
for m in miner[:1] + [guards[0][0]]:
    for v in m:
        print("".join("#" if v & (0x80 >> i) else "." for i in range(8)))
    print()
