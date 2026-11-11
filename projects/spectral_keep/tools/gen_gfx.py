#!/usr/bin/env python3
"""Spectral Keep's pictures for the Atari, from the original's own renderer
(tools/export.c: each room's pieces and draw list, and its own picture).

A room is drawn on the Atari as the original's scene.c draws it: its
pieces (floor tiles, wall bricks, door steps, blocks, arches, spikes, gates,
the throne) in the draw list's order. ANTIC mode E: a pixel is two of the
original's across and one line down.

The pieces are stored as three brightness levels (pixel values 1-3; 0 is
transparent): a room's colours are its bands' (a DLI every BAND lines sets
PF0-PF2 for the levels 1-3; COLBK is the room's background). The levels
don't depend on a room's ink, so pieces are shared between rooms: each
distinct piece is stored once.

    python3 tools/gen_gfx.py [--preview]          (after tools/export.c)
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rooms_io import read_room, rgb  # noqa: E402
import gen_rooms as gr  # noqa: E402  (the palette, the colour spaces, to_atari)

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
HOST = os.path.join(PROJ, "build", "host")
W, H, BAND = 160, 208, 8


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def half_piece(px):
    """a piece (RGB15, 0 transparent) -> half width: RGB (h x w/2 x 3), opaque mask"""
    h, w = px.shape
    if w & 1:
        px = np.hstack([px, np.zeros((h, 1), px.dtype)])
    a = rgb(px)
    op = px != 0
    l, r = op[:, 0::2], op[:, 1::2]
    al, ar = a[:, 0::2], a[:, 1::2]
    both = (l & r)[..., None]
    out = np.where(both, (al + ar) / 2, np.where(l[..., None], al, ar))
    return out, (l | r)


def levels(a, op, t1, t2):
    y = luma(a)
    lv = np.where(y < t1, 1, np.where(y < t2, 2, 3)).astype(np.uint8)
    return np.where(op, lv, 0).astype(np.uint8)


# the rooms' wall (0 brick, 1 rock) and floor (0 slab, 1 wood, 2 cobble) types (levels.c)
def room_types():
    import re
    txt = open(os.path.join(PROJ, "src", "levels.c")).read()
    out = {}
    for l, block in enumerate(re.findall(r"static const KRoom rooms_\d\[\] = \{(.*?)\n\};", txt, re.S)):
        for r, m in enumerate(re.finditer(r'\{ "[^"]*", "[^"]*", (-?\d+), (-?\d+), (\d+), (\d+), (\d+), "[^"]*", (\d), (\d),', block)):
            out[(l, r)] = (int(m.group(6)), int(m.group(7)))
    return out


def piece_class(slot, wall, floor):
    """which rooms share a piece slot's picture: the floor tiles by floor
    type, the wall bricks by wall type, the rest everywhere"""
    if slot < 4:
        return (slot, "f", floor)
    if slot < 8:
        return (slot, "w", wall)
    return (slot,)


def main():
    types = room_types()
    rooms = []
    for f in sorted(glob.glob(os.path.join(HOST, "room_*.bin"))):
        l, r = map(int, os.path.basename(f)[5:-4].split("_"))
        pieces, ops, org = read_room(f)
        rooms.append((l, r, pieces, ops))
    table = []          # distinct pieces: level maps (h x w)
    index = {}
    out_rooms = []
    for l, r, pieces, ops in rooms:
        wall, floor = types[(l, r)]
        hp = [half_piece(p["px"]) if p["w"] else (np.zeros((0, 0, 3)), np.zeros((0, 0), bool)) for p in pieces]
        ys = np.concatenate([luma(a)[op] for a, op in hp if op.size])
        t1, t2 = np.quantile(ys, [0.33, 0.70])
        pid = []
        for slot, (a, op) in enumerate(hp):
            key = piece_class(slot, wall, floor)
            if key not in index:
                lv = levels(a, op, t1, t2) if op.size else np.zeros((0, 0), np.uint8)
                index[key] = len(table)
                table.append(lv)
            pid.append(index[key])
        # compose: levels and the source colours, at the Atari's size
        lvpic = np.zeros((H, W), np.uint8)
        srcpic = np.zeros((H, W, 3), np.float32)
        for op_ in ops:
            a, opm = hp[op_["piece"]]
            if not opm.size:
                continue
            lv = table[pid[op_["piece"]]]
            x0, y0 = op_["x"] // 2, op_["y"]
            h, w = lv.shape
            ys0, ys1 = max(0, y0), min(H, y0 + h)
            xs0, xs1 = max(0, x0), min(W, x0 + w)
            sub = (slice(ys0 - y0, ys1 - y0), slice(xs0 - x0, xs1 - x0))
            m = lv[sub] != 0
            lvpic[ys0:ys1, xs0:xs1][m] = lv[sub][m]
            srcpic[ys0:ys1, xs0:xs1][m] = a[sub][m]
        out_rooms.append({"l": l, "r": r, "pid": pid, "ops": ops, "lv": lvpic, "src": srcpic})
    # each band's colour of each level: the mean source colour there, as an Atari colour
    for ro in out_rooms:
        cols = []
        src = gr.stretch(ro["src"], np.zeros(3))
        for b0 in range(0, H, BAND):
            c = []
            for lvl in (1, 2, 3):
                m = ro["lv"][b0:b0 + BAND] == lvl
                if m.any():
                    mean = src[b0:b0 + BAND][m].mean(0)
                    c.append(int(gr.PAL_IDX[((gr.PALY - gr.yuv(gr.to_atari(mean[None, None, :])[0, 0])) ** 2).sum(1).argmin()]))
                else:
                    c.append(c[-1] if c else 0)
            cols.append(c)
        ro["cols"] = cols
    nbytes = sum(((lv.shape[1] + 3) // 4) * lv.shape[0] for lv in table)
    print(f"{len(table)} distinct pieces, {nbytes} bytes; ops {sum(len(r['ops']) for r in out_rooms)}")
    if "--preview" in sys.argv:
        for ro in out_rooms:
            pic = np.zeros((H, W * 2, 3), np.uint8)
            for y in range(H):
                c = [0x00] + ro["cols"][y // BAND]
                pic[y] = np.repeat(gr.PAL[[c[v] for v in ro["lv"][y]]].astype(np.uint8), 2, axis=0)
            Image.fromarray(pic).resize((W * 4, H * 2), Image.NEAREST).save(
                os.path.join(HOST, f"comp_{ro['l']}_{ro['r']}.png"))
    return table, out_rooms


if __name__ == "__main__" and "--banks" not in sys.argv:
    main()


# ---------------------------------------------------------------- bank data

SPR_NAMES = ([f"walker{m}_{f}_{k}" for m in range(3) for f in range(8) for k in range(3)] +
             [f"sage{f}" for f in range(8)] + [f"ghost{f}" for f in range(8)] +
             [f"bouncer{f}" for f in range(8)] + [f"relic{f}" for f in range(16)] +
             [f"key{f}" for f in range(16)] + ["potion", "crate", "lift"] +
             [f"piston{k}" for k in range(9)])


def pack2(lv):
    """level map (h x w, 0-3) -> rows of bytes, 4 pixels a byte (left = high bits)"""
    h, w = lv.shape
    wb = (w + 3) // 4
    out = []
    for y in range(h):
        for b in range(wb):
            v = 0
            for k in range(4):
                x = b * 4 + k
                v = (v << 2) | (int(lv[y, x]) if x < w else 0)
            out.append(v)
    return wb, out


def sprite_levels(s, t1=90, t2=170):
    a, op = half_piece(s["px"])
    return levels(a, op, t1, t2)


def pm_bits(op_row):
    v = 0
    for x in range(8):
        v = (v << 1) | (1 if x < len(op_row) and op_row[x] else 0)
    return v


def pmg_frame(s, multi):
    """a character frame -> (xoff, yoff, double, rows): rows of 1 byte (a
    monster's silhouette) or 2 (the explorer: P0, P1, their overlap the
    third colour). x / y offsets: from the body's projected point, in
    Atari pixels and lines; double: double-width players (2 pixels a bit)"""
    a, op = half_piece(s["px"])
    h, w = op.shape
    ax = s["ax"] // 2
    double = w > 9
    if double:                                   # 2 pixels a bit
        op = op[:, 0::2] | np.pad(op[:, 1::2], ((0, 0), (0, op.shape[1] % 2)))[:, :op[:, 0::2].shape[1]]
        a = a[:, 0::2]
        ax //= 2
    w = op.shape[1]
    x0 = max(0, (w - 8) // 2)
    op8, a8 = op[:, x0:x0 + 8], a[:, x0:x0 + 8]
    xoff = -(ax - x0) * (2 if double else 1)
    rows = []
    if multi:
        y = luma(a8)
        for r in range(h):
            p0 = p1 = 0
            for x in range(8):
                bit = 0x80 >> x
                if x < op8.shape[1] and op8[r, x]:
                    l = y[r, x]
                    if l < 140: p0 |= bit                # dark: P0
                    elif l < 200: p1 |= bit              # mid: P1
                    else: p0 |= bit; p1 |= bit           # bright: both (P0 | P1)
            rows += [p0, p1]
    else:
        for r in range(h):
            rows.append(pm_bits([op8[r, x] for x in range(op8.shape[1])]))
    return xoff, -s["ay"], double, h, rows


def write_banks(table, out_rooms, sprites, path):
    L = ["; generated by tools/gen_gfx.py - do not edit",
         "; bank 1: the room pictures; bank 2: the sprites", ""]
    def bytes_(v):
        return ["        .byte " + ", ".join(f"${x & 255:02X}" for x in v[i:i + 16]) for i in range(0, len(v), 16)]
    # ---- bank 1: the rooms' table (ops address, count, colours address) and
    # the draw lists; bank 2: the pieces, the rooms' band colours
    ops_data = []
    for i, ro in enumerate(out_rooms):
        v = []
        for o in ro["ops"]:
            lv = table[ro["pid"][o["piece"]]]
            if lv.size:
                v += [ro["pid"][o["piece"]], o["x"] // 2, o["y"] + 8, o["kind"]]
        ops_data.append(v)
    L += ['        .segment "GFX1"', "        .export _gfx_rooms", "_gfx_rooms:"]
    for i, v in enumerate(ops_data):
        L.append(f"        .word ops{i}, {len(v) // 4}, cols{i}")
    for i, v in enumerate(ops_data):
        L.append(f"ops{i}:")
        L += bytes_(v)
    tail2 = ["        .export _gfx_pieces", "_gfx_pieces:",
             "        .word " + ", ".join(f"pc{i}" for i in range(len(table)))]
    for i, lv in enumerate(table):
        wb, data = pack2(lv) if lv.size else (0, [])
        tail2.append(f"pc{i}:   .byte {lv.shape[0] if lv.size else 0}, {wb}")
        tail2 += bytes_(data)
    for i, ro in enumerate(out_rooms):
        tail2.append(f"cols{i}:")
        tail2 += bytes_([c for band in ro["cols"] for c in band])
    # ---- bank 2: characters (players) and the bitmap sprites
    # bank 2 starts with the blitters' tables (draw.s): a byte's mask, then
    # for shifts of 1..3 pixels its part in its own byte and in the next
    tabs = [((b | (b >> 1)) & 0x55) * 3 for b in range(256)]
    for s_ in (1, 2, 3):
        tabs += [b >> (2 * s_) for b in range(256)] + [(b << (8 - 2 * s_)) & 255 for b in range(256)]
    L += ["", '        .segment "GFX2"', "        .export _gfx_frames, _gfx_bmp",
          "; draw.s's tables: at $4000 (the segment's start)"] + bytes_(tabs) + tail2 + ["_gfx_frames:"]
    frames = []
    names = {n: i for i, n in enumerate(SPR_NAMES)}
    for m in range(3):
        for f in range(8):
            for k in range(3):
                frames.append(("walker", names[f"walker{m}_{f}_{k}"], m == 0))
    for kind in ("sage", "ghost", "bouncer"):
        for f in range(8):
            frames.append((kind, names[f"{kind}{f}"], False))
    L.append("        .word " + ", ".join(f"fr{i}" for i in range(len(frames))))
    for i, (_, si, multi) in enumerate(frames):
        xoff, yoff, dbl, h, rows = pmg_frame(sprites[si], multi)
        L.append(f"fr{i}:   .byte {h}, ${xoff & 255:02X}, ${yoff & 255:02X}, {int(dbl)}")
        L += bytes_(rows)
    bmp = ["crate", "lift"] + [f"piston{k}" for k in range(1, 9)] + ["relic0", "key0", "potion"]
    L.append("_gfx_bmp:")
    L.append("        .word " + ", ".join(f"bm{i}" for i in range(len(bmp))))
    for i, n in enumerate(bmp):
        s = sprites[names[n]]
        lv = sprite_levels(s)
        wb, data = pack2(lv)
        # h, wb, x offset (Atari pixels), y offset, then the rows
        L.append(f"bm{i}:   .byte {lv.shape[0]}, {wb}, ${(-(s['ax'] // 2)) & 255:02X}, ${(-s['ay']) & 255:02X}")
        L += bytes_(data)
    open(path, "w").write("\n".join(L) + "\n")
    print(f"wrote {path}: {len(frames)} player frames, {len(bmp)} bitmap sprites")


if __name__ == "__main__" and "--banks" in sys.argv:
    from rooms_io import read_sprites
    t, r = main()
    write_banks(t, r, read_sprites(os.path.join(HOST, "sprites.bin")), os.path.join(PROJ, "src", "gfx_data.s"))
