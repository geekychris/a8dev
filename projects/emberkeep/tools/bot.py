"""Emberkeep's test bot: plays from the game's state (gs, the grid, the
enemies; one read a frame) on the joystick.

It walks the shortest way (breadth-first over the tiles, walls and spikes
blocked) to the nearest rune, to an oil flask when the lantern is under
40%, then to the open portal. With `die_in` set it walks into the spikes
once it reaches that chamber (to lose its lives).
"""
import struct
from collections import deque

T_FLOOR, T_WALL, T_RUNE, T_PORTAL, T_OIL, T_HAZARD = range(6)
GM_TITLE, GM_PLAYING, GM_WIN, GM_LOSE, GM_LEVELWIN = range(5)


class State:
    def __init__(self, emu):
        self.gs = emu.addr("gs")
        self.grid = emu.addr("grid")
        self.e = {n: emu.addr(n) for n in ("e_x", "e_y", "e_min", "e_max", "e_dx", "e_dy", "e_axis", "e_alive")}

    def read(self, emu):
        # space="ram": main memory even while the game has a 130XE bank
        # mapped over $4000-$7FFF (where its variables are)
        g = emu.read_mem(self.gs, 29, space="ram")
        d = {"mode": g[0], "level": g[1], "lives": g[2], "runes": g[3], "runes_total": g[4],
             "score": struct.unpack_from("<I", g, 5)[0], "lantern": struct.unpack_from("<h", g, 9)[0],
             "px": struct.unpack_from("<h", g, 13)[0], "py": struct.unpack_from("<h", g, 15)[0],
             "enemies": g[25]}
        d["grid"] = emu.read_mem(self.grid, 300, space="ram")
        n = d["enemies"]
        w = {k: emu.read_mem(a, 12, space="ram") for k, a in self.e.items()}
        d["foes"] = [dict(x=struct.unpack_from("<h", w["e_x"], 2 * i)[0], y=struct.unpack_from("<h", w["e_y"], 2 * i)[0],
                          lo=struct.unpack_from("<h", w["e_min"], 2 * i)[0], hi=struct.unpack_from("<h", w["e_max"], 2 * i)[0],
                          dx=struct.unpack_from("<b", w["e_dx"], i)[0], dy=struct.unpack_from("<b", w["e_dy"], i)[0],
                          axis=w["e_axis"][i]) for i in range(n) if w["e_alive"][i]]
        return d


def bfs(grid, start, goal_kinds, avoid=()):
    """the first step (col, row) of the shortest way to a tile of goal_kinds"""
    sc, sr = start
    prev = {start: None}
    q = deque([start])
    while q:
        c, r = q.popleft()
        if grid[r * 20 + c] in goal_kinds and (c, r) != start:
            while prev[(c, r)] != start and prev[(c, r)] is not None:
                c, r = prev[(c, r)]
            return (c, r)
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c + dc, r + dr)
            if 0 <= n[0] < 20 and 0 <= n[1] < 15 and n not in prev:
                k = grid[n[1] * 20 + n[0]]
                if k in (T_WALL,) or (k == T_HAZARD and T_HAZARD not in goal_kinds) or n in avoid:
                    continue
                prev[n] = (c, r)
                q.append(n)
    return None


def foes_after(foes, t):
    """the enemies' places t frames on (the original's patrols)"""
    out = []
    for f in foes:
        x, y, dx, dy = f["x"], f["y"], f["dx"], f["dy"]
        for _ in range(t):
            if f["axis"] == 0:
                x += dx
                if x <= f["lo"]: x, dx = f["lo"], -dx
                if x >= f["hi"]: x, dx = f["hi"], -dx
            else:
                y += dy
                if y <= f["lo"]: y, dy = f["lo"], -dy
                if y >= f["hi"]: y, dy = f["hi"], -dy
        out.append((x, y))
    return out


def safe(foes, path):
    """path: the player's places frame by frame; no enemy within 13 pixels"""
    for t, (px, py) in enumerate(path):
        for ex, ey in foes_after(foes, t + 1):
            if abs(ex - px) <= 13 and abs(ey - py) <= 13:
                return False
    return True


class Bot:
    def __init__(self, die_in=None):
        self.die_in = die_in
        self.step_to = None

    def keys(self, s):
        if s["mode"] != GM_PLAYING:
            self.step_to = None
            return ""
        px, py, grid = s["px"], s["py"], s["grid"]
        # going to a tile: until the player stands on it
        if self.step_to:
            tx, ty = self.step_to[0] * 16, self.step_to[1] * 16
            if (px, py) != (tx, ty):
                return self.towards(px, py, tx, ty)
        here = (px + 8) // 16, (py + 8) // 16
        if self.die_in is not None and s["level"] >= self.die_in:
            goals = (T_HAZARD,)
        elif s["runes"]:
            goals = (T_RUNE, T_OIL) if s["lantern"] < 400 else (T_RUNE,)
        else:
            goals = (T_OIL, T_PORTAL) if s["lantern"] < 400 else (T_PORTAL,)
        nxt = bfs(grid, here, goals)
        if nxt is None:
            nxt = bfs(grid, here, (T_PORTAL, T_RUNE, T_OIL))
        if nxt is None:
            return ""
        # the enemies: step only if it's safe for the next 14 frames, or if
        # standing still isn't either
        if s["foes"] and goals != (T_HAZARD,):
            tx, ty = nxt[0] * 16, nxt[1] * 16
            go = [(px + max(-2 * (t + 1), min(2 * (t + 1), tx - px)), py + max(-2 * (t + 1), min(2 * (t + 1), ty - py)))
                  for t in range(14)]
            if not safe(s["foes"], go) and safe(s["foes"], [(px, py)] * 14):
                return ""
        self.step_to = nxt
        return self.towards(px, py, nxt[0] * 16, nxt[1] * 16)

    @staticmethod
    def towards(px, py, tx, ty):
        # off the grid on one axis (a frame with two steps went past):
        # straighten that first, or the walls block the other
        if py % 16 and px % 16 == 0 and px != tx:
            return "DOWN" if py < ty or (py == ty and py % 16 > 8) else "UP"
        if px % 16 and py % 16 == 0 and py != ty:
            return "RIGHT" if px < tx or (px == tx and px % 16 > 8) else "LEFT"
        if px < tx:
            return "RIGHT"
        if px > tx:
            return "LEFT"
        if py < ty:
            return "DOWN"
        if py > ty:
            return "UP"
        return ""


def step(emu, st, bot, held):
    s = st.read(emu)
    want = bot.keys(s)
    if want != held[0]:
        emu.release()
        if want:
            emu.hold(want)
        held[0] = want
    emu.run(1)
    return s
