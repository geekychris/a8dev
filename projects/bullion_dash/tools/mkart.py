#!/usr/bin/env python3
"""Bullion Dash: tiles and sprite frames -> src/art.c.

Tiles are ANTIC 4 characters, 4 colour clocks x 8 lines, 2 bits a pixel
(0 background, 1 COLPF0, 2 COLPF1, 3 COLPF2; an inverse character's 3s are
COLPF3): codes 64 + tile id in play, 74 + tile id in the editor (hidden
ladders, traps and the start markers show there). Codes 0..63 are the ROM
font's (the text lines use them).

Sprites (players and missile pairs) are 4 colour clocks wide: the ZX
Spectrum port's 8 x 8 frames (tools/make_art.py there) taken two pixels at
a time, in the high nibble of each byte; the left-facing copies are made at
start-up. Run: python3 tools/mkart.py
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent

T = {
    "EMPTY": ["0000"] * 8,
    "BRICK": ["1110", "1110", "1110", "0000", "1011", "1011", "1011", "0000"],
    "SOLID": ["2222", "2121", "2212", "2121", "2212", "2121", "2222", "0000"],
    "LADDER": ["3003", "3333", "3003", "3003", "3003", "3333", "3003", "3003"],
    "BAR": ["0000", "3333", "3333", "0000", "0000", "0000", "0000", "0000"],
    "GOLD": ["0000", "0000", "0330", "3223", "3333", "3333", "0000", "0000"],
    "MARK_P": ["3333", "3003", "3333", "3000", "3000", "3000", "3000", "3000"],
    "MARK_E": ["3333", "3000", "3000", "3330", "3000", "3000", "3333", "0000"],
    "HIDDEN_ED": ["3003", "0000", "3003", "0000", "3003", "0000", "3003", "0000"],
    "TRAP_ED": ["1110", "1110", "1110", "0000", "1010", "0101", "1010", "0000"],
}
PLAY = ["EMPTY", "BRICK", "SOLID", "LADDER", "BAR", "GOLD", "EMPTY", "MARK_P", "MARK_E", "BRICK"]
EDIT = ["EMPTY", "BRICK", "SOLID", "LADDER", "BAR", "GOLD", "HIDDEN_ED", "MARK_P", "MARK_E", "TRAP_ED"]
GOLD_INVERSE = True     # gold in COLPF3 (code + 128 on the screen)

FRAMES = {
    "STAND": ["...##...", "...##...", ".######.", "#.####.#",
              "..####..", "..#..#..", "..#..#..", ".##..##."],
    "RUN1":  ["....##..", "....##..", "..####.#", ".#.###..",
              "...###..", "..##.#..", ".#....#.", "#.....#."],
    "RUN2":  ["....##..", "....##..", "..#####.", "..####..",
              "...###..", "...##...", "...#.#..", "..#..#.."],
    "CLIMB1": ["#..##...", "#..##...", "######..", "..####.#",
               "..####.#", "..#..#..", ".##..#..", ".....##."],
    "CLIMB2": ["...##..#", "...##..#", "..######", "#.####..",
               "#.####..", "..#..#..", "..#..##.", ".##....."],
    "HANG":  ["#......#", "#..##..#", "#.####.#", ".######.",
              "..####..", "..####..", "..#..#..", "..#..#.."],
    "DIG":   ["...##...", "...##...", "..####..", ".#####..",
              "..######", "..#..#.#", "..#..#..", ".##..##."],
    "FALL":  ["#..##..#", ".#.##.#.", "..####..", "..####..",
              "..####..", ".#....#.", "#......#", "........"],
    "TRAPPED": ["...##...", "...##...", ".######.", "#.####.#",
                "........", "........", "........", "........"],
    "CURSOR": ["####", "#..#", "#..#", "#..#", "#..#", "#..#", "#..#", "####"],
}


def tile_bytes(art):
    return [sum(int(r[i]) << (6 - 2 * i) for i in range(4)) for r in art]


def halve(art):
    if len(art[0]) == 4:
        return [int(r.replace("#", "1").replace(".", "0"), 2) << 4 for r in art]
    out = []
    for r in art:
        v = 0
        for i in range(4):
            if "#" in r[2 * i:2 * i + 2]:
                v |= 0x80 >> i
        out.append(v)
    return out


lines = ["/* made by tools/mkart.py - edit the art there */", "#include <stdint.h>", '#include "art.h"', "",
         "/* characters 64..83: tile ids 0..9 in play, then in the editor */",
         "const uint8_t tile_chars[20 * 8] = {"]
for name in PLAY + EDIT:
    lines.append("    " + ", ".join(f"0x{v:02X}" for v in tile_bytes(T[name])) + f",   /* {name} */")
lines.append("};")
lines.append("/* sprite frames, facing right, 4 colour clocks in the high nibble: " + " ".join(FRAMES) + " */")
lines.append(f"const uint8_t frame_gfx[{len(FRAMES)} * 8] = {{")
for name, art in FRAMES.items():
    lines.append("    " + ", ".join(f"0x{v:02X}" for v in halve(art)) + f",   /* {name} */")
lines.append("};")
(HERE / "src" / "art.c").write_text("\n".join(lines) + "\n")
hdr = ["/* made by tools/mkart.py */", "#ifndef ART_H", "#define ART_H", "#include <stdint.h>",
       "#define CH_TILE 64              /* + tile id: in play */",
       "#define CH_EDIT 74              /* + tile id: in the editor */",
       "extern const uint8_t tile_chars[20 * 8];", f"extern const uint8_t frame_gfx[{len(FRAMES)} * 8];"]
for i, name in enumerate(FRAMES):
    hdr.append(f"#define FR_{name} {i}")
hdr.append("#endif")
(HERE / "src" / "art.h").write_text("\n".join(hdr) + "\n")
print("ok")
