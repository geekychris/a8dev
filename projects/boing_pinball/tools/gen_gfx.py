#!/usr/bin/env python3
"""Boing Pinball's table for the Atari: the picture, cut into character
tiles for ANTIC mode 4 (4 x 8 pixels, 4 colours), with a palette and a
font per character row, and the flippers' frames.

The drawing is the Spectrum port's (tools/gen_gfx.py there: the original's
render.c pens, its walls, posts, bumpers, ramps, lamps and decoration in
its oblique view), here at the Atari's scale: 320 hires pixels across
(mode 4's 160 pixels, each two wide), the y axis foreshortened more so
about half the table shows at once (the screen scrolls with the ball).

From the table's geometry (tools/table_0.json, made by the original's
pb_tables.c: the Spectrum port's tools/export_table.c) it writes
  gfx/fonts.bin     the fonts (1K each), gfx/map.bin the character map,
  gfx/flips.bin     the flippers' frames (their cells' characters),
  src/gfx_data.c/.h the rows' colours and fonts, the lamps' cells, the
                    drop targets' cells, the flippers' cells, the view.

    python3 projects/boing_pinball/tools/gen_gfx.py
"""
import json
import math
import os
import sys
from collections import Counter

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)

S = 1.0
KX, KY, KZ = 0.61, 0.33, 0.25      # hires pixels a mm across, up and in height (port: more foreshortened)
OX = 3
W = 320                             # hires pixels: mode 4's 160
YTOP = 1080                         # the table's top (mm) at row 0
H = 8 * 48                          # 48 character rows
OY = YTOP * KY                      # the screen row of y = 0 at height 0
FONT = [[32, 32, 32, 32, 32, 0, 32], [80, 80, 0, 0, 0, 0, 0], [80, 248, 80, 80, 248, 80, 0], [32, 120, 160, 112, 40, 240, 32], [200, 200, 16, 32, 64, 152, 152], [64, 160, 160, 64, 168, 144, 104], [32, 32, 0, 0, 0, 0, 0], [16, 32, 64, 64, 64, 32, 16], [64, 32, 16, 16, 16, 32, 64], [0, 80, 32, 248, 32, 80, 0], [0, 32, 32, 248, 32, 32, 0], [0, 0, 0, 0, 0, 32, 64], [0, 0, 0, 248, 0, 0, 0], [0, 0, 0, 0, 0, 0, 32], [8, 8, 16, 32, 64, 128, 128], [112, 136, 152, 168, 200, 136, 112], [32, 96, 32, 32, 32, 32, 112], [112, 136, 8, 16, 32, 64, 248], [112, 136, 8, 48, 8, 136, 112], [16, 48, 80, 144, 248, 16, 16], [248, 128, 240, 8, 8, 136, 112], [48, 64, 128, 240, 136, 136, 112], [248, 8, 16, 32, 64, 64, 64], [112, 136, 136, 112, 136, 136, 112], [112, 136, 136, 120, 8, 16, 96], [0, 0, 32, 0, 0, 32, 0], [0, 0, 32, 0, 0, 32, 64], [8, 16, 32, 64, 32, 16, 8], [0, 0, 248, 0, 248, 0, 0], [128, 64, 32, 16, 32, 64, 128], [112, 136, 8, 16, 32, 0, 32], [112, 136, 184, 184, 128, 136, 112], [112, 136, 136, 248, 136, 136, 136], [240, 136, 136, 240, 136, 136, 240], [112, 136, 128, 128, 128, 136, 112], [240, 136, 136, 136, 136, 136, 240], [248, 128, 128, 240, 128, 128, 248], [248, 128, 128, 240, 128, 128, 128], [112, 136, 128, 184, 136, 136, 112], [136, 136, 136, 248, 136, 136, 136], [112, 32, 32, 32, 32, 32, 112], [56, 16, 16, 16, 16, 144, 96], [136, 144, 160, 192, 160, 144, 136], [128, 128, 128, 128, 128, 128, 248], [136, 216, 168, 136, 136, 136, 136], [136, 200, 168, 152, 136, 136, 136], [112, 136, 136, 136, 136, 136, 112], [240, 136, 136, 240, 128, 128, 128], [112, 136, 136, 136, 168, 144, 104], [240, 136, 136, 240, 160, 144, 136], [112, 136, 128, 112, 8, 136, 112], [248, 32, 32, 32, 32, 32, 32], [136, 136, 136, 136, 136, 136, 112], [136, 136, 136, 136, 80, 80, 32], [136, 136, 136, 136, 168, 216, 136], [136, 136, 80, 32, 80, 136, 136], [136, 136, 80, 32, 32, 32, 32], [248, 8, 16, 32, 64, 128, 248]]

