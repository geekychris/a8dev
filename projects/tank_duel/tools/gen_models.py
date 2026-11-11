#!/usr/bin/env python3
"""gen_models.py - Tank Duel's models for the Atari (src/models.h).

The original's models (render.c render_init: amiga3d's mdl_box, mdl_prism,
mdl_pyramid), in model units of 2 world units (int8, a3d_oshift 1):
x across, y up, z ahead (the model's front). Faces are listed clockwise as
seen from outside (a3d's front), far parts first, each with a pattern:
ANTIC mode D's colours 1-3 and their checkers with the background (the sky
and floor, set line by line), lit or shaded as the original's pens.

port: the pillar is a square column (the original: an 8-sided prism); the
tanks' tracks and the scout's long barrel are left out (the barrel is a
line); far tanks are drawn as their hull only.
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "models.h"


def pat(a, b=None):
    b = a if b is None else b
    return ((a << 6) | (b << 4) | (a << 2) | b, (b << 6) | (a << 4) | (b << 2) | a)


# colours: 1 grey (walls, pillars, the bulwark), 2 orange-brown (crates,
# pyramids, the mortar), 3 green (the scout, shields)
LIT = {1: pat(1), 2: pat(2), 3: pat(3)}
SHADE = {1: pat(1, 0), 2: pat(2, 0), 3: pat(3, 0)}
MIX = {13: pat(1, 3), 12: pat(1, 2), 23: pat(2, 3)}


class Model:
    def __init__(self, name):
        self.name, self.v, self.f = name, [], []

    def vert(self, x, y, z):
        p = (round(x / 2), round(y / 2), round(z / 2))
        assert all(-128 <= c <= 127 for c in p), (self.name, p)
        if p in self.v:
            return self.v.index(p)
        self.v.append(p)
        return len(self.v) - 1

    def face(self, pts, pattern):
        self.f.append(([self.vert(*p) for p in pts], pattern))

    def box(self, x0, x1, y0, y1, z0, z1, lit, shade, top=True):
        """the sides and the top, clockwise seen from outside (y up, z ahead)"""
        self.face([(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)], lit)       # the back (-z)
        self.face([(x1, y1, z1), (x0, y1, z1), (x0, y0, z1), (x1, y0, z1)], lit)       # the front (+z)
        self.face([(x0, y1, z1), (x0, y1, z0), (x0, y0, z0), (x0, y0, z1)], shade)     # left (-x)
        self.face([(x1, y1, z0), (x1, y1, z1), (x1, y0, z1), (x1, y0, z0)], shade)     # right (+x)
        if top:
            self.face([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], lit)   # the top

    def pyramid(self, x0, x1, z0, z1, h, lit, shade):
        a = ((x0 + x1) / 2, h, (z0 + z1) / 2)
        self.face([a, (x1, 0, z0), (x0, 0, z0)], lit)
        self.face([a, (x0, 0, z1), (x1, 0, z1)], lit)
        self.face([a, (x0, 0, z0), (x0, 0, z1)], shade)
        self.face([a, (x1, 0, z1), (x1, 0, z0)], shade)


def models():
    ms = []
    m = Model("pillar")
    m.box(-26, 26, 0, 130, -26, 26, LIT[1], SHADE[1], top=False)
    ms.append(m)
    m = Model("pyramid")
    m.pyramid(-50, 50, -50, 50, 110, LIT[2], SHADE[2])
    ms.append(m)
    m = Model("crate")
    m.box(-34, 34, 0, 56, -34, 34, LIT[2], SHADE[2], top=False)
    ms.append(m)
    m = Model("crate_cracked")
    m.box(-34, 34, 0, 56, -34, 34, SHADE[2], pat(2, 1), top=False)
    ms.append(m)
    # the tanks: hull, turret (the barrel is a line, drawn apart)
    m = Model("scout")
    m.box(-18, 18, 2, 16, -26, 24, LIT[3], SHADE[3])
    m.box(-9, 9, 16, 26, -10, 8, LIT[3], SHADE[3])
    ms.append(m)
    m = Model("mortar")
    m.box(-22, 22, 0, 22, -26, 26, LIT[2], SHADE[2])
    m.box(-12, 12, 22, 30, -14, 10, LIT[2], SHADE[2])
    m.box(-6, 6, 30, 58, -8, 4, SHADE[2], SHADE[2], top=False)
    ms.append(m)
    m = Model("bulwark")                                    # seen from in front: the shield last
    m.box(-26, 26, 0, 26, -28, 22, LIT[1], SHADE[1])
    m.box(-13, 13, 26, 38, -14, 10, LIT[1], SHADE[1])
    m.box(-32, 32, 2, 40, 26, 32, LIT[3], MIX[13])          # the shield
    ms.append(m)
    m = Model("bulwark_rear")                               # seen from behind: the shield first
    m.box(-32, 32, 2, 40, 26, 32, LIT[3], MIX[13])
    m.box(-26, 26, 0, 26, -28, 22, LIT[1], SHADE[1])
    m.box(-13, 13, 26, 38, -14, 10, LIT[1], SHADE[1])
    ms.append(m)
    # far tanks: the hull alone
    for name, src in (("scout_far", (-18, 18, 2, 16, -26, 24, 3)), ("mortar_far", (-22, 22, 0, 22, -26, 26, 2)),
                      ("bulwark_far", (-26, 26, 0, 26, -28, 22, 1))):
        m = Model(name)
        x0, x1, y0, y1, z0, z1, c = src
        m.box(x0, x1, y0, y1, z0, z1, LIT[c], SHADE[c])
        if name == "bulwark_far":                           # the shield shows what it faces
            m.box(-32, 32, 2, 40, 26, 32, LIT[3], MIX[13], top=False)
        ms.append(m)
    return ms


def main():
    ms = models()
    out = ["/* models.h - made by tools/gen_models.py: do not edit */", "#include <stdint.h>", ""]
    names = [m.name for m in ms]
    for m in ms:
        assert len(m.v) <= 63, m.name
        out.append(f"static const int8_t mv_{m.name}[] = {{" + ", ".join(f"{x}, {y}, {z}" for x, y, z in m.v) + "};")
        flat = []
        for idx, (pe, po) in m.f:
            flat += [len(idx), pe, po] + idx
        flat.append(0)
        out.append(f"static const uint8_t mf_{m.name}[] = {{" + ", ".join(map(str, flat)) + "};")
    out.append(f"#define NMODELS {len(ms)}")
    out.append("enum { " + ", ".join(f"M_{n.upper()}" for n in names) + " };")
    out.append("static const int8_t *const model_v[NMODELS] = {" + ", ".join(f"mv_{n}" for n in names) + "};")
    out.append("static const uint8_t model_nv[NMODELS] = {" + ", ".join(str(len(m.v)) for m in ms) + "};")
    out.append("/* faces: a count, the pattern (even, odd line), the corners; 0 ends */")
    out.append("static const uint8_t *const model_f[NMODELS] = {" + ", ".join(f"mf_{n}" for n in names) + "};")
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT}: " + ", ".join(f"{m.name} {len(m.v)}v {len(m.f)}f" for m in ms))


if __name__ == "__main__":
    main()
