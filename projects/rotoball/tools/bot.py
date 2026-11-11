"""Rotoball's test bot: plays P1 (joystick 1) from the game's state (one
read of its variables a frame), the ZX Spectrum port's tactic: it first
lets the CPU score (out of its way to the sideline), then waits for the
CPU 15 units into its own half, 3.5 units to the side of its path (tried
against the game with different starts: it wins 9-5 to 12-2), so the CPU runs into it and the tackle takes the
ball, and carries the ball
into the CPU's goal, swerving round the CPU.
"""
import struct

ONE = 256
PLAYING, FULLTIME = 1, 2
C_FWD, C_BACK, C_LEFT, C_RIGHT = "UP", "DOWN", "LEFT", "RIGHT"


class State:
    def __init__(self, emu):
        self.a = {n: emu.addr(n) for n in ("sk_x", "sk_z", "ball_x", "ball_z", "ball_carrier", "G")}

    def read(self, emu):
        x = struct.unpack("<2h", emu.read_mem(self.a["sk_x"], 4))
        z = struct.unpack("<2h", emu.read_mem(self.a["sk_z"], 4))
        b = struct.unpack("<2h", emu.read_mem(self.a["ball_x"], 2) + emu.read_mem(self.a["ball_z"], 2))
        c = struct.unpack("<b", emu.read_mem(self.a["ball_carrier"], 1))[0]
        g = emu.read_mem(self.a["G"], 12)
        return {"p1": (x[0], z[0]), "p2": (x[1], z[1]), "ball": b, "carrier": c,
                "score": (g[0], g[1]), "left": g[2] | g[3] << 8, "state": g[6], "players": g[7]}


class Bot:
    WAIT_X = -15 * ONE
    WAIT_DZ = 7 * ONE // 2

    def __init__(self, concede=True):
        self.phase = "concede" if concede else "score"

    def keys(self, p):
        if p["state"] != PLAYING:
            return ""
        if self.phase == "concede":
            if p["score"][1] >= 1:
                self.phase = "score"
                return ""
            return C_LEFT if p["p1"][1] < 30 * ONE else ""
        (x, z), (ox, oz), (bx, bz) = p["p1"], p["p2"], p["ball"]
        k = []
        if p["carrier"] == 0:
            k.append(C_FWD)
            if ox > x and abs(oz - z) < 12 * ONE and ox - x < 60 * ONE:
                go_minus = oz > z if abs(z) < 30 * ONE else z > 0
                k.append(C_RIGHT if go_minus else C_LEFT)
            return "+".join(k)
        tz = (oz + self.WAIT_DZ) if p["carrier"] == 1 else bz + self.WAIT_DZ
        if tz < z - ONE:
            k.append(C_RIGHT)
        elif tz > z + ONE:
            k.append(C_LEFT)
        if x < self.WAIT_X - ONE:
            k.append(C_FWD)
        elif x > self.WAIT_X + ONE:
            k.append(C_BACK)
        return "+".join(k)


def step(emu, st, bot, held):
    """one frame with the bot's joystick"""
    p = st.read(emu)
    want = bot.keys(p)
    if want != held[0]:
        emu.release()
        if want:
            emu.hold(want)
        held[0] = want
    emu.run(1)
    return p