def px(x):
    return OX + x * KX


def py(y, z=0):
    return OY - y * KY - z * KZ


# render.c's pens and palettes (12-bit Amiga RGB)
(P_BLACK, P_FIELD1, P_FIELD2, P_ART1, P_ART2, P_ART3, P_METAL_D, P_METAL_L,
 P_CAB_D, P_CAB_L, P_RUBBER, P_RUBBER_D, P_RED, P_RED_D, P_FLASH, P_SHADOW,
 P_BLUE, P_BLUE_D, P_YEL, P_YEL_D, P_LRED, P_LRED_D, P_GREEN, P_MAG,
 P_ORANGE, P_DIM, P_DMD_BG, P_DMD_ON, P_DMD_MID, P_GLASS, P_WOOD, P_TEXT) = range(32)
COMMON = [0x000, 0x213, 0x324, 0x436, 0x547, 0xA86, 0x667, 0xCCD,
          0x413, 0x736, 0xFFE, 0x999, 0xE22, 0x811, 0xFF8, 0x101,
          0x5BF, 0x235, 0xFD4, 0x542, 0xF54, 0x522, 0x6F9, 0xF4F,
          0xFB2, 0x443, 0x210, 0xFA2, 0x741, 0x8CF, 0x631, 0xFFF]
THEME = [[0x213, 0x324, 0x436, 0x547, 0xA86, 0x413, 0x736, 0x102, 0x631],
         [0x024, 0x035, 0x047, 0x0A9, 0xEC8, 0x134, 0x368, 0x012, 0x234],
         [0x322, 0x433, 0x544, 0x765, 0xDA2, 0x333, 0x777, 0x111, 0x421]]
LAMP_ON = [P_BLUE, P_YEL, P_LRED, P_GREEN, P_MAG, P_ORANGE]
LAMP_OFF = [P_BLUE_D, P_YEL_D, P_LRED_D, P_DIM, P_DIM, P_DIM]
SK_WALL, SK_OUTER, SK_SLING, SK_GATE, SK_FLOOR, SK_DROP, SK_GUIDE = range(7)
SN_SPIN = 3


class Canvas:
    def __init__(self, w, h, bg):
        self.im = Image.new("P", (w, h), bg)
        self.d = ImageDraw.Draw(self.im)

    def poly(self, pts, col):
        self.d.polygon([(round(x), round(y)) for x, y in pts], fill=col)

    def ellipse(self, cx, cy, rx, ry, col):
        rx, ry = max(rx, 0.5), max(ry, 0.5)
        self.d.ellipse([round(cx - rx), round(cy - ry), round(cx + rx), round(cy + ry)], fill=col)

    def line(self, x0, y0, x1, y1, col, w=1):
        self.d.line([(round(x0), round(y0)), (round(x1), round(y1))], fill=col, width=w)

    def rect(self, x0, y0, x1, y1, col):
        self.d.rectangle([round(x0), round(y0), round(x1), round(y1)], fill=col)

    def dither_poly(self, pts, col):
        mask = Image.new("1", self.im.size, 0)
        ImageDraw.Draw(mask).polygon([(round(x), round(y)) for x, y in pts], fill=1)
        mp, ip = mask.load(), self.im.load()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys)) - 1), min(self.im.height, int(max(ys)) + 2)):
            for x in range(max(0, int(min(xs)) - 1), min(self.im.width, int(max(xs)) + 2)):
                if mp[x, y] and (x + y) & 1:
                    ip[x, y] = col

    def text(self, x, y, s, col, scale=1):
        for ch in s:
            o = ord(ch) - 33
            if 0 <= o < len(FONT):
                for r, bits in enumerate(FONT[o]):
                    for c in range(5):
                        if bits & (0x80 >> c):
                            self.rect(x + c * scale, y + r * scale, x + c * scale + scale - 1, y + r * scale + scale - 1, col)
            x += 6 * scale


def text_w(s, scale=1):
    return len(s) * 6 * scale - scale


