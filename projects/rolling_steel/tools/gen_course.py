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
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atari_pal as ap  # noqa: E402
import course_pic  # noqa: E402
import rscourse  # noqa: E402
import rsview  # noqa: E402

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


def main():
    os.makedirs(OUT, exist_ok=True)
    for ci in range(NCOURSE):
        c, cam, cv, pix, cols = picture(ci)
        mp, fonts, row_font = tiles(pix)
        print(f"course {ci + 1} {c.name}: map {mp.shape[1]} x {mp.shape[0]}, {len(fonts)} fonts "
              f"({sum(len(f) for f in fonts)} characters)", flush=True)
        if "--preview" in sys.argv:
            Image.fromarray(render_back(mp, fonts, row_font, cols)).save(os.path.join(OUT, f"course{ci + 1}.png"))


if __name__ == "__main__":
    main()


# ---- the physics' grid ----
CELL = 2048                        # half a unit (FX = 4096): a cell's side
S_DECK, S_ROUGH, S_ICE, S_RAIL, S_BOUNCY = range(5)


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
            sub = wall[k0:k1 + 1, i0:i1 + 1]
            top = int(P[:, 1].max())
            sub[:] = np.maximum(sub, top)
    return {"x0": x0, "z0": z0, "nx": nx, "nz": nz, "h": h, "cls": cls, "wall": wall, "NONE": NONE}
