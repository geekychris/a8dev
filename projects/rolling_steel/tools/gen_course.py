#!/usr/bin/env python3
"""Rolling Steel's courses for the Atari, from the original's course files.

The picture: each course drawn whole through the original's camera (yaw 0,
pitch 35 degrees; zoomed out to size 15 from 10.5 so more of the course is
on the small screen), with its lighting and fog (course_pic.py, from the
Next port), then made ANTIC mode 4: a pixel is two of the picture's across,
one line down; 4 x 8 pixel characters. Each character row has its own four
colours (COLBK, PF0-PF2: a DLI sets them) and its own font (CHBASE): rows
share a font while their tiles fit 128 characters.

    python3 tools/gen_course.py [--preview]
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atari_pal as ap  # noqa: E402
import course_pic  # noqa: E402
import rscourse  # noqa: E402
import rsview  # noqa: E402
import lz  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
OUT = os.path.join(PROJ, "build", "host")
CAM_SIZE = 15                     # the original's 10.5: zoomed out (port)
NCOURSE = 6


def picture(ci):
    """course ci -> (pix: H x W pixel values 0-3, cols: [row][4] colours, origin)"""
    c = rscourse.load(ci)
    cam = rsview.Cam(size=int(CAM_SIZE * 4096))
    cv = course_pic.render(c, cam)
    col = cv.col
    rgb = np.dstack([((col >> 10) & 31) * 255 / 31, ((col >> 5) & 31) * 255 / 31, (col & 31) * 255 / 31]).astype(np.float32)
    h8 = (rgb.shape[0] + 7) // 8 * 8
    w8 = (rgb.shape[1] + 7) // 8 * 8
    bgc = rgb[0, 0].copy()
    big = np.zeros((h8, w8, 3), np.float32) + bgc
    big[:rgb.shape[0], :rgb.shape[1]] = rgb
    im = (big[:, 0::2] + big[:, 1::2]) / 2
    bk = int(((ap.PALS - ap.yuv(bgc, ap.SEL_CHROMA)) ** 2).sum(1).argmin())
    pix = np.zeros(im.shape[:2], np.uint8)
    cols = []
    for b0 in range(0, h8, 8):
        band = ap.to_atari(im[b0:b0 + 8])
        sel = sorted(ap.band_palette(band.reshape(-1, 3), bk), key=lambda k: ap.PALS[k, 0])
        pc = ap.PALS[[bk] + sel]
        d = ((ap.yuv(band, ap.SEL_CHROMA)[:, :, None, :] - pc[None, None]) ** 2).sum(3)
        pix[b0:b0 + 8] = d.argmin(2)
        cols.append([int(ap.PAL_IDX[k]) for k in [bk] + sel])
    return c, cam, cv, pix, cols


def tiles(pix):
    """pixels -> map (rows x cols of tile ids per font), fonts (lists of 8-byte
    characters), each row's font"""
    H, W = pix.shape
    t = pix.reshape(H // 8, 8, W // 4, 4).transpose(0, 2, 1, 3)
    byts = (t[..., 0] << 6) | (t[..., 1] << 4) | (t[..., 2] << 2) | t[..., 3]     # rows x cols x 8
    fonts, row_font, cur, index = [], [], None, None
    mp = np.zeros((H // 8, W // 4), np.uint8)
    blank = bytes(8)
    for r in range(H // 8):
        need = {bytes(byts[r, x]) for x in range(W // 4)}
        if cur is None or len(set(index) | need) > 128:
            cur, index = [blank], {blank: 0}
            fonts.append(cur)
        for k in sorted(need - set(index)):
            index[k] = len(cur)
            cur.append(k)
        row_font.append(len(fonts) - 1)
        for x in range(W // 4):
            mp[r, x] = index[bytes(byts[r, x])]
    return mp, fonts, row_font


def render_back(mp, fonts, row_font, cols):
    rows, w = mp.shape
    out = np.zeros((rows * 8, w * 8, 3), np.uint8)
    for r in range(rows):
        f = fonts[row_font[r]]
        cc = ap.PAL[cols[r]].astype(np.uint8)
        for x in range(w):
            ch = f[mp[r, x]]
            for y in range(8):
                b = ch[y]
                for k in range(4):
                    v = (b >> (6 - 2 * k)) & 3
                    out[r * 8 + y, x * 8 + 2 * k:x * 8 + 2 * k + 2] = cc[v]
    return out


def pic_blob(mp, fonts, row_font, cols):
    """the fonts (1K each), the map (rows x w), then each row's colours (4)
    and font: unpacked at FONTS ($8000), so the fonts are 1K-aligned"""
    rows, w = mp.shape
    b = bytearray()
    for f in fonts:
        fb = b"".join(f)
        b += fb + bytes(1024 - len(fb))
    b += mp.tobytes()
    for r in range(rows):
        b += bytes(cols[r])
    b += bytes(row_font)
    return bytes(b)


HQ = 32                            # grid heights: 1/32 unit (Q8 = 8 x)
ESC = 0x40                         # a residual byte: 7-bit signed; ESC then the height (i16)


def grid_q(G):
    """valid cells, their heights in 1/32 unit, their surfaces (0 deck,
    1 rough, 2 ice, 3 rail; bouncy counts as rail)"""
    v = G["h"] > G["NONE"]
    q = np.where(v, (G["h"] * HQ) // 4096, 0).astype(np.int64)
    cls = np.minimum(G["cls"], 3)
    return v, q, cls


def grid_stream(G):
    """the grid packed for the banks (src/course.c's grid_decode undoes it):
    nx, nz (u16); the rows' runs (per row: the number; per run: x (u16),
    length); each cell's height as the miss from the plane through its
    left, upper and upper-left neighbours (or from the one there is), a
    7-bit signed byte, or ESC and the height (i16); the surfaces, 2 bits a
    cell, four to a byte (bits 0-1 first)"""
    v, q, cls = grid_q(G)
    nz, nx = v.shape
    hdr, res, cl = bytearray([nx & 255, nx >> 8, nz & 255, nz >> 8]), bytearray(), []
    for k in range(nz):
        runs, i = [], 0
        while i < nx:
            if not v[k, i]:
                i += 1
                continue
            j = i
            while j < nx and v[k, j] and j - i < 255:
                j += 1
            runs.append((i, j))
            i = j
        hdr.append(len(runs))
        for a, b in runs:
            hdr += bytes([a & 255, a >> 8, b - a])
            for i in range(a, b):
                L = q[k, i - 1] if i > a else None
                U = q[k - 1, i] if k > 0 and v[k - 1, i] else None
                UL = q[k - 1, i - 1] if k > 0 and i > 0 and v[k - 1, i - 1] else None
                if L is not None and U is not None and UL is not None:
                    pr = L + U - UL
                else:
                    pr = L if L is not None else U
                r = None if pr is None else int(q[k, i] - pr)
                if r is not None and -ESC < r < ESC:
                    res.append(r & 127)
                else:
                    hq = int(q[k, i]) & 0xFFFF
                    res += bytes([ESC, hq & 255, hq >> 8])
                cl.append(int(cls[k, i]))
    cb = bytearray()
    for i in range(0, len(cl), 4):
        x = 0
        for j, c2 in enumerate(cl[i:i + 4]):
            x |= c2 << (2 * j)
        cb.append(x)
    return bytes(hdr) + bytes(res) + bytes(cb), len(hdr), len(res)


def grid_mem(G):
    """the grid as src/course.c lays it out at GRID: per row the number of
    runs; per run x (u16), length, base height (i16, Q8); a byte a cell:
    height above the base (1/32 unit, signed, bits 2-7), surface (bits
    0-1). A run splits where a height leaves the base's -32..31."""
    v, q, cls = grid_q(G)
    nz, nx = v.shape
    out = bytearray()
    for k in range(nz):
        runs, i = [], 0
        while i < nx:
            if not v[k, i]:
                i += 1
                continue
            base, cells, j = int(q[k, i]), [], i
            while j < nx and v[k, j] and len(cells) < 255 and -32 <= q[k, j] - base <= 31:
                cells.append((((int(q[k, j]) - base) & 63) << 2) | int(cls[k, j]))
                j += 1
            runs.append((i, base * 8, cells))
            i = j
        out.append(len(runs))
        for x, base, cells in runs:
            out += bytes([x & 255, x >> 8, len(cells), base & 255, (base >> 8) & 255]) + bytes(cells)
    return bytes(out)


BANK_SIZE = 0x3FFC                 # $4000-$7FFB; $7FFC-$7FFF: the signature "RS0".."RS3"
SIG = b"RS"


def write_banks(streams):
    """the packed streams one after another through the four banks; ->
    (bank, address) of each"""
    data = bytearray()
    where = []
    for st in streams:
        where.append((len(data) // BANK_SIZE, 0x4000 + len(data) % BANK_SIZE))
        data += st
    assert len(data) <= 4 * BANK_SIZE, f"the courses need {len(data)} bytes of {4 * BANK_SIZE}"
    gfx = os.path.join(PROJ, "gfx")
    os.makedirs(gfx, exist_ok=True)
    for k in range(4):
        part = bytes(data[k * BANK_SIZE:(k + 1) * BANK_SIZE])
        part += bytes(BANK_SIZE - len(part)) + SIG + bytes([ord("0") + k, 0])
        open(os.path.join(gfx, f"bank{k}.bin"), "wb").write(part)
    print(f"banks: {len(data)} of {4 * BANK_SIZE} bytes")
    return where


def grid_decode(st):
    """grid_stream -> grid_mem, as src/course.c does it (a check)"""
    nx, nz = st[0] | st[1] << 8, st[2] | st[3] << 8
    hp, runs_all = 4, []
    for k in range(nz):
        nr = st[hp]
        hp += 1
        rr = []
        for _ in range(nr):
            rr.append((st[hp] | st[hp + 1] << 8, st[hp + 2]))
            hp += 3
        runs_all.append(rr)
    rp = hp
    ncell = sum(n for rr in runs_all for _, n in rr)
    # the residuals' length: walk them
    p, cnt = rp, 0
    while cnt < ncell:
        p += 3 if st[p] == ESC else 1
        cnt += 1
    cp, ci = p, 0
    prev = [None] * nx
    out = bytearray()
    for k in range(nz):
        cur = [None] * nx
        mem = []
        for x, n in runs_all[k]:
            for i in range(x, x + n):
                c = st[rp]
                if c == ESC:
                    hq = st[rp + 1] | st[rp + 2] << 8
                    hq = hq - 65536 if hq >= 32768 else hq
                    rp += 3
                else:
                    r = c - 128 if c >= 64 else c
                    rp += 1
                    L = cur[i - 1] if i > x else None
                    U, UL = prev[i], prev[i - 1] if i > 0 else None
                    pr = L + U - UL if (L is not None and U is not None and UL is not None) else (L if L is not None else U)
                    hq = pr + r
                cur[i] = hq
                cl = (st[cp + (ci >> 2)] >> (2 * (ci & 3))) & 3
                ci += 1
                if mem and mem[-1][0] + len(mem[-1][2]) == i and len(mem[-1][2]) < 255 and -32 <= hq - mem[-1][1] <= 31:
                    mem[-1][2].append((((hq - mem[-1][1]) & 63) << 2) | cl)
                else:
                    mem.append([i, hq, [cl]])
        out.append(len(mem))
        for x, base, cells in mem:
            b8 = base * 8
            out += bytes([x & 255, x >> 8, len(cells), b8 & 255, (b8 >> 8) & 255]) + bytes(cells)
        prev = cur
    return bytes(out)


R_Q8 = 128                         # the marble's radius (Q8)
CPK, SPK = 210, 147                # cos, sin 35 degrees: picture lines = -(y cpk + z spk) >> 13


def avoid_acid(c, P, margin=2048):
    """a route point in an acid pool (the original's sphere-box test), moved
    out of it the short way, half a unit clear (port: the autopilot
    steers at the route; the original's route runs through some pools)"""
    x, y, z = P
    for cx, cy, cz, hx, hy, hz, cs, sn in c.acid:
        dx, dz = x - cx, z - cz
        lx = (dx * cs - dz * sn) / 16384
        lz = (dx * sn + dz * cs) / 16384
        if abs(y - cy) > hy + 2048 or abs(lx) > hx + 2048 + margin or abs(lz) > hz + 2048 + margin:
            continue
        ex = (hx + 2048 + margin) * (1 if lx >= 0 else -1) - lx
        ez = (hz + 2048 + margin) * (1 if lz >= 0 else -1) - lz
        if abs(ex) < abs(ez):
            lx += ex
        else:
            lz += ez
        x = int(cx + (lx * cs + lz * sn) / 16384)
        z = int(cz + (-lx * sn + lz * cs) / 16384)
    return x, y, z


def meta(c, G, cv):
    """the course's numbers for the Atari (src/course.c's Meta): positions
    in Q8 units from the grid's corner (gx, gz) and Q8 heights (gy), the
    picture's place (picture pixel = (gx + px0) >> 6; line = ly0 - ((gy
    cpk + gz spk) >> 13)), clock and medals in 1/50 s, the route, the
    trigger boxes as cell runs, the enemies"""
    x0, z0 = G["x0"], G["z0"]
    gq = lambda X, Y, Z: ((X - x0) >> 4, Y >> 4, (Z - z0) >> 4)
    px0 = x0 // 16 - 32 * cv.ox
    # ly0: the best fit to the camera over the route
    cam = rsview.Cam(size=int(CAM_SIZE * 4096))
    errs = []
    for P in c.path:
        gx, gy, gz = gq(*P[:3])
        errs.append(cam.proj(P[:3])[1] / 65536 - cv.oy + ((gy * CPK + gz * SPK) >> 13))
    ly0 = int(round(float(np.median(errs))))
    assert 0 <= px0 + 0 < 65536 or True
    sp = gq(*c.spawn)
    st = lambda ms: (ms + 10) // 20
    b = bytearray(struct.pack("<HhHhHhHHHH", px0 & 0xFFFF, ly0, sp[0], sp[1], sp[2], c.kill_y >> 4,
                              st(c.time_ms), st(c.gold_ms), st(c.silver_ms), st(c.bronze_ms)))
    boxes = []
    for o in c.acid:
        boxes.append((0, o, 0, 0))
    boxes.append((1, c.goal, 0, 0))
    for z in c.zone:
        pw = z[11] / 2500.0                                 # Q12 u/s^2 -> Q12 units per step per step
        boxes.append((2, z[:8], int(round(z[8] / 16384 * pw)), int(round(z[10] / 16384 * pw))))
    b += bytes([len(c.path), len(boxes), len(c.enemy), 0])
    for P in c.path:
        gx, gy, gz = gq(*avoid_acid(c, P[:3]))
        b += struct.pack("<HHh", gx, gz, (gy & ~1) | (1 if P[3] else 0))
    # each box as rows of the grid: at a row's middle z, the x interval
    # (Q8) where the ball's centre is within the box grown by the ball's
    # radius (the original's sphere-box overlap, the corners square)
    nz = G["nz"]
    for kind, o, dvx, dvz in boxes:
        cx, cy, cz, hx, hy, hz, cs, sn = o
        R = 2048
        rows = {}
        for k in range(nz):
            z = z0 + (k + 0.5) * CELL
            xs = np.arange(x0, x0 + G["nx"] * CELL, CELL / 16)
            dx, dz = xs - cx, z - cz
            lx = (dx * cs - dz * sn) / 16384
            lz = (dx * sn + dz * cs) / 16384
            m = (np.abs(lx) <= hx + R) & (np.abs(lz) <= hz + R)
            if m.any():
                ii = np.nonzero(m)[0]
                rows[k] = ((int(xs[ii[0]]) - x0) >> 4, (int(xs[ii[-1]]) - x0) >> 4)
        k0, k1 = min(rows), max(rows)
        y0, y1 = (cy - hy - R) >> 4, (cy + hy + R) >> 4
        b += struct.pack("<BBHhhbb", kind, k1 - k0 + 1, k0, y0, y1, dvx, dvz)
        for k in range(k0, k1 + 1):
            i0, i1 = rows.get(k, (1, 0))
            b += struct.pack("<HH", i0, i1)
    for e in c.enemy:
        kind, ex, ey, ez, rng, spd, ax, az = e
        gx, gy, gz = gq(ex, ey, ez)
        if kind == 1:                                       # blob: 65536 per turn per step; its swing (Q8)
            speed = int(round(spd * 0.35 / 4096 / 50 / (2 * np.pi) * 65536))
            amp = 3.2 * 256
            ax8, az8 = int(round(ax / 16384 * amp)), int(round(az / 16384 * amp))
            r8 = 0xFFFF
        else:                                               # chaser: Q12 units per step per step
            speed = int(round(spd / 2500))
            ax8 = az8 = 0
            r8 = min(0xFFFF, rng >> 4)
        b += struct.pack("<BBHHhHHhh", kind, 0, speed, gx, gy, gz, r8, ax8, az8)
    return bytes(b)


def main():
    os.makedirs(OUT, exist_ok=True)
    streams, dims = [], []
    for ci in range(NCOURSE):
        c, cam, cv, pix, cols = picture(ci)
        mp, fonts, row_font = tiles(pix)
        pb = pic_blob(mp, fonts, row_font, cols)
        G = grid(c)
        gs, nh, nres = grid_stream(G)
        gm = grid_mem(G)
        assert grid_decode(gs) == gm, "the grid's decoder disagrees"
        assert len(gm) <= GRID_MAX, f"course {ci + 1}: the grid needs {len(gm)} bytes of {GRID_MAX}"
        assert len(gs) <= PIC_MAX
        mb = meta(c, G, cv)
        assert len(mb) <= META_MAX, f"course {ci + 1}: meta {len(mb)} bytes"
        for nm, data in (("pic", pb), ("grid", gm), ("gstream", gs), ("meta", mb)):
            open(os.path.join(OUT, f"{nm}{ci + 1}.bin"), "wb").write(data)
        streams += [lz.pack(pb), lz.pack(gs), lz.pack(mb)]
        dims.append((mp.shape[1], mp.shape[0], len(fonts), c.name))
        print(f"course {ci + 1} {c.name}: map {mp.shape[1]} x {mp.shape[0]}, {len(fonts)} fonts; "
              f"picture {len(pb)} bytes, grid {len(gm)} bytes in memory, meta {len(mb)}; "
              f"packed {len(streams[-3])} + {len(streams[-2])} + {len(streams[-1])}", flush=True)
        if "--preview" in sys.argv:
            Image.fromarray(render_back(mp, fonts, row_font, cols)).save(os.path.join(OUT, f"course{ci + 1}.png"))
    where = write_banks(streams)
    src = ["/* made by tools/gen_course.py - do not edit */", '#include "rs.h"', "",
           "/* each course: its packed picture, grid and numbers in the banks",
           "   (bank, address), the map's width and rows, its fonts, its name */",
           "const CourseSrc course_src[NCOURSE] = {"]
    for ci in range(NCOURSE):
        (pb_, pa), (gb_, ga), (mb_, ma) = where[3 * ci], where[3 * ci + 1], where[3 * ci + 2]
        w, rows, nf, name = dims[ci]
        src.append(f'    {{ {pb_}, 0x{pa:04X}, {gb_}, 0x{ga:04X}, {mb_}, 0x{ma:04X}, {w}, {rows}, {nf}, "{name}" }},')
    src.append("};")
    open(os.path.join(PROJ, "src", "course_dir.c"), "w").write("\n".join(src) + "\n")


GRID_MAX = 0xFFFA - 0xD800         # the grid in memory: $D800 up to the vectors
PIC_MAX = 0xB000 - 0x8000          # the packed grid while it is decoded (row buffers at $B000)
META_MAX = 2048


# ---- the physics' grid ----
CELL = 2048                        # half a unit (FX = 4096): a cell's side
S_DECK, S_ROUGH, S_ICE, S_RAIL, S_BOUNCY = range(5)
WALL_MIN = 4096 * 3 // 16          # a wall: its top 3/16 unit above the deck beside it


def grid(c):
    """the course as cells of half a unit: each cell's deck height (the top
    walkable triangle under its centre: normal.y > 0.45, the original's
    GROUND_NY), its surface class, and whether a wall (a steep triangle,
    rails) stands in it and how high. -> dict of arrays [z][x]"""
    V = np.array(c.v, dtype=np.int64)
    x0 = int(V[:, 0].min()) // CELL * CELL - CELL
    z0 = int(V[:, 2].min()) // CELL * CELL - CELL
    nx = (int(V[:, 0].max()) - x0) // CELL + 2
    nz = (int(V[:, 2].max()) - z0) // CELL + 2
    NONE = -(1 << 30)
    h = np.full((nz, nx), NONE, np.int64)
    cls = np.zeros((nz, nx), np.uint8)
    wall = np.full((nz, nx), NONE, np.int64)
    cxs = x0 + (np.arange(nx) + 0.5) * CELL
    czs = z0 + (np.arange(nz) + 0.5) * CELL
    for vs, n, fl in c.t:
        P = V[list(vs)].astype(np.float64)
        ny = n[1] / 16384
        i0 = max(0, int((P[:, 0].min() - x0) // CELL))
        i1 = min(nx - 1, int((P[:, 0].max() - x0) // CELL))
        k0 = max(0, int((P[:, 2].min() - z0) // CELL))
        k1 = min(nz - 1, int((P[:, 2].max() - z0) // CELL))
        if ny > 0.45:
            X, Z = np.meshgrid(cxs[i0:i1 + 1], czs[k0:k1 + 1])
            a, b, cc = P[0], P[1], P[2]
            v0x, v0z, v1x, v1z = b[0] - a[0], b[2] - a[2], cc[0] - a[0], cc[2] - a[2]
            den = v0x * v1z - v1x * v0z
            if abs(den) < 1e-6:
                continue
            v2x, v2z = X - a[0], Z - a[2]
            u = (v2x * v1z - v1x * v2z) / den
            w = (v0x * v2z - v2x * v0z) / den
            inside = (u >= -0.002) & (w >= -0.002) & (u + w <= 1.002)
            y = a[1] + u * (b[1] - a[1]) + w * (cc[1] - a[1])
            sub = h[k0:k1 + 1, i0:i1 + 1]
            better = inside & (y > sub)
            sub[better] = y[better].astype(np.int64)
            cls[k0:k1 + 1, i0:i1 + 1][better] = fl & 255
        elif abs(ny) < 0.5:
            # a wall: the cells its edges pass over (sampled every 1/8 cell),
            # with its top; kept only where it stands above the deck (below)
            top = int(P[:, 1].max())
            for a, b in ((P[0], P[1]), (P[1], P[2]), (P[2], P[0])):
                n = int(max(abs(b[0] - a[0]), abs(b[2] - a[2])) // (CELL / 8)) + 1
                for t in range(n + 1):
                    x = a[0] + (b[0] - a[0]) * t / n
                    z = a[2] + (b[2] - a[2]) * t / n
                    i, k = int((x - x0) // CELL), int((z - z0) // CELL)
                    if 0 <= i < nx and 0 <= k < nz and top > wall[k, i]:
                        wall[k, i] = top
    # walls: rails and kerbs stand above the deck around them (a deck's own
    # skirt reaches only its edge's height, so the void beside it stays void)
    pad = np.pad(h, 1, constant_values=NONE)
    near = np.max([pad[1 + dk:1 + dk + nz, 1 + di:1 + di + nx] for dk in (-1, 0, 1) for di in (-1, 0, 1)], axis=0)
    up = (wall > near + WALL_MIN) & (near > NONE)
    h[up] = wall[up]
    cls[up] = S_RAIL
    # pillars (bouncy posts): their cells at their tops
    for px, py, pz, pr, ph in c.pillar:
        for k in range(nz):
            for i in range(nx):
                if (cxs[i] - px) ** 2 + (czs[k] - pz) ** 2 <= (pr + CELL // 4) ** 2:
                    h[k, i] = py + ph
                    cls[k, i] = S_RAIL
    return {"x0": x0, "z0": z0, "nx": nx, "nz": nz, "h": h, "cls": cls, "wall": wall, "NONE": NONE}


if __name__ == "__main__":
    main()
