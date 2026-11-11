#!/usr/bin/env python3
"""gen_view.py - Block Breaker 3D's view tables for the Atari (src/view_tab.h).

The camera is fixed (port: the original leans it with the paddle) at
(0, 0, -320), looking down the tunnel, so everything that doesn't move is
worked out here:

- the tunnel's quads on the screen (drawn once at start-up),
- the bricks' edges on the screen (a front face is a rectangle),
- 1/z tables for what moves (the ball, the capsules, the ring, the shadow),
- the players' shapes: the ball and the capsules at each size, the paddle,
- each level's colours: the bricks' rows are bands of lines, and a DLI
  sets COLPF1/COLPF2 for each band, chosen here from the bricks in it.

Screen: ANTIC mode D, 160 x 88 lines (2 scan lines each), centre (80, 44).
The original: 320 x 256, focal length 270; here x is halved (135) and y
scaled for mode D's tall pixels (115 lines; 230 scan lines).
"""
import itertools
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "src" / "view_tab.h"

W, H, D, Z0, CD, CW, CH = 168, 120, 800, 620, 60, 56, 60
CAMZ = -320
FX, FY = 135.0, 115.0            # pixels, lines (scan lines: 2 * FY)
CX, CY = 80, 44
LINES = 88

PAL = (HERE / "atari_pal.bin").read_bytes()


def sx(x, z):
    return CX + x * FX / (z - CAMZ)


def sy(y, z):
    return CY - y * FY / (z - CAMZ)


# ------------------------------------------------------------ colours
BAK, PF0 = 0x50, 0x54            # the tunnel (dark, light); steel is PF0
OUT_PF1, OUT_PF2 = 0xBC, 0x00    # outside the bricks' bands: the ring (green), the shadow


def rgb(c):
    return tuple(PAL[c * 3:c * 3 + 3])


def amiga(v):
    return tuple(((v >> s) & 15) * 17 for s in (8, 4, 0))


def mixc(a, b):
    return tuple((x + y) / 2 for x, y in zip(a, b))


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b))


# the original's lit pens (red F33, orange F83, yellow FE4, green 5E4, cyan
# 3DF, blue 56F, tough CCD, bonus F4E) as Atari colours, picked by eye: the
# nearest by RGB are dull in the PAL palette
CLASS_ATARI = {1: 0x36, 2: 0x2A, 3: 0xEE, 4: 0xBA, 5: 0x9C, 6: 0x88, 7: 0x0E, 9: 0x5C}
CODE = {"BAK": 0, "PF0": 1, "PF1": 2, "PF2": 3}


def pat(a, b=None):
    """pattern bytes (even, odd line) of colour codes a/b in a checker"""
    b = a if b is None else b
    e = (a << 6) | (b << 4) | (a << 2) | b
    o = (b << 6) | (a << 4) | (b << 2) | a
    return e, o


PATTERNS = [("PF1", None), ("PF2", None), ("PF1", "PF2"), ("PF1", "BAK"), ("PF2", "BAK"), ("PF1", "PF0"),
            ("PF2", "PF0")]

ATARI = list(range(0, 256, 2))


def near(t, n):
    return sorted(ATARI, key=lambda c: dist(rgb(c), t))[:n]


def band_palette(classes, weight=None):
    """PF1, PF2 and a pattern for each class, closest to the original's colours
    (weight: how much each class matters, by how many bricks show it)"""
    weight = weight or {}
    if not classes:
        return OUT_PF1, OUT_PF2, {}
    targets = {k: rgb(CLASS_ATARI[k]) for k in classes}
    pool = sorted({(CLASS_ATARI[k] & 0xF0) | ((CLASS_ATARI[k] + d) & 0x0E) for k in classes for d in (-2, 0, 2)})
    best = None
    for p1, p2 in itertools.product(pool, pool):
        if p1 == p2 and len(classes) > 1:
            continue
        reg = {"BAK": rgb(BAK), "PF0": rgb(PF0), "PF1": rgb(p1), "PF2": rgb(p2)}
        look = [reg[a] if b is None else mixc(reg[a], reg[b]) for a, b in PATTERNS]
        for perm in itertools.permutations(range(len(PATTERNS)), len(classes)):
            # solid colours read better than mixes: a small penalty for a mix
            cost = sum((dist(look[i], targets[k]) + (0 if PATTERNS[i][1] is None else 900)) * weight.get(k, 1)
                       for i, k in zip(perm, classes))
            if best is None or cost < best[0]:
                best = (cost, p1, p2, dict(zip(classes, perm)))
    _, p1, p2, assign = best
    return p1, p2, {k: pat(CODE[PATTERNS[i][0]], CODE[PATTERNS[i][1] or PATTERNS[i][0]]) for k, i in assign.items()}


