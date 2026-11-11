"""orbit_docking: the title's demo, the thrusters, a dock, a crash, game over.

1. The title: the autopilot flies the demo; RIGHT/LEFT choose the station,
   FIRE starts station 1.
2. The thrusters: the stick moves the craft across, W pushes it in, FIRE +
   RIGHT rolls it; each burns fuel. START pauses (nothing moves) and starts
   again.
3. The autopilot (B, the original's bot) docks at station 1 (the score) and
   station 2 comes up.
4. Autopilot off, W held: the craft rams the station: a crash, a life.
5. One life left, rammed again: game over, FIRE, the title.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "orbit_docking.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Game:
    # ODGame: seed, rng, level, lives, over, won, between, x, y, z, xf, yf, zf, vx, vy, vz,
    # roll, roll_rate, st_angle, st_spin, fuel, fuel_max, ...
    OFS = 8

    def __init__(self, emu):
        self.emu = emu
        self.G = emu.addr("_G")

    def probe(self):
        p = self.emu.read_mem("_od_probe", 11)
        return {"state": p[0], "level": p[1], "lives": p[2], "ap": p[3], "over": p[4], "between": p[5],
                "paused": p[6], "pick": p[7], "docks": p[8], "crashes": p[9], "fail": p[10]}

    def craft(self):
        b = self.emu.read_mem(self.G + self.OFS, 5 + 6 + 3 + 6 + 16 + 4)
        x, y, z = struct.unpack("<3h", b[5:11])
        vx, vy, vz = struct.unpack("<3h", b[14:20])
        roll, rr, st, sts = struct.unpack("<4i", b[20:36])
        fuel, fmax = struct.unpack("<2H", b[36:40])
        return {"x": x, "y": y, "z": z, "vx": vx, "vy": vy, "vz": vz, "roll_rate": rr, "fuel": fuel}

    def wait(self, cond, frames):
        for _ in range(frames // 10):
            p = self.probe()
            if cond(p):
                return p
            self.emu.run(10)
        return self.probe()


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("od: title", 4000))
    check("ready", emu.run_until("od: ready", 200))
    g = Game(emu)
    emu.run(50)
    a = g.craft()
    emu.run(150)
    b = g.craft()
    check("the title's demo flies (autopilot, the craft closes in)",
          g.probe()["ap"] == 1 and g.probe()["state"] == 0 and b["z"] < a["z"], f"{a} {b}")
    emu.screenshot(HERE / "build" / "t_title.png")

    emu.press("RIGHT", hold_frames=4)
    emu.run(20)
    check("RIGHT picks station 2", g.probe()["pick"] == 2, str(g.probe()))
    emu.press("LEFT", hold_frames=4)
    emu.run(20)
    check("LEFT back to station 1", g.probe()["pick"] == 1, str(g.probe()))
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE starts station 1", emu.run_until("od: start station=1", 300))
    p = g.wait(lambda p: p["state"] == 1, 200)
    check("flying at station 1, autopilot off, 3 lives", p["state"] == 1 and p["level"] == 1 and p["ap"] == 0
          and p["lives"] == 3, str(p))
    emu.run(20)

    # the thrusters
    a = g.craft()
    emu.hold("RIGHT")
    emu.run(20)
    emu.release()
    b = g.craft()
    check("the stick moves the craft across (fuel burnt)", (b["vx"], b["vy"]) != (a["vx"], a["vy"]) and b["fuel"] < a["fuel"],
          f"{a} {b}")
    emu.hold("W")
    emu.run(20)
    emu.release()
    c = g.craft()
    check("W pushes it in", c["vz"] < b["vz"], f"{b} {c}")
    emu.hold(["FIRE", "RIGHT"])
    emu.run(20)
    emu.release()
    d = g.craft()
    check("FIRE + RIGHT rolls it", d["roll_rate"] > c["roll_rate"], f"{c} {d}")
    emu.screenshot(HERE / "build" / "t_fly.png")

    emu.log()
    emu.press("START", hold_frames=4)
    check("START pauses", emu.run_until("od: paused=1", 100))
    a = g.craft()
    emu.run(50)
    check("nothing moves while paused", g.craft() == a)
    emu.press("START", hold_frames=4)
    check("START again: flying", emu.run_until("od: paused=0", 100))

    # the autopilot docks
    emu.log()
    emu.type("B")
    check("B: the autopilot", emu.run_until("od: autopilot=1", 100))
    check("docked at station 1 (score)", emu.run_until("od: dock station=1", 6000), emu.log_text(0)[-300:])
    emu.run(4)
    emu.screenshot(HERE / "build" / "t_dock.png")
    check("station 2", emu.run_until("od: station=2", 300))
    emu.type("B")
    check("B: the autopilot off", emu.run_until("od: autopilot=0", 100))
    p = g.wait(lambda p: p["between"] == 0, 300)
    check("at station 2 with a docking", p["level"] == 2 and p["docks"] == 1 and p["state"] == 1, str(p))

    # ram the station
    emu.log()
    emu.hold("W")
    crashed = emu.run_until("od: crash", 4000)
    emu.release()
    check("W held: a crash", crashed, emu.log_text(0)[-300:])
    p = g.wait(lambda p: p["crashes"] == 1, 50)
    check("a life gone", p["lives"] == 2 and p["crashes"] == 1, str(p))
    emu.run(10)
    emu.screenshot(HERE / "build" / "t_crash.png")

    # the last life
    emu.write_mem(g.G + 9, bytes([1]))
    emu.run(60)
    emu.log()
    emu.hold("W")
    over = emu.run_until("od: gameover", 4000)
    emu.release()
    check("the last life: game over", over)
    p = g.wait(lambda p: p["state"] == 2, 100)
    check("the game-over state", p["state"] == 2 and p["over"] & 1, str(p))
    emu.screenshot(HERE / "build" / "t_over.png")
    emu.run(150)
    emu.press("FIRE", hold_frames=4)
    check("back to the title", emu.run_until("od: title", 300))
    fps = [l for l in emu.debug_log if "ticks5s" in l]
    print(f"       {fps[-3:]}")
    if failures:
        print("\n".join(emu.debug_log[-20:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("800XL: runs (no banks)", emu.run_until("od: ready", 4000))

print(f"{'FAIL' if failures else 'PASS'}: orbit_docking")
sys.exit(1 if failures else 0)