def wall3d(cv, ax, ay, bx, by, r, h, side, top):
    ex, ey = bx - ax, by - ay
    ln = math.hypot(ex, ey)
    nx, ny = (r, 0) if ln < 0.25 else (-ey / ln * r, ex / ln * r)
    c = [(ax + nx, ay + ny), (ax - nx, ay - ny), (bx - nx, by - ny), (bx + nx, by + ny)]
    for k in range(4):                    # the sides: each face from the floor to the top
        (x0, y0), (x1, y1) = c[k], c[(k + 1) % 4]
        cv.poly([(px(x0), py(y0)), (px(x1), py(y1)), (px(x1), py(y1, h)), (px(x0), py(y0, h))], side)
    cv.ellipse(px(ax), py(ay), r * KX, r * KY, side)
    cv.ellipse(px(bx), py(by), r * KX, r * KY, side)
    cv.rect(px(ax) - r * KX, py(ay, h), px(ax) + r * KX, py(ay), side)
    cv.rect(px(bx) - r * KX, py(by, h), px(bx) + r * KX, py(by), side)
    cv.poly([(px(x), py(y, h)) for x, y in c], top)
    cv.ellipse(px(ax), py(ay, h), r * KX, r * KY, top)
    cv.ellipse(px(bx), py(by, h), r * KX, r * KY, top)


def cylinder(cv, x, y, r, h, side, top):
    cx, y0, y1, rx, ry = px(x), py(y), py(y, h), r * KX, r * KY
    cv.ellipse(cx, y0, rx, ry, side)
    cv.rect(cx - rx, y1, cx + rx, y0, side)
    cv.ellipse(cx, y1, rx, ry, top)


def field_edges(y):
    if y > 780:
        d = y - 780
        w = math.sqrt(260 * 260 - d * d) if d < 260 else 0
        return 260 - w, 260 + w
    xl = 20 if y <= 380 else (20 - (y - 380) * 20 / 90 if y <= 470 else 0)
    return xl, 520


