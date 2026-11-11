#!/usr/bin/env python3
"""Boing Twist for the Atari 130XE: everything made on the host.

The ball is drawn by 6502 code made here, one routine per view, that runs
from a 130XE bank: for every byte of the ball's picture (ANTIC mode D, 4
pixels a byte) it fetches the four texels the pixels show and stores the
byte:

    lda T+o3,y : lsr : lsr : ora T+o2,y : lsr : lsr : ora T+o1,y
    lsr : lsr : ora T+o0,y : sta buf

T is the ball's texture (64 x 32 texels, colour << 6, each row 96 bytes
long: columns 64-95 repeat 0-31) in the RAM under the OS ROM; Y is the
ball's turn (0-63 texels), so a turn costs nothing. o is the texel the
pixel shows at turn 0 (v * 96 + u, u < 32: the visible half). About 8.5
cycles a pixel: a frame for the whole ball.

The flip is a turn too: at the start of a flip the texture is resampled
round the flip's axis (which faces the eye) into FT (64 angles x 16
radii) by a table made here, and the flip view's code reads those with Y
= the angle turned.

Views (src/gfx_data.h has their sizes and entry points):
  play    the ball you twist (the original's camera, 3.6 away)
  flip    the same pixels, round the flip axis
  title   the Boing ball leaning -17 degrees, smaller (4.4 away); also the
          select screen's preview

Also made here: each row's shading (the original's light, fixed to the
eye, averaged along the row: the DLI sets each row's colours), the ten
rooms (one background colour per row: the walls, the floor and the grid
lines, drawn with the original's box), the pictures (64 x 32 texels in 3
colours each, the Atari palette), and the bands' places for the markers.

Outputs: gfx/bank0..3.bin (16K each, loaded into the banks by the XEX),
src/gfx_data.h, src/gfx_data.c. Run it from anywhere:
    python3 projects/boing_twist/tools/gen_data.py
"""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ORIG = os.path.normpath(os.path.join(PROJ, "..", "..", "..", "amiga_mcp", "examples", "boing_twist"))

# ---------------------------------------------------------------- layout
TEX_W, TEX_H, ROW = 64, 32, 96          # the texture: lon x lat texels, bytes a row
FT_W, FT_H = 64, 16                      # the flip's texture: angle x radius
BUF = 0xA040                             # the ball's picture: 64 rows of 40 bytes
BUF_ROWS = 64
STRIDE = 40
BALL_CX, BALL_CY = 80, 32                # its middle (pixels, rows) in the buffer
TEX_A = 0xD800                            # the texture: RAM under the OS ROM (boing_twist.cfg TEXRAM)
FT_A = 0xB000                             # the flip texture (screen area)
SCREEN_ROWS = 88                          # mode D rows above the text
PLAY_DIST, TITLE_DIST, SELECT_DIST = 3.6, 4.4, 3.9
PLAY_D = 60                               # the ball's diameter in pixels at the play distance
FOCAL = (PLAY_D / 2) / math.tan(math.asin(1 / PLAY_DIST))
LIGHT = np.array([-0.45, 0.55, 0.70])
LIGHT /= np.linalg.norm(LIGHT)


def rot_axis(ax, ay, az, deg):
    a = math.radians(deg)
    c, s, t = math.cos(a), math.sin(a), 1 - math.cos(a)
    return np.array([
        [t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay],
        [t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax],
        [t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c],
    ])


def ball_rot(pitch, roll):
    """the original's render.c ball_rot at yaw 0: the axis tipped, then leaned"""
    return rot_axis(0, 0, 1, roll) @ rot_axis(1, 0, 0, pitch)


