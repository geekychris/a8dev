"""Sky Knights' player for the tests (test.py, make_gif.py): it reads the
game's arrays (one read of the BSS from b_x to gs) and holds the joystick.

Its game: pods first (before they hatch), else the nearest enemy: it flies
to 28 pixels above it, then drops onto it (the higher knight wins). Once
it has `target` points it gives up: it flies in level with the enemies or
below them until it has lost its lives."""
import struct

NB, NE, NG = 10, 8, 8
PLAYING, INTRO, GAMEOVER = 1, 2, 4


class World:
    def __init__(self, emu):
        self.base = emu.addr("b_x")
        self.size = emu.addr("gs") + 22 - self.base
        self.off = {n: emu.addr(n) - self.base for n in
                    ("b_x", "b_y", "b_dy", "b_px", "b_py", "p_alive", "p_lives", "e_active",
                     "g_x", "g_y", "g_active", "gs")}

    def read(self, emu):
        m = emu.read_mem(self.base, self.size)
        o = self.off

        def words(n, k):
            return list(struct.unpack_from(f"<{k}h", m, o[n]))

        def bytes_(n, k):
            return list(m[o[n]:o[n] + k])
        gs = struct.unpack_from("<2l7h", m, o["gs"])
        return {
            "px": words("b_px", NB), "py": bytes_("b_py", NB), "dy": words("b_dy", NB),
            "alive": bytes_("p_alive", 2), "lives": struct.unpack_from("<2b", m, o["p_lives"]),
            "e_active": bytes_("e_active", NE),
            "gx": [v >> 6 for v in words("g_x", NG)], "gy": [v >> 6 for v in words("g_y", NG)],
            "g_active": bytes_("g_active", NG),
            "score": gs[0], "wave": gs[2], "state": gs[3],
        }


def wrap_dx(a, b):
    """b - a the short way round (the screen wraps at 320 + 20)"""
    d = b - a
    if d > 170:
        d -= 340
    if d < -170:
        d += 340
    return d


class Bot:
    def __init__(self, target=3000):
        self.target = target
        self.t = 0
        self.give_up = False

    def keys(self, w):
        self.t += 1
        if w["state"] != PLAYING or not w["alive"][0]:
            return ""
        if w["score"] >= self.target:
            self.give_up = True
        px, py, dy = w["px"][0], w["py"][0], w["dy"][0]
        enemies = [(w["px"][2 + i], w["py"][2 + i]) for i in range(NE) if w["e_active"][i]]
        pods = [(w["gx"][i], w["gy"][i]) for i in range(NG) if w["g_active"][i]]
        if self.give_up:
            if not enemies:
                return ""
            tx, ty = min(enemies, key=lambda e: abs(wrap_dx(px, e[0])))
            ty += 8                                  # level with it, a little under
        elif pods and not self.give_up:
            tx, ty = min(pods, key=lambda g: abs(wrap_dx(px, g[0])))
            tx -= 6
            ty -= 14                                 # the pod's top: knight's box on it
        elif enemies:
            tx, ty = min(enemies, key=lambda e: abs(wrap_dx(px, e[0])) + abs(e[1] - py))
            dx = wrap_dx(px, tx)
            ty -= 6 if abs(dx) < 14 else 30          # above it, then down onto it
        else:
            return ""
        dx = wrap_dx(px, tx)
        if not self.give_up:
            # an enemy close by, level with it or higher: climb away from it
            for ex, ey in enemies:
                ddx = wrap_dx(px, ex)
                if abs(ddx) < 34 and ey <= py + 3 and py > 24:
                    dx, ty = -ddx, py - 40
                    break
        k = []
        if dx < -3:
            k.append("LEFT")
        elif dx > 3:
            k.append("RIGHT")
        if py > ty and dy > -150:
            k.append("FIRE")
        return "+".join(k)


def step(emu, world, bot, held):
    """one frame: the bot's keys held"""
    w = world.read(emu)
    want = bot.keys(w)
    if want != held[0]:
        emu.release()
        if want:
            emu.hold(want)
        held[0] = want
    emu.run(1)
    return w
