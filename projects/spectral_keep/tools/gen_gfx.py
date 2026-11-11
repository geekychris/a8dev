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


if __name__ == "__main__":
    main()
