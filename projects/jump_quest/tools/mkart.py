#!/usr/bin/env python3
"""Jump Quest: tiles, items and sprites -> src/art.c.

Tiles and items are ANTIC 5 characters, two a tile (8 colour clocks x 8
lines of data, each shown twice: 16 scan lines, the original's 16 x 16).
. = the sky (background), a = COLPF0 (earth, brick), b = COLPF1 (grass,
bushes, pipes), c = COLPF2 (stone, clouds, poles), d = COLPF3 (inverse
characters: ? blocks, the flag, the items).
The hero (2 characters x 2 frames x 3 poses, 28 lines) and the enemies (4 x
2 frames, 16 lines) are the ZX Spectrum port's 16-pixel-wide 1-bit sprites
(made there from the original's drawing code), taken two pixels at a time
for an 8-colour-clock player. Run: python3 tools/mkart.py
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SPEC = Path.home() / "code/claude_world/spectrum_dev/projects/jump_quest/src/gfx_data.c"

TILES = [
    ["........"] * 8,                                                                       # empty
    ["aaaaaaaa", "aaaaaaaa", "aa.aaaaa", "aaaaaaaa", "aaaaa.aa", "aaaaaaaa", "a.aaaaaa", "aaaaaaaa"],  # ground
    ["bbbbbbbb", "bbbbbbbb", "ababbaba", "aaaaaaaa", "aaaaa.aa", "aaaaaaaa", "a.aaaaaa", "aaaaaaaa"],  # grass
    ["aaa.aaaa", "aaa.aaaa", "........", "aaaaaaa.", "aaaaaaa.", "........", "aaa.aaaa", "aaa.aaaa"],  # brick
    ["dddddddd", "d..dd..d", "d.d..d.d", "d...d..d", "d..d...d", "d......d", "d..d...d", "dddddddd"],  # ? block
    ["cccccccc", "c.cccc.c", "cccccccc", "cc.cc.cc", "cccccccc", "c.cccc.c", "cccccccc", "cccccccc"],  # stone
    ["bbbbbbbb", "bbbbbbbb", "bbcbbbbb", "bbcbbbbb", ".bbbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb"],  # pipe tl
    ["bbbbbbbb", "bbbbbbbb", "bbbbbbcb", "bbbbbbcb", "bbbbbbb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb."],  # pipe tr
    [".bcbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb", ".bcbbbbb"],  # pipe bl
    ["bbbbbcb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb.", "bbbbbcb."],  # pipe br
    ["aaaaaaaa", "a.a.a.a.", "........", "........", "........", "........", "........", "........"],  # platform
    ["........", "....cccc", "..cccccc", ".ccccccc", "cccccccc", "cccccccc", ".ccccccc", "........"],  # cloud l
    ["........", "cc......", "cccc....", "cccccc..", "ccccccc.", "cccccccc", "ccccccc.", "........"],  # cloud r
    ["cccccccc", "c......c", "c......c", "c......c", "c......c", "c......c", "c......c", "cccccccc"],  # ? block hit
    ["c.dddd..", "c.dddddd", "c.dddd..", "c.......", "c.......", "c.......", "c.......", "c......."],  # flag
    ["........", "........", "...bb...", "..bbbb..", ".bbbbbb.", "bbbbbbbb", "bbbbbbbb", "........"],  # bush
]
# items, in the order of the item kinds (ENT_BOARD.. ENT_PLANT, ENT_BRIEFCASE.. ENT_WINE)
ITEMS = [
    ["........", ".bbbbbb.", ".bdbbdb.", ".bbbbbb.", ".bdbbdb.", ".bbbbbb.", "..c..c..", "........"],  # board
    ["........", ".cccccc.", ".cddddc.", ".cddddc.", ".cccccc.", ".c.cc.c.", ".cccccc.", "........"],  # diskette
    ["........", "..dddd..", ".dddddd.", ".ddcddd.", ".dddddd.", ".dddddd.", "..dddd..", "........"],  # ball
    ["........", "........", ".cccccc.", "cccccccc", "cccccccc", ".cccccc.", "........", "........"],  # pillow
    ["...bb...", "..bbbb..", "...bb...", "..bbbb..", "...bb...", "..aaaa..", "..aaaa..", "........"],  # plant
    ["........", "..aaaa..", "aaaaaaaa", "aaddaaaa", "aaaaaaaa", "aaaaaaaa", "aaaaaaaa", "........"],  # briefcase
    ["........", ".dd.dd..", "ddddddd.", "ddddddd.", ".ddddd..", "..ddd...", "...d....", "........"],  # heart
    ["........", ".cccccc.", "ccdcdccc", "cccdcccc", "ccdcdccc", ".cccccc.", "........", "........"],  # pillow zz
    ["..cccc..", "..c..c..", ".dddddd.", ".dddddd.", ".dddddd.", ".dddddd.", ".cccccc.", "........"],  # beer
    ["...cc...", "...cc...", "..cccc..", ".caaaac.", ".caaaac.", "..cccc..", "...cc...", "..cccc.."],  # wine
]
V = {".": 0, "a": 1, "b": 2, "c": 3, "d": 3}


def chars(art):
    l = [sum(V[r[i]] << (6 - 2 * i) for i in range(4)) for r in art]
    r_ = [sum(V[r[4 + i]] << (6 - 2 * i) for i in range(4)) for r in art]
    return l + r_, any("d" in r for r in art)


src = SPEC.read_text()


def table(name):
    body = src[src.index(name):]
    body = body[body.index("{") + 1:body.index("};")]
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]


def halve16(rows2):
    """16-pixel 1-bit rows (2 bytes) -> 8 pixels (pairs ORed)"""
    out = []
    for i in range(0, len(rows2), 2):
        w = rows2[i] << 8 | rows2[i + 1]
        v = 0
        for k in range(8):
            if (w >> (14 - 2 * k)) & 3:
                v |= 0x80 >> k
        out.append(v)
    return out


player = table("gfx_player[")       # 2 chars x 2 frames x 3 poses x 28 rows x 2 bytes
enemy = table("gfx_enemy[")         # 4 types x 2 frames x 16 rows x 2 bytes
hero = [halve16(player[k * 56:(k + 1) * 56]) for k in range(len(player) // 56)]
foes = [halve16(enemy[k * 32:(k + 1) * 32]) for k in range(len(enemy) // 32)]

out = ["/* made by tools/mkart.py - edit the art there */", "#include <stdint.h>", '#include "art.h"', ""]
inv = []
out.append(f"/* tiles 0..15 then items 0..9: two characters each (left, right) */")
out.append(f"const uint8_t tile_chars[{(len(TILES) + len(ITEMS)) * 16}] = {{")
for art in TILES + ITEMS:
    b, i = chars(art)
    inv.append(1 if i else 0)
    out.append("    " + ", ".join(f"0x{v:02X}" for v in b) + ",")
out.append("};")
out.append(f"const uint8_t tile_inverse[{len(inv)}] = {{ " + ", ".join(map(str, inv)) + " };")
out.append(f"const uint8_t hero_gfx[{len(hero)} * 28] = {{")
for h in hero:
    out.append("    " + ", ".join(f"0x{v:02X}" for v in h) + ",")
out.append("};")
out.append(f"const uint8_t foe_gfx[{len(foes)} * 16] = {{")
for f in foes:
    out.append("    " + ", ".join(f"0x{v:02X}" for v in f) + ",")
out.append("};")
(HERE / "src" / "art.c").write_text("\n".join(out) + "\n")
(HERE / "src" / "art.h").write_text(f"""/* made by tools/mkart.py */
#ifndef ART_H
#define ART_H
#include <stdint.h>
#define CH_TILES 64             /* tile t: CH_TILES + 2t, item k: CH_TILES + 2 * (16 + k) */
#define ART_ITEMS 16
extern const uint8_t tile_chars[{(len(TILES) + len(ITEMS)) * 16}];
extern const uint8_t tile_inverse[{len(inv)}];
extern const uint8_t hero_gfx[{len(hero)} * 28];   /* [character][frame][pose] as the Spectrum's gfx_player */
extern const uint8_t foe_gfx[{len(foes)} * 16];    /* [type][frame] */
#endif
""")
print(len(hero), "hero frames,", len(foes), "enemy frames")
for v in hero[0]:
    print("".join("#" if v & (0x80 >> i) else "." for i in range(8)))