def ball_pixels(dist, pitch, roll):
    """every buffer pixel the ball covers: x, y, q (the point of the ball in its
    own frame), n (the normal in eye space)"""
    ys, xs = np.mgrid[0:BUF_ROWS, 0:160]
    d = np.stack([(xs + 0.5 - BALL_CX) / FOCAL, -(ys + 0.5 - BALL_CY) / FOCAL, -np.ones(xs.shape)], -1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    C = np.array([0.0, 0.0, -dist])
    b = d @ C
    disc = b * b - (C @ C - 1.0)
    hit = disc >= 0
    t = b - np.sqrt(np.where(hit, disc, 0))
    p = t[..., None] * d - C                     # on the unit ball, eye frame
    q = p @ ball_rot(pitch, roll)
    return xs[hit], ys[hit], q[hit], p[hit]


def tex_uv(q):
    lat = np.degrees(np.arcsin(np.clip(q[:, 1], -1, 1)))
    lon = np.degrees(np.arctan2(q[:, 0], q[:, 2]))
    u = np.floor(lon * TEX_W / 360.0).astype(int) % TEX_W
    v = np.clip(np.floor((90.0 - lat) * TEX_H / 180.0).astype(int), 0, TEX_H - 1)
    return u, v


def view_offsets(q, full=False):
    """texture offsets at turn 0: v * ROW + (u + 16) % 64 (the visible half
    has u + 16 in 0..31). Pixels the half can't hold (tilted views) get
    the nearest that fits."""
    u, v = tex_uv(q)
    ur = (u + 16) % TEX_W
    if not full:
        assert ur.max() < 32, "a view sees more than half the ball"
    return v * ROW + ur


# ---------------------------------------------------------------- code
LDA_Y, ORA_Y, LSR, STA, RTS = 0xB9, 0x19, 0x4A, 0x8D, 0x60


def make_code(xs, ys, offs, base):
    """the routine drawing one view: per buffer byte the texels of its pixels.
    The texture holds colour << 6; the byte is built from its right-hand
    pixel: lda p3 : lsr : lsr : ora p2 : lsr : lsr : ora p1 : lsr : lsr : ora p0"""
    by = {}
    for x, y, o in zip(xs, ys, offs):
        by.setdefault((int(y), int(x) >> 2), [None] * 4)[int(x) & 3] = int(o)
    code = bytearray()

    def op(opc, addr):
        code.extend((opc, addr & 0xFF, addr >> 8))

    rows = {}
    for (y, bx) in sorted(by):
        p = by[(y, bx)]
        first = True
        for i in (3, 2, 1, 0):
            if p[i] is not None:
                op(LDA_Y if first else ORA_Y, base + p[i])
                first = False
            if not first and i:
                code.extend((LSR, LSR))
        op(STA, BUF + y * STRIDE + bx)
        r = rows.setdefault(y, [bx, bx])
        r[0], r[1] = min(r[0], bx), max(r[1], bx)
    code.append(RTS)
    return bytes(code), rows


def row_shading(ys, n):
    """each buffer row's light: the original's diffuse + highlight, averaged
    along the row -> a luminance step (-6..+4) for the DLI"""
    lam = np.clip(n @ LIGHT, 0, 1)
    out = []
    for r in range(BUF_ROWS):
        m = ys == r
        if not m.any():
            out.append(0)
            continue
        l = float(np.mean(0.30 + 0.70 * lam[m]))
        out.append(int(round((l - 0.72) * 14)) * 2)
    return [max(-6, min(4, v)) for v in out]


class View:
    def __init__(self, name, dist, pitch, roll, flip=False):
        self.name = name
        xs, ys, q, n = ball_pixels(dist, pitch, roll)
        self.xs, self.ys, self.q = xs, ys, q
        if flip:
            # the flip texture: angle round the screen middle (64 steps), radius (16)
            R = (FOCAL * math.tan(math.asin(1 / dist)))
            dx, dy = xs + 0.5 - BALL_CX, -(ys + 0.5 - BALL_CY)
            a = np.floor(np.degrees(np.arctan2(dy, dx)) % 360.0 * FT_W / 360.0).astype(int) % FT_W
            r = np.clip(np.floor(np.hypot(dx, dy) / R * FT_H).astype(int), 0, FT_H - 1)
            offs = r * ROW + a
            self.code, self.rows = make_code(xs, ys, offs, FT_A)
            # the table: each flip texel -> the texture offset (turn 0) it samples
            tab = []
            for rr in range(FT_H):
                for aa in range(FT_W):
                    ang = math.radians((aa + 0.5) * 360.0 / FT_W)
                    rad = min((rr + 0.5) / FT_H, 0.995) * R
                    px, py = rad * math.cos(ang), rad * math.sin(ang)
                    dd = np.array([px / FOCAL, py / FOCAL, -1.0])
                    dd /= np.linalg.norm(dd)
                    C = np.array([0.0, 0.0, -dist])
                    b = dd @ C
                    disc = max(b * b - (C @ C - 1.0), 0.0)
                    t = b - math.sqrt(disc)
                    p = t * dd - C
                    p /= np.linalg.norm(p)
                    tab.append(int(view_offsets(p[None, :] @ ball_rot(pitch, roll))[0]))
            self.fliptab = tab
        else:
            self.code, self.rows = make_code(xs, ys, view_offsets(q), TEX_A)
        self.shade = row_shading(ys, n)
        self.top = int(ys.min())
        self.bottom = int(ys.max())
        self.radius = (FOCAL * math.tan(math.asin(1 / dist)))
        self.pitch, self.roll, self.dist = pitch, roll, dist
        print(f"  view {name}: {len(xs)} pixels, rows {self.top}-{self.bottom}, code {len(self.code)} bytes")

    def band_rows(self, bands):
        """the screen rows (top, bottom) of each band down the ball's middle"""
        out = []
        for b in range(bands):
            lat0, lat1 = 90 - 180 * b / bands, 90 - 180 * (b + 1) / bands
            ys = []
            for lat in (lat0, lat1):
                la = math.radians(lat)
                p = np.array([0.0, math.sin(la), math.cos(la)])
                p = p @ ball_rot(self.pitch, self.roll).T
                # perspective
                z = -self.dist + p[2]
                ys.append(BALL_CY - FOCAL * p[1] / -z)
            out.append((int(round(min(ys))), int(round(max(ys)))))
        return out


# ---------------------------------------------------------------- the Atari palette
def atari_rgb(c):
    """an approximation of the PAL GTIA palette: hue 0 grey, 1-15 round the wheel"""
    hue, lum = c >> 4, c & 0x0E
    y = 0.08 + 0.92 * lum / 14.0
    if hue == 0:
        r = g = b = y
    else:
        ang = math.radians(132 - (hue - 1) * 24)   # fitted to a measured palette: 1 gold, 4 magenta, 8 blue, 12 green
        sat = 0.24
        u, v = sat * math.cos(ang), sat * math.sin(ang)
        r, g, b = y + 1.14 * v, y - 0.395 * u - 0.581 * v, y + 2.03 * u
    return tuple(int(max(0, min(1, k)) * 255) for k in (r, g, b))


PAL = [(c, atari_rgb(c)) for c in range(0, 256, 2)]


def nearest_atari(rgb):
    """by hue angle and brightness (RGB distance picks greys for the muted
    colours a 3-colour quantise gives)"""
    r, g, b = (k / 255.0 for k in rgb)
    y = 0.299 * r + 0.587 * g + 0.114 * b
    u, v = 0.492 * (b - y), 0.877 * (r - y)
    lum = int(round(max(0.0, min(1.0, (y - 0.08) / 0.92)) * 7)) * 2
    if math.hypot(u, v) < 0.035:
        return lum
    ang = math.degrees(math.atan2(v, u))
    best, bd = 1, 1e9
    for hue in range(1, 16):
        d = abs((ang - (132 - (hue - 1) * 24) + 180) % 360 - 180)
        if d < bd:
            best, bd = hue, d
    return best * 16 + lum


def nearest_atari_rgb(rgb):
    r, g, b = rgb
    best, bd = 0, 1e9
    for c, (pr, pg, pb) in PAL:
        d = (r - pr) ** 2 * 0.3 + (g - pg) ** 2 * 0.59 + (b - pb) ** 2 * 0.11
        if d < bd:
            best, bd = c, d
    return best


def hexrgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


# ---------------------------------------------------------------- pictures
LEVELS = {"easy": 0, "medium": 1, "hard": 2, "expert": 3}


def read_titles():
    pics = []
    for line in open(os.path.join(ORIG, "images", "titles.txt")):
        line = line.split("#")[0].strip()
        if not line or "=" not in line:
            continue
        name, rest = (s.strip() for s in line.split("=", 1))
        parts = [p.strip() for p in rest.split("|")]
        title = parts[0].upper()[:20]
        level = LEVELS[parts[1].lower()] if len(parts) > 1 and parts[1] else 0
        room = parts[2] if len(parts) > 2 else ""
        pics.append((int(name.split("_")[0]), name, title, level, room))
    pics.sort()
    return pics


def picture(path):
    """64 x 32 texels in 3 colours (values 1..3; 0 is the groove)"""
    from PIL import ImageEnhance
    im = Image.open(path).convert("RGB").resize((TEX_W, TEX_H), Image.LANCZOS)
    im = ImageEnhance.Color(im).enhance(1.8)
    q = im.quantize(colors=3, method=Image.Quantize.MEDIANCUT, kmeans=4, dither=Image.Dither.NONE)
    pal = q.getpalette()[:9]
    idx = np.array(q, dtype=np.uint8)
    cols = [nearest_atari(tuple(pal[3 * i:3 * i + 3])) for i in range(3)]
    # darkest first (the shading lifts the others)
    order = sorted(range(3), key=lambda i: sum(pal[3 * i:3 * i + 3]))
    remap = {o: k for k, o in enumerate(order)}
    idx = np.vectorize(lambda v: remap[v] + 1)(idx).astype(np.uint8)
    cols = [cols[o] for o in order]
    packed = bytearray()
    for row in idx:
        for x in range(0, TEX_W, 4):
            packed.append((row[x] << 6) | (row[x + 1] << 4) | (row[x + 2] << 2) | row[x + 3])
    return bytes(packed), cols


# ---------------------------------------------------------------- rooms
ROOM_ORDER = ["boing", "midnight", "space", "aquarium", "arcade", "garden", "observatory",
              "retroroom", "study", "workshop"]


def read_theme(name):
    th = {}
    for line in open(os.path.join(ORIG, "themes", name + ".theme")):
        line = line.split("#")[0].strip()
        if "=" in line:
            k, v = (s.strip() for s in line.split("=", 1))
            th[k] = v
    return th


def room_rows(th, dist):
    """one background colour per mode D row (88): down the middle of the
    screen the ray hits the back wall, the floor or the ceiling; near a
    grid line (every 0.5) it takes the line's colour"""
    back = th["back"].split()
    floor = th["floor"].split()
    ceil = th.get("ceiling", "off").split()
    bg = hexrgb(th["background"])
    out = []
    pix = 1.0 / FOCAL
    for r in range(SCREEN_ROWS):
        # rows: the ball's middle (buffer row 32) sits at screen row 40
        sy = -(r + 0.5 - 40) * 1.0            # in pixels, up positive
        dy = sy / FOCAL
        col = bg
        # back wall at z = -6.5 (the ball's centre 3.6 away -> the camera is at the origin)
        tz = 6.5
        yw = dy * tz
        if -1.5 <= yw <= 2.2 and back[0] != "off":
            col = hexrgb(back[0])
            if back[1] != "none" and abs(yw / float(back[2]) - round(yw / float(back[2]))) < 0.5 * pix * tz * 2 / float(back[2]):
                col = hexrgb(back[1])
        elif yw < -1.5 and floor[0] != "off":
            t = -1.5 / dy                      # floor hit distance
            col = hexrgb(floor[0])
            if floor[1] != "none":
                cell = float(floor[2])
                z = t
                w = pix * t * t / 1.5 * 1.2    # one row's depth there
                if abs(z / cell - round(z / cell)) < max(w / cell, 0.02):
                    col = hexrgb(floor[1])
        elif yw > 2.2 and ceil[0] != "off":
            col = hexrgb(ceil[0])
        out.append(nearest_atari(col))
    return out


def room_shadow(th):
    sh = th.get("shadow", "floor 50").split()
    return (1 if sh[0] == "wall" else 0), int(sh[1])


# ---------------------------------------------------------------- main
def main():
    views = {
        "play": View("play", PLAY_DIST, 0.0, 0.0),
        "flip": View("flip", PLAY_DIST, 0.0, 0.0, flip=True),
        "title": View("title", TITLE_DIST, 0.0, -17.0),
    }
    banks = [bytearray(0x4000) for _ in range(4)]
    entry = {}

    def put(bank, off, data):
        # bank 2 from $6680 holds the rules' code (boing_twist.cfg XB2HI)
        assert off + len(data) <= (0x2680 if bank == 2 else 0x3FF0), f"bank {bank} full"
        banks[bank][off:off + len(data)] = data
        return 0x4000 + off

    entry["play"] = (0, put(0, 0, views["play"].code))
    entry["flip"] = (1, put(1, 0, views["flip"].code))
    t = views["title"].code
    entry["title"] = (2, put(2, 0, t))
    ft = bytearray()
    for o in views["flip"].fliptab:
        ft += bytes((o & 0xFF, o >> 8))
    fliptab_addr = put(3, 0, ft)

    pics = read_titles()
    pic_recs = []
    off = len(ft)
    for num, name, title, level, room in pics:
        data, cols = picture(os.path.join(ORIG, "images", name + ".jpg"))
        addr = put(3, off, data)
        off += len(data)
        ri = ROOM_ORDER.index(room) if room in ROOM_ORDER else 0
        pic_recs.append((title, level, ri, addr, cols))
    print(f"  bank 3: flip table + {len(pics)} pictures, {off} bytes")
    for b in range(4):
        banks[b][0x3FFC:0x4000] = b"BT" + bytes((0x30 + b, 0xA5))
        with open(os.path.join(PROJ, "gfx", f"bank{b}.bin"), "wb") as f:
            f.write(banks[b])

    rooms = []
    for name in ROOM_ORDER:
        th = read_theme(name)
        rows = room_rows(th, PLAY_DIST)
        wall, dark = room_shadow(th)
        rooms.append((th["name"].upper(), rows, wall, dark))

    # ---- header + tables
    pv = views["play"]
    hdr = ["/* made by tools/gen_data.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
           "#include <stdint.h>", "",
           f"#define TEX_W {TEX_W}", f"#define TEX_H {TEX_H}", f"#define TEX_ROW {ROW}",
           f"#define FT_W {FT_W}", f"#define FT_H {FT_H}",
           f"#define TEX_A_ADDR 0x{TEX_A:04X}", f"#define FT_A_ADDR 0x{FT_A:04X}",
           f"#define BUF_ADDR 0x{BUF:04X}", f"#define BUF_ROWS {BUF_ROWS}", f"#define BUF_STRIDE {STRIDE}",
           f"#define SCREEN_ROWS {SCREEN_ROWS}", f"#define FLIPTAB_ADDR 0x{fliptab_addr:04X}", ""]
    hdr.append("typedef struct { uint8_t bank; uint16_t entry; uint8_t top, bottom, radius; } View;")
    hdr.append("enum { V_PLAY, V_FLIP, V_TITLE, NVIEWS };")
    hdr.append("extern const View views[NVIEWS];")
    hdr.append("extern const int8_t view_shade[NVIEWS][BUF_ROWS];")
    hdr.append("typedef struct { const char *title; uint8_t level, room; uint16_t addr; uint8_t col[3]; } Pic;")
    hdr.append(f"#define NPICS {len(pic_recs) + 1}   /* 0: the Boing ball, made by the game */")
    hdr.append("extern const Pic pics[NPICS];")
    hdr.append("typedef struct { const char *name; uint8_t wall, dark; } Room;")
    hdr.append(f"#define NROOMS {len(rooms)}")
    hdr.append("extern const Room rooms[NROOMS];")
    hdr.append("extern const uint8_t room_rows[NROOMS][SCREEN_ROWS];")
    hdr.append("/* the bands' rows down the play ball's middle, per grid (4, 6, 8 bands) */")
    hdr.append("extern const uint8_t band_top[3][8], band_bot[3][8];")
    hdr += ["", "#endif", ""]
    src = ["/* made by tools/gen_data.py - do not edit */", '#include "gfx_data.h"', "",
           "const View views[NVIEWS] = {"]
    for k in ("play", "flip", "title"):
        v = views[k]
        b, e = entry[k]
        src.append(f"    {{ {b}, 0x{e:04X}, {v.top}, {v.bottom}, {int(round(v.radius))} }},   /* {k} */")
    src.append("};")
    src.append("const int8_t view_shade[NVIEWS][BUF_ROWS] = {")
    for k in ("play", "flip", "title"):
        src.append("    { " + ", ".join(str(s) for s in views[k].shade) + " },")
    src.append("};")
    src.append("const Pic pics[NPICS] = {")
    src.append('    { "THE BOING BALL", 0, 0, 0, { 0x0E, 0x34, 0x34 } },')
    for title, level, ri, addr, cols in pic_recs:
        src.append(f'    {{ "{title}", {level}, {ri}, 0x{addr:04X}, {{ 0x{cols[0]:02X}, 0x{cols[1]:02X}, 0x{cols[2]:02X} }} }},')
    src.append("};")
    src.append("const Room rooms[NROOMS] = {")
    for name, rows, wall, dark in rooms:
        src.append(f'    {{ "{name}", {wall}, {dark} }},')
    src.append("};")
    src.append("const uint8_t room_rows[NROOMS][SCREEN_ROWS] = {")
    for name, rows, wall, dark in rooms:
        src.append("    { " + ", ".join(f"0x{c:02X}" for c in rows) + " },")
    src.append("};")
    bt, bb = [], []
    for nb in (4, 6, 8):
        br = pv.band_rows(nb) + [(0, 0)] * (8 - nb)
        bt.append(", ".join(str(a) for a, _ in br))
        bb.append(", ".join(str(b) for _, b in br))
    src.append("const uint8_t band_top[3][8] = { {" + "}, {".join(bt) + "} };")
    src.append("const uint8_t band_bot[3][8] = { {" + "}, {".join(bb) + "} };")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write("\n".join(hdr))
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write("\n".join(src) + "\n")


if __name__ == "__main__":
    main()
