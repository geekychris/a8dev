#!/usr/bin/env python3
"""gen_view.py - Orbit Docking's station model for the Atari (src/view_tab.h).

The camera never yaws or pitches: it looks back down the station's axis and
only rolls. So every corner of the station lies on a plane across the axis
and is kept in polar form (radius, angle) on that plane: on the screen it
is the plane's centre plus the radius times the cosine and sine of its
angle plus the station's turn less the roll, scaled by the plane's
distance (src/proj.s).

The shapes and numbers are the original's render.c: the hub (an octagon of
radius 60 from z = 60 back to -300), its face, the slot and its three
lights, four arms out to the ring, the ring (16 segments, radius 260-300),
two solar panels. The polygons are listed far to near as the original's
depth sort puts them, with a pattern each (the original's pens as the
Atari's four colours and their checkers).

Also: the roll dial's needle (player 1) in 16 directions, the axis box
(player 2), the sight (player 0), the stars.
"""
import math
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "view_tab.h"

# planes: z, turns with the station
PLANES = [(60, 1), (61, 1), (-300, 1), (-60, 1), (-100, 1), (-90, 1), (150, 0), (260, 0), (420, 0)]
P_FACE, P_SLOT, P_BACK, P_RING, P_ARMB, P_PANEL, P_M0, P_M1, P_M2 = range(9)

# patterns: codes 0 BAK (space), 1 PF0 (mid grey), 2 PF1 (light grey), 3 PF2 (blue)


def pat(a, b=None):
    b = a if b is None else b
    return ((a << 6) | (b << 4) | (a << 2) | b, (b << 6) | (a << 4) | (b << 2) | a)


PATS = {
    "hub0": pat(2, 1), "hub1": pat(1), "face": pat(2), "slot": pat(0), "green": pat(3), "red": pat(3, 1),
    "arm": pat(1), "arm1": pat(1, 0), "panel0": pat(3), "panel1": pat(3, 0),
}

verts = []      # (plane, r, angle 0..255)
vindex = {}


def vert(plane, x, y):
    r = math.hypot(x, y)
    a = round(math.atan2(y, x) * 128 / math.pi) & 255
    key = (plane, round(r), a)
    if key not in vindex:
        vindex[key] = len(verts)
        verts.append(key)
    return vindex[key]


polys = []      # (vertex indices, pattern name), far to near
lods = []       # 0 always, 3 only near (small details)


def poly(plane_xy, name, lod=0):
    polys.append(([vert(p, x, y) for p, x, y in plane_xy], name))
    lods.append(lod)


