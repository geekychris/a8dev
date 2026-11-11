#!/usr/bin/env python3
"""Planet Chomp's characters and sprites for the Atari (src/gfx_data.c).

The maze is drawn in ANTIC mode 4 characters (4 x 8 pixels, 4 colours),
a cell as 3 x 3 of them (12 x 24 pixels: square on screen): four corners
(walls on their two outer sides, a post), four edges (a wall or not), the
middle (nothing, a crumb, a key, the nest): 28 characters and one for no
cell. Colour 1 the walls, 2 the crumbs, 3 a key and the nest.

The chomper (4 facings x 3 mouths) and the spooks (looking 4 ways; the
frightened and the eyes-only ones) are 8 x 16 pixels: the chomper is the
four missiles side by side (the fifth player), the spooks players 0-3.
The original draws its textures in sprites.c; these are drawn here at the
Atari's size after them.

    python3 projects/planet_chomp/tools/gen_gfx.py
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)

# A cell is 3 x 3 characters (12 x 24 pixels: square on screen): corners
# (walls on their two outer sides), edges (a wall on their outer side) and
# the middle (what's in the cell). Character numbers:
#   corner q (0 TL, 1 TR, 2 BL, 3 BR) with walls w (bit 1 the horizontal
#     side, bit 0 the vertical):      q * 4 + w          (0..15)
#   edge e (0 top, 1 right, 2 bottom, 3 left), wall bit: 16 + e * 2 + w  (16..23)
#   the middle, content c (0 none, 1 crumb, 2 key, 3 nest): 24 + c   (24..27)
#   no cell: 28
NONE_CHAR = 28


def blank():
    return [[0] * 4 for _ in range(8)]


def corner_char(q, w):
    px = blank()
    top, left = q < 2, q % 2 == 0
    hy, vx = (0 if top else 7), (0 if left else 3)
    if w & 2:
        for x in range(4):
            px[hy][x] = 1
    if w & 1:
        for y in range(8):
            px[y][vx] = 1
    px[hy][vx] = 1                               # the post
    return px


def edge_char(e, w):
    px = blank()
    if w:
        if e == 0:
            px[0] = [1, 1, 1, 1]
        elif e == 2:
            px[7] = [1, 1, 1, 1]
        else:
            for y in range(8):
                px[y][3 if e == 1 else 0] = 1
    return px


def middle_char(c):
    px = blank()
    if c == 1:                                   # a crumb
        for y in (3, 4):
            px[y][1] = px[y][2] = 2
    elif c == 2:                                 # a key: a gold diamond
        for y, xs in ((1, (1, 2)), (2, (0, 1, 2, 3)), (3, (0, 1, 2, 3)), (4, (1, 2)), (5, (1, 2)), (6, (1, 2))):
            for x in xs:
                px[y][x] = 3
    elif c == 3:                                 # the nest: a ring
        for y in range(1, 7):
            px[y][0] = px[y][3] = 3
        px[1] = [3, 3, 3, 3]
        px[6] = [3, 3, 3, 3]
    return px


def to_bytes(px):
    return [(r[0] << 6) | (r[1] << 4) | (r[2] << 2) | r[3] for r in px]


def font():
    chars = [[0] * 8 for _ in range(128)]
    for q in range(4):
        for w in range(4):
            chars[q * 4 + w] = to_bytes(corner_char(q, w))
    for e in range(4):
        for w in range(2):
            chars[16 + e * 2 + w] = to_bytes(edge_char(e, w))
    for c in range(4):
        chars[24 + c] = to_bytes(middle_char(c))
    chars[NONE_CHAR] = [0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00]
    return chars


def circle_rows(h=16, w=8, r=None):
    """a round shape 8 px wide, 16 lines tall (square on screen)"""
    rows = []
    for y in range(h):
        yy = (y + 0.5 - h / 2) / (h / 2)
        bits = 0
        for x in range(w):
            xx = (x + 0.5 - w / 2) / (w / 2)
            if xx * xx + yy * yy <= 1.0:
                bits |= 0x80 >> x
        rows.append(bits)
    return rows


def chomper(facing, mouth):
    """facing 0 up 1 right 2 down 3 left; mouth 0 shut .. 2 wide"""
    ang = [0, 25, 50][mouth]
    face = [270, 0, 90, 180][facing]             # degrees, screen (y down)
    rows = []
    for y in range(16):
        yy = (y + 0.5 - 8) / 8
        bits = 0
        for x in range(8):
            xx = (x + 0.5 - 4) / 4
            if xx * xx + yy * yy > 1.0:
                continue
            a = math.degrees(math.atan2(yy, xx)) % 360
            d = abs((a - face + 180) % 360 - 180)
            if mouth and d < ang:
                continue
            bits |= 0x80 >> x
        rows.append(bits)
    return rows


def ghost(look, eyes_only=False, scared=False):
    body = circle_rows()
    rows = []
    for y in range(16):
        b = body[y] if y < 8 else 0x7E if y < 14 else (0xDB if y == 14 else 0x92)
        if y >= 8 and y < 14:
            b = 0xFF if y > 9 else body[y] | 0x81
        rows.append(b)
    # the eyes: holes in the body (the playfield shows through: dark)
    ex = {0: (0, -1), 1: (1, 0), 2: (0, 1), 3: (-1, 0)}[look]
    eyes = [0] * 16
    for cx in (2, 5):
        for y in range(5, 8):
            x = cx + (ex[0] if y >= 6 else 0)
            yy = y + ex[1]
            if 0 <= x < 8:
                eyes[yy] |= 0x80 >> x
    if eyes_only:
        return eyes
    if scared:
        out = rows[:]
        for y in (6, 7):
            out[y] &= ~0x24 & 0xFF
        out[11] &= ~0x55 & 0xFF                  # a wavy mouth
        return out
    return [r & ~e & 0xFF for r, e in zip(rows, eyes)]


def missiles(rows):
    """an 8-pixel shape as the four missiles side by side: M0 the left pair"""
    out = []
    for b in rows:
        m = 0
        for n in range(4):
            pair = (b >> (6 - 2 * n)) & 3
            m |= pair << (2 * n)
        out.append(m)
    return out


def main():
    ch = font()
    src = ["/* made by tools/gen_gfx.py - do not edit */", '#include "gfx_data.h"', ""]
    src.append("const uint8_t pc_font[1024] = {")
    for i in range(128):
        src.append("    " + ", ".join(f"0x{v:02X}" for v in ch[i]) + ",")
    src.append("};")
    src.append("/* the chomper as the four missiles: facing (0 up 1 right 2 down 3 left) x mouth */")
    src.append("const uint8_t spr_chomp[4][3][16] = {")
    for f in range(4):
        src.append("    {")
        for m in range(3):
            src.append("        { " + ", ".join(f"0x{v:02X}" for v in missiles(chomper(f, m))) + " },")
        src.append("    },")
    src.append("};")
    src.append("const uint8_t spr_ghost[4][16] = {   /* looking up, right, down, left */")
    for lk in range(4):
        src.append("    { " + ", ".join(f"0x{v:02X}" for v in ghost(lk)) + " },")
    src.append("};")
    src.append("const uint8_t spr_scared[16] = { " + ", ".join(f"0x{v:02X}" for v in ghost(0, scared=True)) + " };")
    src.append("const uint8_t spr_eyes[4][16] = {")
    for lk in range(4):
        src.append("    { " + ", ".join(f"0x{v:02X}" for v in ghost(lk, eyes_only=True)) + " },")
    src.append("};")
    hdr = ["/* made by tools/gen_gfx.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
           "#include <stdint.h>", f"#define NONE_CHAR {NONE_CHAR}",
           "extern const uint8_t pc_font[1024];", "extern const uint8_t spr_chomp[4][3][16];",
           "extern const uint8_t spr_ghost[4][16], spr_scared[16], spr_eyes[4][16];", "#endif", ""]
    open(os.path.join(PROJ, "src", "gfx_data.c"), "w").write("\n".join(src) + "\n")
    open(os.path.join(PROJ, "src", "gfx_data.h"), "w").write("\n".join(hdr))


if __name__ == "__main__":
    main()
