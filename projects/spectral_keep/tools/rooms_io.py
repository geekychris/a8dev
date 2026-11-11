"""read tools/export.c's room_<l>_<r>.bin (pieces + draw list) and sprites.bin"""
import struct
import numpy as np


def read_sprite(b, o):
    w, h, ax, ay = struct.unpack_from("<HHhh", b, o)
    o += 8
    px = np.frombuffer(b, dtype="<u2", count=w * h, offset=o).reshape(h, w) if w else np.zeros((0, 0), np.uint16)
    return {"w": w, "h": h, "ax": ax, "ay": ay, "px": px}, o + 2 * w * h


def read_room(path):
    b = open(path, "rb").read()
    n, = struct.unpack_from("<H", b, 0)
    o = 2
    pieces = []
    for _ in range(n):
        s, o = read_sprite(b, o)
        pieces.append(s)
    nops, = struct.unpack_from("<H", b, o)
    o += 2
    ops = []
    for _ in range(nops):
        p, k, x, y, cx, cy, cz = struct.unpack_from("<BBhhhhh", b, o)
        o += 12
        ops.append({"piece": p, "kind": k, "x": x, "y": y, "cell": (cx, cy, cz)})
    ox, oy = struct.unpack_from("<hh", b, o)
    return pieces, ops, (ox, oy)


def read_sprites(path):
    b = open(path, "rb").read()
    n, = struct.unpack_from("<H", b, 0)
    o = 2
    out = []
    for _ in range(n):
        s, o = read_sprite(b, o)
        out.append(s)
    return out


def rgb(px):
    r, g, b = ((px >> 10) & 31) * 255 / 31, ((px >> 5) & 31) * 255 / 31, (px & 31) * 255 / 31
    return np.dstack([r, g, b]).astype(np.float32)