def main():
    # the hub's sides (mean depth -120: behind the arms and ring in the original's sort)
    hub = []
    for k in range(8):
        a0 = (k * 8192 + 4096) / 65536 * 2 * math.pi
        a1 = a0 + math.pi / 4
        x0, y0 = 60 * math.cos(a0), 60 * math.sin(a0)
        x1, y1 = 60 * math.cos(a1), 60 * math.sin(a1)
        hub.append((x0, y0))
        poly([(P_FACE, x0, y0), (P_FACE, x1, y1), (P_BACK, x1, y1), (P_BACK, x0, y0)], "hub0" if k & 1 == 0 else "hub1")
    out_ring = None
    # the solar panels (z -90)
    for k in range(2):
        sg = -1 if k else 1
        poly([(P_PANEL, sg * 310, -40), (P_PANEL, sg * 470, -40), (P_PANEL, sg * 470, 40), (P_PANEL, sg * 310, 40)],
             "panel0" if k == 0 else "panel1")
    # the arms' sides (-60 to -100), then the ring and the arms' fronts (z -60)
    arms = []
    for k in range(4):
        c, s = math.cos(k * math.pi / 2), math.sin(k * math.pi / 2)
        ax, ay, bx, by = c * 60, s * 60, c * 260, s * 260
        px, py = -s * 10, c * 10
        poly([(P_RING, ax + px, ay + py), (P_RING, bx + px, by + py), (P_ARMB, bx + px, by + py), (P_ARMB, ax + px, ay + py)], "arm1")
        arms.append([(P_RING, ax + px, ay + py), (P_RING, bx + px, by + py), (P_RING, bx - px, by - py), (P_RING, ax - px, ay - py)])
    # port: the ring (radius 260-300 at z -60) is an annulus drawn a line at a
    # time (proj.s ring_draw), not 16 quads: it looks the same turned
    for a in arms:
        poly(a, "arm")
    # the face, the slot and its lights
    poly([(P_FACE, x, y) for x, y in hub], "face")
    poly([(P_SLOT, -24, -7), (P_SLOT, 24, -7), (P_SLOT, 24, 7), (P_SLOT, -24, 7)], "slot")
    poly([(P_SLOT, -34, -4), (P_SLOT, -28, -4), (P_SLOT, -28, 4), (P_SLOT, -34, 4)], "green", 3)
    poly([(P_SLOT, 28, -4), (P_SLOT, 34, -4), (P_SLOT, 34, 4), (P_SLOT, 28, 4)], "green", 3)
    poly([(P_SLOT, -3, 30), (P_SLOT, 3, 30), (P_SLOT, 3, 36), (P_SLOT, -3, 36)], "red", 3)
    # the approach markers' corners (squares of 68 round the axis, not turning)
    marks = []
    for p in (P_M0, P_M1, P_M2):
        marks.append([vert(p, 34 * sx, 34 * sy) for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1))])

    # winding: the original's quads are anticlockwise in world x-y seen from +z;
    # the view mirrors x (looking back down the axis), so they come out clockwise
    # on the screen as a3d wants. Check with the camera on the axis at z = 1000.
    def screen(i):
        p, r, a = verts[i]
        z = PLANES[p][0]
        th = a * math.pi / 128
        x, y = -r * math.cos(th), r * math.sin(th)
        d = 1000 - z
        return 80 + x * 115 / d, 44 - y * 98 / d

    out = ["/* view_tab.h - made by tools/gen_view.py: do not edit */", "#include <stdint.h>", ""]
    def area(idx):
        pts = [screen(i) for i in idx]
        return sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))

    # the flat parts face the camera: clockwise on the screen (a3d's front).
    # The sides (the hub's, the arms') are wound the other way round from the
    # original's quads, so that from the axis they are back faces
    flip = area(polys[[n for _, n in polys].index("face")][0]) < 0
    fixed = []
    for idx, name in polys:
        if name in ("hub0", "hub1", "arm1"):
            # the sides: seen from the axis in front of the station they face
            # away (their normals point out from the axis): back faces there
            if not flip:
                idx = idx[::-1]
        elif area(idx) < 0:
            idx = idx[::-1]
        fixed.append((idx, name))
    polys[:] = fixed
    print("flipped the original's winding:", flip)

    # the corners grouped by radius class (plane, radius), so proj.s sets the
    # multiplier once a class
    classes = sorted({(p, r) for p, r, _ in verts})
    order = sorted(range(len(verts)), key=lambda i: (classes.index(verts[i][:2]), i))
    new = {old: n for n, old in enumerate(order)}
    verts[:] = [verts[i] for i in order]
    polys[:] = [([new[i] for i in idx], name) for idx, name in polys]
    marks[:] = [[new[i] for i in m] for m in marks]
    assert len(verts) <= 64 + 64
    out.append(f"#define NPLANES {len(PLANES)}")
    out.append("const int16_t plane_z[NPLANES] = {" + ", ".join(str(z) for z, _ in PLANES) + "};")
    out.append("static const uint8_t plane_turns[NPLANES] = {" + ", ".join(str(t) for _, t in PLANES) + "};")
    out.append("/* the markers' corners (z 150, 260, 420) */")
    out.append("static const uint8_t mark_v[3][4] = {" + ", ".join("{" + ", ".join(map(str, m)) + "}" for m in marks) + "};")
    out.append(f"#define FACE_CLS {sorted({(p, r) for p, r, _ in verts}).index((P_FACE, 60))}")
    out.append(f"#define NVERTS {len(verts)}")
    out.append("/* the corners: plane, radius, angle (256 a turn) */")
    out.append("const uint8_t mv_plane[NVERTS] = {" + ", ".join(str(p) for p, _, _ in verts) + "};")
    out.append("const uint8_t mv_a[NVERTS] = {" + ", ".join(str(a) for _, _, a in verts) + "};")
    # the radii on each plane: a frame scales each once (port: not a corner at a time)
    out.append(f"#define NCLS {len(classes)}")
    starts = [min(i for i, v in enumerate(verts) if v[:2] == c) for c in classes] + [len(verts)]
    out.append("/* the corners of class c: cls_v0[c] .. cls_v0[c+1]-1 */")
    out.append("const uint8_t cls_v0[NCLS + 1] = {" + ", ".join(map(str, starts)) + "};")
    out.append("const uint8_t mv_cls[NVERTS] = {" + ", ".join(str(classes.index((p, r))) for p, r, _ in verts) + "};")
    out.append("const uint8_t cls_plane[NCLS] = {" + ", ".join(str(p) for p, _ in classes) + "};")
    out.append("const int16_t cls_r[NCLS] = {" + ", ".join(str(r) for _, r in classes) + "};")
    flat, starts = [], []
    for idx, name in polys:
        starts.append(len(flat))
        flat += [len(idx)] + idx
    out.append(f"#define NPOLYS {len(polys)}")
    out.append(f"#define POLYS_UNDER_RING {[n for _, n in polys].index('arm')}   /* the polygons behind the ring */")
    out.append("/* far to near: a count then the corners */")
    out.append(f"const uint8_t mp_list[{len(flat)}] = {{" + ", ".join(map(str, flat)) + "};")
    names = list(PATS)
    out.append("const uint8_t mp_lod[NPOLYS] = {" + ", ".join(map(str, lods)) + "};")
    out.append("const uint8_t mp_pat[NPOLYS] = {" + ", ".join(str(names.index(n)) for _, n in polys) + "};")
    out.append("const uint8_t pats[][2] = {" + ", ".join(f"{{0x{e:02X}, 0x{o:02X}}}" for e, o in PATS.values()) + "};")

    # ------------------------------------------------ players
    # the sight: a cross with a gap (player 0), 8 x 13 scan lines
    sight = [0x10] * 4 + [0] * 2 + [0xEE] + [0] * 2 + [0x10] * 4
    sight = [0x10, 0x10, 0x10, 0x10, 0, 0, 0xC6, 0, 0, 0x10, 0x10, 0x10, 0x10]
    out.append(f"static const uint8_t sight[{len(sight)}] = {{" + ", ".join(f"0x{v:02X}" for v in sight) + "};")
    # the dial (player 1, double width: 16 pixels, 24 scan lines): a ring, the
    # mark at the top, the needle in 16 directions (up = lined up)
    H = 24
    dial = []
    for n in range(16):
        a = n * 2 * math.pi / 16
        rows = []
        for j in range(H):
            v = 0
            for b in range(8):
                x = (b + 0.5 - 4) / 4.0
                y = (j + 0.5 - H / 2) / (H / 2)
                d = math.hypot(x, y)
                if 0.86 <= d <= 1.02:
                    v |= 0x80 >> b
                # the needle: from the middle along (sin a, -cos a)
                t = x * math.sin(a) - y * math.cos(a)
                w = abs(x * math.cos(a) + y * math.sin(a))
                if 0 <= t <= 0.8 and w < 0.16:
                    v |= 0x80 >> b
            rows.append(v)
        dial.append(rows)
    out.append(f"#define DIAL_H {H}")
    out.append("static const uint8_t dial[16][DIAL_H] = {" + ", ".join("{" + ", ".join(f"0x{v:02X}" for v in r) + "}" for r in dial) + "};")
    # the axis box (player 2, double width: 16 pixels, 28 scan lines): a frame and a cross
    BH = 28
    box = [0xFF if j in (0, 1, BH - 2, BH - 1, BH // 2 - 1, BH // 2) else 0x99 for j in range(BH)]
    out.append(f"#define BOX_H {BH}")
    out.append(f"static const uint8_t box[BOX_H] = {{" + ", ".join(f"0x{v:02X}" for v in box) + "};")

    # ------------------------------------------------ stars: (x, y) around the middle of the view
    import random
    rnd = random.Random(7)
    stars = []
    while len(stars) < 28:
        x, y = rnd.randint(-120, 120), rnd.randint(-120, 120)
        if abs(x) < 120 and abs(y) < 120:
            stars.append((x, y))
    out.append(f"#define NSTARS {len(stars)}")
    out.append("/* polar: radius (pixels), angle (256 a turn); proj.s turns them by the roll */")
    out.append("const uint8_t star_r[NSTARS] = {" + ", ".join(str(min(255, round(math.hypot(x, y)))) for x, y in stars) + "};")
    out.append("const uint8_t star_a[NSTARS] = {" + ", ".join(str(round(math.atan2(y, x) * 128 / math.pi) & 255) for x, y in stars) + "};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT}: {len(verts)} corners, {len(polys)} polygons")


if __name__ == "__main__":
    main()
