"""marble_tilt: the title's demo, tilting on the joystick, a level, a fall, game over.

1. The title: the autopilot rolls the demo; RIGHT/LEFT choose the level,
   FIRE starts it.
2. The joystick: RIGHT tilts the board right and the marble rolls right,
   UP tilts it away and the marble rolls away. START pauses.
3. The autopilot (B, the original's bot) takes level 1 to the exit: the
   score, then level 2 (The Moat).
4. On level 2 the stick (autopilot off) rolls the marble into the moat: a
   fall, 3 seconds off the clock, back at the start.
5. The clock runs out (the test sets it to 2 seconds): game over, the title.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "marble_tilt.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Marble:
    OFS_X = 4 + 12 * 16                  # MTGame: level, over, won, between, cell[12][16], x, z, vx, vz, roll, pitch

    def __init__(self, emu):
        self.emu = emu
        self.G = emu.addr("_G")

    def probe(self):
        p = self.emu.read_mem("_mt_probe", 9)
        return {"state": p[0], "level": p[1], "falls": p[2], "ap": p[3], "over": p[4], "between": p[5],
                "gates": p[6], "paused": p[7], "pick": p[8]}

    def marble(self):
        x, z, vx, vz, roll, pitch = struct.unpack("<hhhhbb", self.emu.read_mem(self.G + self.OFS_X, 10))
        return {"x": x, "z": z, "vx": vx, "vz": vz, "roll": roll, "pitch": pitch}

    def time_left(self):
        # after falling, cp_row, cp_col, gates_open, falls, bumps
        return struct.unpack("<h", self.emu.read_mem(self.G + self.OFS_X + 10 + 4 + 4, 2))[0]

    def wait(self, cond, frames):
        for _ in range(frames // 10):
            p = self.probe()
            if cond(p):
                return p
            self.emu.run(10)
        return self.probe()


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("mt: title", 4000))
    check("ready", emu.run_until("mt: ready", 200))
    g = Marble(emu)
    emu.run(150)
    check("the title's demo rolls (autopilot on)", g.probe()["ap"] == 1 and g.probe()["state"] == 0)
    m0 = g.marble()
    emu.run(100)
    m1 = g.marble()
    check("the demo marble moves", (m0["x"], m0["z"]) != (m1["x"], m1["z"]), f"{m0} {m1}")
    emu.screenshot(HERE / "build" / "t_title.png")

    emu.press("RIGHT", hold_frames=4)
    emu.run(20)
    check("RIGHT picks level 2", g.probe()["pick"] == 2, str(g.probe()))
    emu.press("LEFT", hold_frames=4)
    emu.run(20)
    check("LEFT back to level 1", g.probe()["pick"] == 1, str(g.probe()))
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE starts level 1", emu.run_until("mt: start level=1", 300))
    p = g.wait(lambda p: p["state"] == 1, 200)
    check("playing level 1, autopilot off", p["state"] == 1 and p["level"] == 1 and p["ap"] == 0, str(p))
    t0 = g.time_left()

    # the joystick: S is at the far left corner, with floor to the right and towards you
    m0 = g.marble()
    emu.hold("RIGHT")
    emu.run(30)
    emu.release()
    m1 = g.marble()
    print(f"       RIGHT: roll {m0['roll']} -> {m1['roll']}, x {m0['x']} -> {m1['x']}")
    check("RIGHT tilts right, the marble rolls right", m1["roll"] > 0 and m1["x"] > m0["x"])
    emu.run(40)
    m0 = g.marble()
    emu.hold("DOWN")
    emu.run(30)
    m1 = g.marble()
    emu.release()
    print(f"       DOWN: pitch {m0['pitch']} -> {m1['pitch']}, z {m0['z']} -> {m1['z']}")
    check("DOWN tilts towards you, the marble rolls nearer", m1["pitch"] < 0 and m1["z"] < m0["z"])
    emu.hold("UP")
    emu.run(30)
    emu.release()
    check("UP tilts away", g.marble()["pitch"] > 0)
    emu.screenshot(HERE / "build" / "t_play.png")
    check("the clock runs", g.time_left() < t0)

    emu.log()
    emu.press("START", hold_frames=4)
    check("START pauses", emu.run_until("mt: paused=1", 50))
    t1 = g.time_left()
    emu.run(60)
    check("the clock stops while paused", g.time_left() == t1)
    emu.press("START", hold_frames=4)
    check("START again: playing", emu.run_until("mt: paused=0", 50))

    # the autopilot to the exit
    emu.log()
    emu.type("B")
    check("B: the autopilot", emu.run_until("mt: autopilot=1", 100))
    check("the exit (score)", emu.run_until("mt: exit level=1", 4000), emu.log_text(0)[-300:])
    print("       " + " | ".join(l for l in emu.debug_log if "exit" in l)[-120:])
    emu.screenshot(HERE / "build" / "t_exit.png")
    check("level 2", emu.run_until("mt: level=2", 300))
    p = g.wait(lambda p: p["between"] == 0, 300)
    check("on level 2", p["level"] == 2 and p["state"] == 1, str(p))

    # into the moat: S (1,1), holes at (2,2)...
    emu.log()
    emu.type("B")
    check("B: the autopilot off", emu.run_until("mt: autopilot=0", 100))
    t1 = g.time_left()
    emu.hold(["DOWN", "RIGHT"])
    fell = emu.run_until("mt: fall", 400)
    emu.release()
    check("a fall", fell, emu.log_text(0)[-300:])
    check("a fall costs 3 seconds", g.time_left() <= t1 - 75, f"{t1} -> {g.time_left()}")
    p = g.wait(lambda p: p["falls"] == 1, 50)
    check("counted", p["falls"] == 1, str(p))
    emu.run(4)
    emu.screenshot(HERE / "build" / "t_fall.png")
    check("back at the start", emu.run_until("mt: respawn", 200))

    # the clock runs out
    emu.write_mem(g.G + Marble.OFS_X + 18, struct.pack("<h", 50))
    check("time up: game over", emu.run_until("mt: gameover", 300))
    p = g.wait(lambda p: p["state"] == 2, 100)
    check("the game-over state", p["state"] == 2 and p["over"] & 1, str(p))
    emu.screenshot(HERE / "build" / "t_over.png")
    emu.run(150)
    emu.press("FIRE", hold_frames=4)
    check("back to the title", emu.run_until("mt: title", 300))
    ticks = [l for l in emu.debug_log if "ticks5s" in l]
    print(f"       {ticks[-3:]}")
    if failures:
        print("\n".join(emu.debug_log[-20:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("800XL: runs (no banks)", emu.run_until("mt: ready", 4000))

print(f"{'FAIL' if failures else 'PASS'}: marble_tilt")
sys.exit(1 if failures else 0)
