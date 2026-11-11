"""The courses drawn whole through the original's camera (rsview.py): from
the Spectrum Next port's tools/gen_course.py (render, faces, path_points,
decor), which follows the original's render.c."""
import os
import numpy as np
import rsview
from rsview import FX

BG = (1 << 10) | (1 << 5) | 3            # render_begin(): the void the courses float in


def path_points(c, cam, step=FX // 2):
    """the route, every half unit: (screen x, y in pixels, depth)"""
    pts = []
    for a, b in zip(c.path, c.path[1:]):
        n = max(1, int(max(abs(b[0] - a[0]), abs(b[1] - a[1]), abs(b[2] - a[2])) // step))
        for k in range(n):
            p = [a[i] + (b[i] - a[i]) * k // n for i in range(3)]
            x, y, d = cam.proj(p)
            pts.append((x / 65536, y / 65536, d))
    return np.array(pts)


def faces(c, cam):
    """course quads facing the camera: (xs, ys, zs, key, quad index)"""
    out = []
    for qi, (vs, col, flags, nw) in enumerate(c.q):
        nx = ((nw << 2) & 0xFFFFFFFF)
        nx = (nx - (1 << 32) if nx & 0x80000000 else nx) >> 22
        ny = ((nw << 12) & 0xFFFFFFFF)
        ny = (ny - (1 << 32) if ny & 0x80000000 else ny) >> 22
        nz = ((nw << 22) & 0xFFFFFFFF)
        nz = (nz - (1 << 32) if nz & 0x80000000 else nz) >> 22
        if nx * cam.f[0] + ny * cam.f[1] + nz * cam.f[2] >= 0:
            continue
        P = [cam.proj(c.v[i]) for i in vs]
        xs, ys, zs = [p[0] for p in P], [p[1] for p in P], [p[2] for p in P]
        cr = ((xs[1] - xs[0]) >> 8) * ((ys[3] - ys[0]) >> 8) - ((ys[1] - ys[0]) >> 8) * ((xs[3] - xs[0]) >> 8)
        if cr <= 0:
            continue
        if (flags & 255) == 2:
            zs = [z - 205 for z in zs]
        out.append((xs, ys, zs, max(p[2] for p in P), qi))
    return out


def render(c, cam=None):
    cam = cam or rsview.Cam()
    fl = faces(c, cam)
    xs = [x for f in fl for x in f[0]]
    ys = [y for f in fl for y in f[1]]
    ox, oy = (min(xs) >> 16) - 8, (min(ys) >> 16) - 8
    w, h = (max(xs) >> 16) - ox + 9, (max(ys) >> 16) - oy + 9
    cv = rsview.Canvas(w, h, ox, oy, BG)
    pp = path_points(c, cam)
    for xs_, ys_, zs, key, qi in fl:
        vs, col, flags, nw = c.q[qi]
        if flags >> 8:
            continue                       # crumbling tiles: sprites on the Next
        cx, cy = sum(xs_) / 4 / 65536, sum(ys_) / 4 / 65536
        k = np.argmin((pp[:, 0] - cx) ** 2 + (pp[:, 1] - cy) ** 2)
        rel = key - pp[k, 2]               # port: the fog's depth from the route nearest on screen
        lv = int(rel - 4 * FX) >> 14
        cv.quad(xs_, ys_, zs, rsview.fog_col(col, lv), qi + 1)
    decor(c, cam, cv, pp)
    return cv


SPHERES = {}


def decor(c, cam, cv, pp):
    """render.c's decor() at clock 0. port: the floating scenery holds still
    (the original bobs and turns it); it is in the picture"""
    for d in c.deco:
        if len(d["v"]) > 256:
            continue
        sc = d["scale"]
        ang = (d["phase"] * 10430) >> 12
        bob = (rsview.isin(ang) * (d["amp"] >> 2)) >> 12
        yaw = rsview.c_div(d["yaw"] * 2, 45)
        cs, sn = rsview.icos(yaw) >> 2, rsview.isin(yaw) >> 2
        pos = (d["x"], d["y"] + bob, d["z"])
        P = []
        for v in d["v"]:
            lx, ly, lz = (v[0] * sc) >> 12, (v[1] * sc) >> 12, (v[2] * sc) >> 12
            P.append(cam.proj((pos[0] + ((lx * cs + lz * sn) >> 12), pos[1] + ly, pos[2] + ((-lx * sn + lz * cs) >> 12))))
        for q in d["q"]:
            xs = [P[k][0] for k in q[:4]]
            ys = [P[k][1] for k in q[:4]]
            zs = [P[k][2] for k in q[:4]]
            cr = ((xs[1] - xs[0]) >> 8) * ((ys[3] - ys[0]) >> 8) - ((ys[1] - ys[0]) >> 8) * ((xs[3] - xs[0]) >> 8)
            if cr <= 0:
                continue
            key = max(zs)
            k = np.argmin((pp[:, 0] - sum(xs) / 4 / 65536) ** 2 + (pp[:, 1] - sum(ys) / 4 / 65536) ** 2)
            lv = int(key - pp[k, 2] - 4 * FX) >> 14
            cv.quad(xs, ys, zs, rsview.fog_col([x & 0xFFFFFFFF for x in q[4:7]], lv), 30000)
        for s in d["s"]:
            lx, lz = (s[0] * sc) >> 12, (s[2] * sc) >> 12
            w = (pos[0] + ((lx * cs + lz * sn) >> 12), pos[1] + ((s[1] * sc) >> 12), pos[2] + ((-lx * sn + lz * cs) >> 12))
            r = (s[3] * sc) >> 12
            look = s[4] if s[4] < 8 else rsview.SP_CRYSTAL
            if look not in SPHERES:
                SPHERES[look] = rsview.sphere(look)
            x, y, dz = cam.proj(w)
            cv.sprite(SPHERES[look], x, y, (r * cam.scale) >> 4, dz - r, 30001)