def ramp_curve(pts):
    out = []
    n = len(pts)
    for i in range(n - 1):
        for k in range(4):
            t = k / 4
            p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, n - 1)]
            out.append([0.5 * (2 * p1[c] + (-p0[c] + p2[c]) * t + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t * t +
                               (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t ** 3) for c in range(3)])
    out.append(list(pts[-1]))
    return out


def draw_table(d, theme):
    cv = Canvas(W, H, P_WOOD)
    # the playfield
    for row in range(H):
        ymm = (OY - row) / KY
        if ymm < -60 or ymm > 1040:
            continue
        xl, xr = field_edges(ymm)
        if xl < xr:
            cv.line(px(xl), row, px(xr), row, P_FIELD1)
            if 380 < ymm < 760 and row & 1:
                cv.line(px(xl + 30), row, px(xr - 30 if xr < 470 else 440), row, P_FIELD2)
    # the theme's art (render.c theme_art: its LCG, its positions)
    r = 12345 + theme
    def rnd():
        nonlocal r
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        return (r >> 16) & 0x7FFF
    for i in range(160):
        x, y = rnd() % 500 + 10, rnd() % 1060 - 40
        xl, xr = field_edges(y)
        if x < xl + 6 or x > (xr - 6 if xr < 476 else 476):
            continue
        if theme == 0:
            cv.rect(px(x), py(y), px(x), py(y), P_ART3 if i & 3 else P_TEXT)
        elif theme == 1:
            if i & 1:
                cv.ellipse(px(x), py(y), 0.6, 0.6, P_ART2)
        elif not i & 3:
            cv.rect(px(x), py(y), px(x) + 1, py(y), P_ART2)
    for kind, col, x, y, a, b, text, dpx, dpy in d["deco"]:
        if kind == 0:
            cv.ellipse(px(x), py(y), a * KX, b * KY, col)
        elif kind == 1:
            for i in range(48):
                a0, a1 = i * 2 * math.pi / 48, (i + 1) * 2 * math.pi / 48
                cv.line(px(x) + a * KX * math.cos(a0), py(y) + b * KY * math.sin(a0),
                        px(x) + a * KX * math.cos(a1), py(y) + b * KY * math.sin(a1), col, 1)
        elif kind == 2:
            cv.poly([(px(dpx[i]), py(dpy[i])) for i in range(4)], col)
        elif kind == 4 and text:
            cv.text(round(px(239) - text_w(text) / 2) + 1, round(py(y)) - 3, text, P_ART2)
            cv.text(round(px(239) - text_w(text) / 2), round(py(y)) - 4, text, col)
    # the lamps, unlit: each its own palette entry
    lamps = []
    for i, (x, y, r_, col) in enumerate(d["lamp"]):
        if not r_:
            continue
        cv.ellipse(px(x), py(y), r_ * KX + 0.7, r_ * KY + 0.7, P_BLACK)
        cv.ellipse(px(x), py(y), max(r_ * KX, 1.0), max(r_ * KY, 0.8), I_LAMP + i)
        lamps.append((i, col))
    # the saucer
    sx, sy, sr = d["saucer"]
    cv.ellipse(px(sx), py(sy), (sr + 4) * KX, (sr + 4) * KY, P_METAL_L)
    cv.ellipse(px(sx), py(sy), sr * KX, sr * KY, P_BLACK)
    # the drop targets: the slot, and the target in its own palette entry
    # (standing: its colour; down: the slot's)
    for i, si in enumerate(d["drop_seg"]):
        ax, ay, bx, by = d["seg"][si][:4]
        cv.line(px(ax), py(ay), px(bx), py(by), P_BLACK, 2)
    # walls, posts and bumpers, far to near
    items = []
    for i, s in enumerate(d["seg"]):
        if s[6] in (SK_DROP, SK_GATE):
            continue
        items.append((30000 if s[6] == SK_OUTER else min(s[1], s[3]), 0, i))
    for i, c in enumerate(d["circ"]):
        items.append((c[1], 1, i))
    items.sort(key=lambda t: -t[0])
    for _, kind, i in items:
        if kind == 0:
            ax, ay, bx, by, r_, e, k, sid, owx, owy, bank, h, up = d["seg"][i]
            side = P_CAB_D if k == SK_OUTER else P_RUBBER_D if k == SK_SLING else P_METAL_D
            top = P_CAB_L if k == SK_OUTER else (I_SLING + (sid & 1)) if k == SK_SLING else P_METAL_L
            wall3d(cv, ax, ay, bx, by, r_, h, side, top)
        else:
            cx, cy, r_, e, bumper, cid, h = d["circ"][i]
            if bumper:
                cv.ellipse(px(cx), py(cy), (r_ + 3) * KX, (r_ + 3) * KY, P_RUBBER)
                cylinder(cv, cx, cy, r_ - 2, h - 8, P_METAL_D, P_RUBBER_D)
                cylinder(cv, cx, cy + 1, r_ * 0.9, h, P_RED_D, I_BUMPER + i)
                cv.ellipse(px(cx) - 1.5, py(cy, h) - 0.5, r_ * KX / 3, r_ * KY / 3, P_LRED)
            else:
                cylinder(cv, cx, cy, r_, h, P_RUBBER_D, P_RUBBER)
    # the drop targets, standing (their palette entry hides them when down)
    for i, si in enumerate(d["drop_seg"]):
        ax, ay, bx, by = d["seg"][si][:4]
        wall3d(cv, ax, ay, bx, by, 3, 26, I_DROP + i, I_DROP + i)
    # the gates: a wire on two posts
    for s in d["seg"]:
        if s[6] != SK_GATE:
            continue
        ax, ay, bx, by, h = s[0], s[1], s[2], s[3], s[11]
        cv.line(px(ax), py(ay, h), px(bx), py(by, h), P_METAL_L)
        cv.line(px(ax), py(ay), px(ax), py(ay, h), P_METAL_D)
        cv.line(px(bx), py(by), px(bx), py(by, h), P_METAL_D)
    # the spinners: brackets and a blade in its own palette entry
    for ax, ay, bx, by, kind, idx in d["sens"]:
        if kind != SN_SPIN:
            continue
        cv.line(px(ax) + 1, py(ay), px(ax) + 1, py(ay, 30), P_METAL_D, 1)
        cv.line(px(bx) - 1, py(by), px(bx) - 1, py(by, 30), P_METAL_D, 1)
        cv.rect(px(ax) + 2, py(ay, 30), px(bx) - 2, py(ay, 10), I_SPIN + idx)
        cv.line(px(ax) + 1, py(ay, 30), px(bx) - 1, py(ay, 30), P_METAL_L)
    # the apron over the drain
    cv.poly([(px(20), py(22, 20)), (px(474), py(22, 20)), (px(474), py(-60)), (px(20), py(-60))], P_CAB_D)
    cv.line(px(20), py(22, 20), px(474), py(22, 20), P_CAB_L, 1)
    t = "BOING PINBALL"
    cv.text(round(px(239) - text_w(t) / 2), round(py(-20, 20)), t, P_ART3)
    # the ramps, over everything
    for rp in d["ramp"]:
        pts = ramp_curve(rp["pts"])
        half, rail = 22, 14
        for i in range(4, len(pts) - 2, 6):
            if pts[i][2] > 12:
                cv.line(px(pts[i][0]), py(pts[i][1]), px(pts[i][0]), py(pts[i][1], pts[i][2]), P_METAL_D, 1)
        for i in range(len(pts) - 1):
            (x0, y0, z0), (x1, y1, z1) = pts[i], pts[i + 1]
            ln = math.hypot(x1 - x0, y1 - y0)
            if not ln:
                continue
            nx, ny = -(y1 - y0) * half / ln, (x1 - x0) * half / ln
            cv.dither_poly([(px(x0 + nx), py(y0 + ny, z0)), (px(x1 + nx), py(y1 + ny, z1)),
                            (px(x1 - nx), py(y1 - ny, z1)), (px(x0 - nx), py(y0 - ny, z0))], P_GLASS)
            for s in (-1, 1):
                cv.line(px(x0 + s * nx), py(y0 + s * ny, z0 + rail), px(x1 + s * nx), py(y1 + s * ny, z1 + rail), P_METAL_L, 1)
    return cv, lamps



# ---------------------------------------------------------------- the Atari's colours
I_LAMP, I_DROP, I_BUMPER, I_SLING, I_SPIN = 32, 64, 72, 82, 84
I_FLIP = 90                                   # (the flippers, drawn by the game)


def rgb12(c):
    return (((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def pen_rgb(theme=0):
    pal = {i: rgb12(c) for i, c in enumerate(COMMON)}
    for pen, c in zip((P_FIELD1, P_FIELD2, P_ART1, P_ART2, P_ART3, P_CAB_D, P_CAB_L, P_SHADOW, P_WOOD), THEME[theme]):
        pal[pen] = rgb12(c)
    for i in range(29):
        pal[I_LAMP + i] = pal[P_DIM]
    for i in range(8):
        pal[I_DROP + i] = pal[P_ORANGE]
    for i in range(10):
        pal[I_BUMPER + i] = pal[P_RED]
    pal[I_SLING] = pal[I_SLING + 1] = pal[P_RUBBER]
    pal[I_SPIN] = pal[I_SPIN + 1] = pal[P_METAL_L]
    pal[I_FLIP] = pal[P_RUBBER]
    pal[I_FLIP + 1] = pal[P_RED]
    return pal


def atari_rgb(c):
    """an approximation of the PAL GTIA palette (fitted to a measured one)"""
    hue, lum = c >> 4, c & 0x0E
    y = 0.08 + 0.92 * lum / 14.0
    if hue == 0:
        r = g = b = y
    else:
        ang = math.radians(132 - (hue - 1) * 24)
        u, v = 0.24 * math.cos(ang), 0.24 * math.sin(ang)
        r, g, b = y + 1.14 * v, y - 0.395 * u - 0.581 * v, y + 2.03 * u
    return tuple(int(max(0, min(1, k)) * 255) for k in (r, g, b))


def nearest_atari(rgb):
    """by hue angle and brightness"""
    r, g, b = (k / 255.0 for k in rgb)
    y = 0.299 * r + 0.587 * g + 0.114 * b
    u, v = 0.492 * (b - y), 0.877 * (r - y)
    lum = int(round(max(0.0, min(1.0, (y - 0.08) / 0.92)) * 7)) * 2
    if math.hypot(u, v) < 0.035:
        return lum
    ang = math.degrees(math.atan2(v, u))
    best, bd = 1, 1e9
    for hue in range(1, 16):
        dd = abs((ang - (132 - (hue - 1) * 24) + 180) % 360 - 180)
        if dd < bd:
            best, bd = hue, dd
    return best * 16 + lum


def cdist(a, b):
    ra, rb = atari_rgb(a), atari_rgb(b)
    return (ra[0] - rb[0]) ** 2 * 3 + (ra[1] - rb[1]) ** 2 * 4 + (ra[2] - rb[2]) ** 2 * 2


# ---------------------------------------------------------------- the picture
LAMP_ON_PEN = [P_BLUE, P_YEL, P_LRED, P_GREEN, P_MAG, P_ORANGE]


def draw(d, flippers=None, drops_up=True):
    cv, lamps = draw_table(d, d.get("theme", 0))
    if not drops_up:
        px_ = cv.im.load()
        for y in range(H):
            for x in range(W):
                if I_DROP <= px_[x, y] < I_DROP + 8:
                    px_[x, y] = P_BLACK
    if flippers:
        for f, ang in flippers:
            draw_flipper(cv, f, ang)
    return cv, lamps


def draw_flipper(cv, f, ang):
    side, fx, fy, ln, r1, r2 = f["side"], f["px"], f["py"], f["len"], f["r1"], f["r2"]
    tx, ty = fx + ln * math.cos(ang), fy + ln * math.sin(ang)
    wall3d(cv, fx, fy, tx, ty, (r1 + r2) / 2, 18, I_FLIP + 1, I_FLIP)


def flip_defs(d):
    out = []
    for f in d["flip"]:
        side, fx, fy, ln, r1, r2, rest, up = f[:8]
        out.append(dict(side=side, px=fx, py=fy, len=ln, r1=r1, r2=r2, rest=rest, up=up))
    return out


NFRAMES = 8


def frame_angles(f):
    return [f["rest"] + (f["up"] - f["rest"]) * k / (NFRAMES - 1) for k in range(NFRAMES)]


def to_mode4(im):
    """320 hires pixels -> 160: each pair takes its left pixel's pen, unless
    that is the playfield and the right one isn't (thin lines survive)"""
    src = im.load()
    out = [[0] * 160 for _ in range(H)]
    for y in range(H):
        for x in range(160):
            a, b = src[2 * x, y], src[2 * x + 1, y]
            out[y][x] = b if a in (P_FIELD1, P_FIELD2, P_WOOD) and b not in (P_FIELD1, P_FIELD2, P_WOOD) else a
    return out


# ---------------------------------------------------------------- rows, tiles, fonts
NROWS = H // 8


def row_palettes(pix, pal_atari, lamp_rows, flip_rows):
    """per character row: [bk, pf0, pf1, pf2, pf3] Atari colours. pf2/pf3
    are a lamp's off/on colours where lamps are (a lamp's pixels are colour
    3: the inverse bit picks pf3)"""
    out = []
    for r in range(NROWS):
        cnt = Counter()
        lamp_on = Counter()
        for y in range(r * 8, r * 8 + 8):
            for x in range(160):
                pen = pix[y][x]
                if I_LAMP <= pen < I_LAMP + 29:
                    lamp_on[pen] += 1
                    continue
                cnt[pal_atari[pen]] += 1
        cols = [c for c, _ in cnt.most_common()]
        bk = cols[0] if cols else 0
        rest = cols[1:]
        if r in flip_rows:                          # the flippers' colours are always there
            fl = [pal_atari[I_FLIP], pal_atari[I_FLIP + 1]]
            rest = [c for c in fl if c != bk] + [c for c in rest if c not in fl]
        if lamp_on:
            pf = (rest + [bk, bk])[:2]
            lamp = lamp_on.most_common(1)[0][0]
            out.append([bk, pf[0], pf[1], 0x04, LAMPCOL.get(lamp, 0x1E)])
        else:
            pf = (rest + [bk, bk, bk])[:3]
            out.append([bk, pf[0], pf[1], pf[2], 0x0E])
    return out


LAMPCOL = {}


def row_pixels(pix, pal_atari, rp, r):
    """the row's pixels as colour numbers 0..3 (lamps: 3)"""
    cols = rp[r][:4] if rp[r][3] != 0x04 else rp[r][:3]
    rows = []
    for y in range(r * 8, r * 8 + 8):
        line = []
        for x in range(160):
            pen = pix[y][x]
            if I_LAMP <= pen < I_LAMP + 29 and rp[r][3] == 0x04:
                line.append(3)
                continue
            c = pal_atari[pen]
            best = min(range(len(cols)), key=lambda k: cdist(c, cols[k]))
            line.append(best)
        rows.append(line)
    return rows


def cell_tile(rowpix, cx):
    t = []
    for y in range(8):
        b = 0
        for k in range(4):
            b = (b << 2) | rowpix[y][cx * 4 + k]
        t.append(b)
    return bytes(t)


def main():
    d = json.load(open(os.path.join(HERE, "table_0.json")))
    pens = pen_rgb(d.get("theme", 0))
    pal_atari = {p: nearest_atari(c) for p, c in pens.items()}
    for i, (x, y, r_, col) in enumerate(d["lamp"]):
        LAMPCOL[I_LAMP + i] = nearest_atari(pens[LAMP_ON_PEN[col]])
    flips = flip_defs(d)
    base_cv, lamps = draw(d)
    base = to_mode4(base_cv.im)
    down = to_mode4(draw(d, drops_up=False)[0].im)
    # the flippers' frames and the cells they touch
    frames = []
    cells_f = []
    for fi, f in enumerate(flips):
        fr = []
        cs = set()
        for ang in frame_angles(f):
            p = to_mode4(draw(d, flippers=[(f, ang)])[0].im)
            fr.append(p)
            for y in range(H):
                for x in range(160):
                    if p[y][x] != base[y][x]:
                        cs.add((y // 8, x // 4))
        frames.append(fr)
        cells_f.append(sorted(cs))
    flip_rows = {r for cs in cells_f for r, _ in cs}
    lamp_rows = set()
    rp = row_palettes(base, pal_atari, lamp_rows, flip_rows)
    print(f"  flipper cells: {[len(c) for c in cells_f]}, rows {sorted(flip_rows)}")
    # the tiles of every row (the base picture, the drop targets down)
    tiles = {}
    for r in range(NROWS):
        rpix = row_pixels(base, pal_atari, rp, r)
        dpix = row_pixels(down, pal_atari, rp, r)
        for cx in range(40):
            tiles[(r, cx)] = (cell_tile(rpix, cx), cell_tile(dpix, cx))
    per_row = []
    for r in range(NROWS):
        u = set()
        for cx in range(40):
            a, b = tiles[(r, cx)]
            u.add(a)
            u.add(b)
        per_row.append(len(u))
    print("  unique tiles per row:", per_row)
    # the drop targets' cells (where target i's pixels are)
    drop_cells = []
    for i in range(len(d["drop_seg"])):
        cs = set()
        for y in range(H):
            for x in range(160):
                if base[y][x] == I_DROP + i:
                    cs.add((y // 8, x // 4))
        drop_cells.append(sorted(cs))
    # the lamps' cells
    lamp_cells = [[] for _ in range(29)]
    for y in range(H):
        for x in range(160):
            pen = base[y][x]
            if I_LAMP <= pen < I_LAMP + 29 and rp[y // 8][3] == 0x04:
                c = (y // 8, x // 4)
                if c not in lamp_cells[pen - I_LAMP]:
                    lamp_cells[pen - I_LAMP].append(c)
    # fonts: rows in order, a new font when the next row's tiles don't fit
    flip_cell = {c: (fi, k) for fi, cs in enumerate(cells_f) for k, c in enumerate(cs)}
    fonts, font_of_row, code = [], [], {}
    cur, cur_tiles = None, None
    for r in range(NROWS):
        need = []
        for cx in range(40):
            if (r, cx) in flip_cell:
                need.append(("flip", r, cx))
            else:
                a, b = tiles[(r, cx)]
                need += [a, b]
        new = [t for t in dict.fromkeys(need) if t not in (cur_tiles or {})]
        if cur is None or len(cur_tiles) + len(new) > 128:
            cur_tiles = {}
            fonts.append(cur_tiles)
            cur = len(fonts) - 1
            new = list(dict.fromkeys(need))
        for t in new:
            cur_tiles[t] = len(cur_tiles)
        font_of_row.append(cur)
    print(f"  {len(fonts)} fonts: {[len(f) for f in fonts]}")
    # the map, the fonts' bytes
    mp = bytearray()
    for r in range(NROWS):
        f = fonts[font_of_row[r]]
        for cx in range(40):
            mp.append(f[("flip", r, cx)] if (r, cx) in flip_cell else f[tiles[(r, cx)][0]])
    fb = bytearray()
    for fi, f in enumerate(fonts):
        chars = bytearray(1024)
        for t, c in f.items():
            if isinstance(t, tuple):            # a flipper's cell: frame 0
                _, r, cx = t
                fl, k = flip_cell[(r, cx)]
                rpix = row_pixels(frames[fl][0], pal_atari, rp, r)
                t = cell_tile(rpix, cx)
            chars[c * 8:c * 8 + 8] = t
        fb += chars
    # the flippers' frames: [flipper][frame][cell] 8 bytes
    fl_bytes = bytearray()
    for fl, cs in enumerate(cells_f):
        for fr in frames[fl]:
            rows = {}
            for (r, cx) in cs:
                if r not in rows:
                    rows[r] = row_pixels(fr, pal_atari, rp, r)
                fl_bytes += cell_tile(rows[r], cx)
    return dict(d=d, rp=rp, tiles=tiles, cells_f=cells_f, fonts=fonts, font_of_row=font_of_row, map=mp,
                fontbytes=fb, flipbytes=fl_bytes, drop_cells=drop_cells, lamp_cells=lamp_cells, flip_cell=flip_cell)


def write(o):
    """gfx/*.bin and src/gfx_data.c/.h"""
    d, fonts = o["d"], o["fonts"]
    gfx = os.path.join(PROJ, "gfx")
    os.makedirs(gfx, exist_ok=True)
    open(os.path.join(gfx, "fonts.bin"), "wb").write(o["fontbytes"])
    open(os.path.join(gfx, "map.bin"), "wb").write(o["map"])
    open(os.path.join(gfx, "flips.bin"), "wb").write(o["flipbytes"])
    nf = len(fonts)
    hdr = ["/* made by tools/gen_gfx.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
           "#include <stdint.h>", "",
           f"#define TROWS {NROWS}                  /* the table's character rows */",
           f"#define NFONTS {nf}",
           f"#define FONT_BYTES {len(o['fontbytes'])}",
           f"#define FLIP_CELLS {len(o['cells_f'][0])}             /* each flipper's cells */",
           f"#define FLIP_FRAMES {NFRAMES}",
           f"#define VIEW_KX_Q12 {int(round(KX / 2 * 4096 / 16))}    /* mode 4 pixels a P (1/16 mm), Q12 */",
           f"#define VIEW_KY_Q12 {int(round(KY * 4096 / 16))}    /* scan lines a P, Q12 */",
           f"#define VIEW_KZ_Q12 {int(round(KZ * 4096))}     /* scan lines a mm of height, Q12 */",
           f"#define VIEW_OX {OX / 2:.0f}                 /* mode 4 pixel of x = 0 */",
           f"#define VIEW_OY {int(round(OY))}               /* scan line of y = 0 */",
           "extern const uint8_t row_col[TROWS][5];   /* COLBK, PF0..PF3 */",
           "extern const uint8_t row_font[TROWS];     /* which font */",
           "extern const uint8_t lamp_n[29];          /* each lamp's cells */",
           "extern const uint16_t lamp_cell[29][4];   /* their offsets in the map */",
           "extern const uint8_t drop_n[8];",
           "extern const uint16_t drop_cell[8][6];",
           "extern const uint8_t drop_up[8][6], drop_down[8][6];   /* the characters standing / down */",
           "extern const uint16_t flip_cell[2][FLIP_CELLS];        /* the map offset of each flipper cell */",
           "", "#endif", ""]
    src = ["/* made by tools/gen_gfx.py - do not edit */", '#include "gfx_data.h"', ""]
    src.append("const uint8_t row_col[TROWS][5] = {")
    for r in o["rp"]:
        src.append("    { " + ", ".join(f"0x{c:02X}" for c in r) + " },")
    src.append("};")
    src.append("const uint8_t row_font[TROWS] = { " + ", ".join(str(f) for f in o["font_of_row"]) + " };")
    ln, lc = [], []
    for cs in o["lamp_cells"]:
        cs = cs[:4]
        ln.append(len(cs))
        lc.append("{ " + ", ".join(str(r * 40 + cx) for r, cx in cs + [(0, 0)] * (4 - len(cs))) + " }")
    src.append("const uint8_t lamp_n[29] = { " + ", ".join(map(str, ln)) + " };")
    src.append("const uint16_t lamp_cell[29][4] = { " + ", ".join(lc) + " };")
    dn, dc, du, dd = [], [], [], []
    for i in range(8):
        cs = o["drop_cells"][i][:6] if i < len(o["drop_cells"]) else []
        dn.append(len(cs))
        dc.append("{ " + ", ".join(str(r * 40 + cx) for r, cx in cs + [(0, 0)] * (6 - len(cs))) + " }")
        ups, downs = [], []
        for r, cx in cs:
            f = fonts[o["font_of_row"][r]]
            a, b = o["tiles"][(r, cx)]
            ups.append(f[a])
            downs.append(f[b])
        du.append("{ " + ", ".join(str(v) for v in ups + [0] * (6 - len(ups))) + " }")
        dd.append("{ " + ", ".join(str(v) for v in downs + [0] * (6 - len(downs))) + " }")
    src.append("const uint8_t drop_n[8] = { " + ", ".join(map(str, dn)) + " };")
    src.append("const uint16_t drop_cell[8][6] = { " + ", ".join(dc) + " };")
    src.append("const uint8_t drop_up[8][6] = { " + ", ".join(du) + " };")
    src.append("const uint8_t drop_down[8][6] = { " + ", ".join(dd) + " };")
    fc = []
    for cs in o["cells_f"]:
        fc.append("{ " + ", ".join(str(r * 40 + cx) for r, cx in cs) + " }")
    src.append("const uint16_t flip_cell[2][FLIP_CELLS] = { " + ", ".join(fc) + " };")
    open(os.path.join(PROJ, "src", "gfx_data.h"), "w").write("\n".join(hdr))
    open(os.path.join(PROJ, "src", "gfx_data.c"), "w").write("\n".join(src) + "\n")
    print(f"  fonts {len(o['fontbytes'])} bytes, map {len(o['map'])}, flips {len(o['flipbytes'])}")


def preview(rp, tiles, path):
    im = Image.new("RGB", (320, H))
    p = im.load()
    for r in range(NROWS):
        cols = [atari_rgb(c) for c in rp[r]]
        for cx in range(40):
            t = tiles[(r, cx)][0]
            for y in range(8):
                for k in range(4):
                    v = (t[y] >> (6 - 2 * k)) & 3
                    c = cols[v]
                    p[cx * 8 + k * 2, r * 8 + y] = c
                    p[cx * 8 + k * 2 + 1, r * 8 + y] = c
    im.resize((640, H * 2)).save(path)


if __name__ == "__main__":
    o = main()
    write(o)
    if "--preview" in sys.argv:
        preview(o["rp"], o["tiles"], os.path.join(PROJ, "build", "table_preview.png"))