def levels():
    src = (HERE.parent / "src" / "bb_core.c").read_text()
    blk = src[src.index("levels[BB_LEVELS][12][7]"):]
    blk = blk[:blk.index("};")]
    import re
    rows = re.findall(r'"([^"]{6})"', blk)
    return [rows[i * 12:i * 12 + 12] for i in range(8)]


CH2K = {"r": 1, "o": 2, "y": 3, "g": 4, "c": 5, "b": 6, "T": 7, "S": 8, "*": 9}


def main():
    out = ["/* view_tab.h - made by tools/gen_view.py: do not edit */", "#include <stdint.h>", ""]

    # ---------------------------------------------------- bricks' edges
    zs = [Z0, Z0 + CD, Z0 + 2 * CD, D]
    sxe = [[round(sx(-W + c * CW, z)) for c in range(7)] for z in zs]
    sye = [[round(sy(-H + r * CH, z)) for r in range(5)] for z in zs]
    out.append("/* screen x of the bricks' column edges, y (line) of their row edges (row 0 lowest), at the\n"
               "   z of each layer's front and the back wall */")
    out.append("const uint8_t bk_sx[4][7] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in sxe) + "};")
    out.append("const uint8_t bk_sy[4][5] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in sye) + "};")
    bx0 = min(min(r) for r in sxe)
    bx1 = max(max(r) for r in sxe)
    by0 = min(min(r) for r in sye)
    by1 = max(max(r) for r in sye)
    BOX_Y0, BOX_Y1, BOX_B0, BOX_B1 = by0, by1 + 1, bx0 // 4, (bx1 + 3) // 4
    out.append(f"#define BOX_Y0 {BOX_Y0}\n#define BOX_Y1 {BOX_Y1}\n#define BOX_B0 {BOX_B0}\n#define BOX_B1 {BOX_B1}")

    # band lines: where each brick row's colours start (the front layer's top edge)
    band_top = [sye[0][r + 1] for r in range(4)]          # row r's first line
    out.append(f"/* the first line of each brick row's band (row 0 lowest); the bands end at line {sye[0][0] - 1} */")
    out.append("static const uint8_t band_top[4] = {" + ", ".join(map(str, band_top)) + "};")
    out.append(f"#define BAND_END {sye[0][0]}")

    # ---------------------------------------------------- the tunnel
    # A ray per pixel into the original's tunnel (stripes every 100 along it,
    # from z = -200). Every line is then the side walls' row (the same on every
    # line: a side wall pixel's depth depends only on x) with one run in the
    # middle of a single pattern (the floor or ceiling at that line's depth, or
    # the back wall), so the tunnel is two rows and three bytes a line.
    floor_p = [pat(1), pat(1, 0)]       # floor: PF0 / a PF0-BAK checker
    ceil_p = [pat(1, 0), pat(0)]        # ceiling: the checker / BAK
    side_p = [pat(1, 0), pat(1)]        # sides: the checker / PF0 (the other phase)
    back_p = pat(0)

    def surface(x, y):
        dx = (x + 0.5 - CX) / FX
        dy = (CY - (y + 0.5)) / FY
        zs = W / abs(dx) if dx else 1e9
        zf = H / abs(dy) if dy else 1e9
        zz = min(zs, zf)
        if zz >= D - CAMZ:
            return "back", back_p
        k = int((zz + CAMZ + 200) // 100) & 1
        if zs < zf:
            return "side", side_p[k]
        return "floor", (floor_p if dy < 0 else ceil_p)[k]

    def code(p, x, y):
        b = p[y & 1]
        return (b >> (6 - 2 * (x & 3))) & 3

    img = [[surface(x, y) for x in range(160)] for y in range(LINES)]
    # the side walls' row: from the line where they reach furthest into the middle
    tmpl = [[0] * 160, [0] * 160]
    for x in range(160):
        p = None
        for y in range(LINES):
            if img[y][x][0] == "side":
                p = img[y][x][1]
                break
        p = p or back_p
        for par in (0, 1):
            tmpl[par][x] = code(p, x, par)
    xl, xr, mp = [], [], []
    mids = []
    for y in range(LINES):
        xs = [x for x in range(160) if img[y][x][0] != "side"]
        a0, a1 = (min(xs), max(xs) + 1) if xs else (80, 80)
        kinds = {img[y][x][1] for x in range(a0, a1)}
        p = img[y][a0][1] if xs else back_p
        if len(kinds) > 1 and not (BOX_Y0 <= y < BOX_Y1):
            # the middle crosses the back wall's edge here: floor + back wall
            raise SystemExit(f"line {y}: more than one pattern in the middle")
        if len(kinds) > 1:
            p = back_p
        xl.append(a0)
        xr.append(a1)
        if p not in mids:
            mids.append(p)
        mp.append(mids.index(p))
        # check: the template + the middle is the picture (outside the bricks' box)
        for x in range(160):
            if BOX_Y0 <= y < BOX_Y1 and BOX_B0 * 4 <= x < BOX_B1 * 4 + 4:
                continue
            want = code(img[y][x][1], x, y)
            got = code(p, x, y) if a0 <= x < a1 else tmpl[y & 1][x]
            assert want == got, (x, y, img[y][x])

    def pack(row):
        return [(row[i] << 6) | (row[i + 1] << 4) | (row[i + 2] << 2) | row[i + 3] for i in range(0, 160, 4)]

    out.append("/* the tunnel: the side walls' row (even, odd lines), and on each line the middle run */")
    out.append("const uint8_t tun_row[2][40] = {" + ", ".join("{" + ", ".join(f"0x{v:02X}" for v in pack(r)) + "}" for r in tmpl) + "};")
    out.append(f"const uint8_t tun_xl[{LINES}] = {{" + ", ".join(map(str, xl)) + "};")
    out.append(f"const uint8_t tun_xr[{LINES}] = {{" + ", ".join(map(str, xr)) + "};")
    out.append(f"const uint8_t tun_mid[{LINES}] = {{" + ", ".join(map(str, mp)) + "};")
    out.append("const uint8_t tun_pats[][2] = {" + ", ".join(f"{{0x{e:02X}, 0x{o:02X}}}" for e, o in mids) + "};")
    polys = []

    # ---------------------------------------------------- 1/z
    # kx_tab/ky_tab are made at start-up (view.c): by (z + 320 - 256) / 4,
    # FX * 256 / (z + 320) pixels and 2 * FY * 256 / (z + 320) scan lines a unit
    out.append(f"#define KX_NUM {round(FX * 256)}u\n#define KY_NUM {round(2 * FY * 256)}u")

    # ---------------------------------------------------- the ball (a player)
    def ellipse(rx, ry, wide):
        """rows of a player: an ellipse rx pixels, ry scan lines (radius); wide: double width"""
        unit = 2 if wide else 1
        h = max(2, round(2 * ry))
        bits = 8
        rows = []
        for j in range(h):
            yy = (j + 0.5 - h / 2) / ry
            v = 0
            for b in range(bits):
                xx = ((b + 0.5) * unit - bits * unit / 2) / rx
                if xx * xx + yy * yy <= 1.0:
                    v |= 0x80 >> b
            rows.append(v)
        while rows and rows[0] == 0:
            rows.pop(0)
        while rows and rows[-1] == 0:
            rows.pop()
        return rows

    # sizes by (z + 320) / 32: 8..35
    ball_shapes, ball_idx, ball_meta = [], [], []
    cache = {}
    for zz32 in range(8, 36):
        zz = zz32 * 32 + 16
        rx, ry = 10 * FX / zz, 10 * 2 * FY / zz
        wide = rx > 4.3
        rows = ellipse(rx, ry, wide)
        key = (tuple(rows), wide)
        if key not in cache:
            cache[key] = len(ball_shapes)
            ball_shapes.append(rows)
            ball_meta.append((len(rows), 1 if wide else 0))
        ball_idx.append(cache[key])
    out.append("/* the ball: shape by (z + 320) / 32 - 8 */")
    out.append("static const uint8_t ball_of[28] = {" + ", ".join(map(str, ball_idx)) + "};")
    flat, offs = [], []
    for rows in ball_shapes:
        offs.append(len(flat))
        flat += rows
    out.append(f"static const uint8_t ball_rows[{len(flat)}] = {{" + ", ".join(f"0x{v:02X}" for v in flat) + "};")
    out.append("static const uint8_t ball_off[] = {" + ", ".join(map(str, offs)) + "};")
    out.append("static const uint8_t ball_h[] = {" + ", ".join(str(m[0]) for m in ball_meta) + "};")
    out.append("static const uint8_t ball_wide[] = {" + ", ".join(str(m[1]) for m in ball_meta) + "};")

    # ---------------------------------------------------- capsules (a player): 32 x 20 units, the letter cut out
    letters = {  # 3 x 5
        0: ["X.X", "X.X", "XXX", "XXX", "X.X"],   # W
        1: [".XX", "X..", ".X.", "..X", "XX."],   # S
        2: ["X..", "X..", "X..", "X..", "XXX"],   # L
        3: ["XX.", "X.X", "XX.", "X..", "X.."],   # P
    }
    cap_shapes, cap_idx, cap_meta = [], [], []
    for zz32 in range(8, 36):
        zz = zz32 * 32 + 16
        w, h = 32 * FX / zz, 20 * 2 * FY / zz
        wide = w > 8.5
        unit = 2 if wide else 1
        nb = max(1, min(8, round(w / unit)))
        nh = max(2, round(h))
        idx = []
        for t in range(4):
            rows = []
            for j in range(nh):
                rows.append(((0xFF00 >> nb) & 0xFF))
            if nb >= 5 and nh >= 10:   # the letter: 3 bits, 5 rows of 2 scan lines
                x0 = (nb - 3) // 2
                y0 = (nh - 10) // 2
                for ly, line in enumerate(letters[t]):
                    for lx, ch in enumerate(line):
                        if ch == "X":
                            for k in range(2):
                                rows[y0 + ly * 2 + k] &= ~(0x80 >> (x0 + lx)) & 0xFF
            key = (tuple(rows), wide)
            if key not in cache:
                cache[key] = ("c", len(cap_shapes))
                cap_shapes.append(rows)
                cap_meta.append((nh, 1 if wide else 0, nb * unit))
            idx.append(cache[key][1])
        cap_idx.append(idx)
    out.append("/* capsules: shape by (z + 320) / 32 - 8 and type */")
    out.append("static const uint8_t cap_of[28][4] = {" + ", ".join("{" + ", ".join(map(str, i)) + "}" for i in cap_idx) + "};")
    flat, offs = [], []
    for rows in cap_shapes:
        offs.append(len(flat))
        flat += rows
    out.append(f"static const uint8_t cap_rows[{len(flat)}] = {{" + ", ".join(f"0x{v:02X}" for v in flat) + "};")
    out.append("static const uint16_t cap_off[] = {" + ", ".join(map(str, offs)) + "};")
    out.append("static const uint8_t cap_h[] = {" + ", ".join(str(m[0]) for m in cap_meta) + "};")
    out.append("static const uint8_t cap_wide[] = {" + ", ".join(str(m[1]) for m in cap_meta) + "};")
    out.append("static const uint8_t cap_w[] = {" + ", ".join(str(m[2]) for m in cap_meta) + "};")

    # ---------------------------------------------------- the paddle (players 0 and 1, left and right halves)
    def paddle(hw, hh, unit, nbits):
        """hw pixels, hh scan lines (half sizes); each half nbits of unit pixels"""
        zz = -CAMZ
        t = round(5 * 2 * FY / zz)
        h = round(2 * hh)
        cr = round(6 * 2 * FY / zz)
        left, right = [], []
        for j in range(h):
            full = j < t or j >= h - t
            l = r = 0
            if full:
                l = (0xFF00 >> nbits) & 0xFF
                r = (0xFF00 >> nbits) & 0xFF
            else:
                l = 0x80                              # the outer edges
                r = 0x80 >> (nbits - 1)
            mid = h // 2
            if mid - 1 <= j <= mid:                   # the cross: across
                l |= 0x01 << (8 - nbits)
                r |= 0x80
            if abs(j + 0.5 - mid) <= cr:              # and up and down
                r |= 0x80
            left.append(l)
            right.append(r)
        return left, right

    zz = -CAMZ
    nl, nr = paddle(40 * FX / zz, 28 * 2 * FY / zz, 2, 8)
    wl, wr = paddle(62 * FX / zz, 42 * 2 * FY / zz, 4, 7)
    out.append(f"#define PAD_H {len(nl)}\n#define WIDE_H {len(wl)}")
    for name, rows in (("pad_l", nl), ("pad_r", nr), ("wide_l", wl), ("wide_r", wr)):
        out.append(f"static const uint8_t {name}[{len(rows)}] = {{" + ", ".join(f"0x{v:02X}" for v in rows) + "};")

    # ---------------------------------------------------- each level's band colours
    lv = levels()
    pf1s, pf2s, pats_l = [], [], []
    for L in range(8):
        p1r, p2r, pr = [], [], []
        for r in range(4):           # brick row, 0 lowest
            cls = set()
            weight = {}
            for c in range(6):
                front = True
                for l in range(3):
                    ch = lv[L][l * 4 + 3 - r][c]
                    if ch == ".":
                        continue
                    k = CH2K[ch]
                    cls.add(k)
                    # a brick seen from the start counts 4 times one behind another
                    weight[k] = weight.get(k, 0) + (4 if front else 1)
                    front = False
            cls.discard(8)
            cls = sorted(cls)
            p1, p2, assign = band_palette(cls, weight)
            p1r.append(p1)
            p2r.append(p2)
            row = []
            for k in range(10):
                if k == 8:
                    row.append(pat(1))                       # steel: PF0, the tunnel's light colour
                else:
                    row.append(assign.get(k, (0, 0)))
            pr.append(row)
        pf1s.append(p1r)
        pf2s.append(p2r)
        pats_l.append(pr)
    out.append(f"#define C_BAK 0x{BAK:02X}\n#define C_PF0 0x{PF0:02X}\n#define C_OUT1 0x{OUT_PF1:02X}\n#define C_OUT2 0x{OUT_PF2:02X}")
    out.append("/* [level][row]: COLPF1, COLPF2 on the band of brick row `row` */")
    out.append("static const uint8_t lv_pf1[8][4] = {" + ", ".join("{" + ", ".join(f"0x{c:02X}" for c in r) + "}" for r in pf1s) + "};")
    out.append("static const uint8_t lv_pf2[8][4] = {" + ", ".join("{" + ", ".join(f"0x{c:02X}" for c in r) + "}" for r in pf2s) + "};")
    out.append("/* [level][row][BK_*]: the pattern (even, odd line) */")
    out.append("static const uint8_t lv_pat[8][4][10][2] = {")
    for pr in pats_l:
        out.append("  {" + ", ".join("{" + ", ".join(f"{{0x{e:02X},0x{o:02X}}}" for e, o in row) + "}" for row in pr) + "},")
    out.append("};")

    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT}: box lines {by0}-{by1} px {bx0}-{bx1}; band tops {band_top}; "
          f" {len(ball_shapes)} ball, {len(cap_shapes)} capsule shapes")
    for L in range(8):
        print(L + 1, [f"{a:02X}/{b:02X}" for a, b in zip(pf1s[L], pf2s[L])])


if __name__ == "__main__":
    main()
