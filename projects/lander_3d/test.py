"""lander_3d: the title's demo, flying on the joystick, a landing, crashes, game over.

1. The title: the autopilot flies the demo; RIGHT/LEFT choose the world,
   FIRE starts the Moon.
2. The joystick: FIRE (full thrust) slows the fall, UP tips the craft
   forward, RIGHT tips it right, SELECT sets a steady throttle.
3. The autopilot (B, the original's autopilot hook) lands on a pad:
   the score, then the next level.
4. Three crashes (no thrust): a ship each, then game over and the title.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "lander_3d.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Lander:
    def __init__(self, emu):
        self.emu = emu
        self.L = emu.addr("_L")

    def probe(self):
        p = self.emu.read_mem("_ld_probe", 8)
        return {"state": p[0], "outcome": p[1], "ap": p[2], "ships": p[3], "level": p[4], "world": p[5],
                "throttle": p[6], "paused": p[7]}

    def vy(self):
        return struct.unpack("<h", self.emu.read_mem("_ld_v88", 6)[2:4])[0] / 256

    def attitude(self):
        return struct.unpack("<hh", self.emu.read_mem(self.L + 7 + 992 + 21, 4))   # pitch, roll (after h and the pads)

    def wait(self, cond, frames):
        for _ in range(frames // 10):
            p = self.probe()
            if cond(p):
                return p
            self.emu.run(10)
        return self.probe()


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("ld: title", 4000))
    check("ready", emu.run_until("ld: ready", 200))
    g = Lander(emu)
    emu.run(150)
    emu.screenshot(HERE / "build" / "t_title.png")
    check("the title's demo flies (autopilot on)", g.probe()["ap"] == 1)

    emu.press("RIGHT", hold_frames=4)
    emu.run(20)
    emu.press("LEFT", hold_frames=4)
    emu.run(20)
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE starts the Moon", emu.run_until("ld: start world=MOON", 1200))
    p = g.wait(lambda p: p["state"] == 1, 400)
    check("playing, 3 ships, level 1", p["state"] == 1 and p["ships"] == 3 and p["level"] == 1, str(p))

    # the joystick
    emu.run(40)
    v0 = g.vy()
    emu.run(60)
    v1 = g.vy()
    emu.hold("FIRE")
    emu.run(80)
    v2 = g.vy()
    emu.release()
    print(f"       vertical speed: {v0:.1f} -> {v1:.1f} falling, {v2:.1f} after thrust")
    check("falls without thrust", v1 < v0)
    check("FIRE thrusts upwards", v2 > v1)
    a0 = g.attitude()
    emu.hold("UP")
    emu.run(30)
    emu.release()
    emu.hold("RIGHT")
    emu.run(30)
    emu.release()
    emu.run(10)
    a1 = g.attitude()
    print(f"       pitch, roll: {a0} -> {a1}")
    check("UP tips forward, RIGHT tips right", a1[0] > a0[0] and a1[1] < a0[1])
    emu.press("SELECT", hold_frames=4)
    emu.run(30)
    check("SELECT: a steady throttle", g.probe()["throttle"] > 0)
    emu.screenshot(HERE / "build" / "t_fly.png")

    # the autopilot lands
    emu.log()
    emu.type("B")
    check("B: the autopilot", emu.run_until("ld: autopilot=1", 100))
    landed = emu.run_until("ld: landed", 30000)
    emu.run(5)
    log = "\n".join(emu.debug_log)
    print("       " + " | ".join(l for l in emu.debug_log if "landed" in l or "total=" in l)[-200:])
    check("lands (score)", landed and re.search(r"ld: (landed|offpad).*total=[1-9]", log) is not None,
          log[-300:])
    emu.screenshot(HERE / "build" / "t_landed.png")
    check("the next level", emu.run_until("ld: next level=2", 400))
    p = g.wait(lambda p: p["state"] == 1, 600)
    check("level 2", p["level"] == 2, str(p))

    # three crashes: no thrust, autopilot off
    for k in range(3):
        emu.log()
        if g.probe()["ap"]:
            emu.type("B")
        hit = emu.run_until("ld: crashed", 30000) or emu.run_until("ld: lost", 10)
        check(f"crash {k + 1}", hit, emu.log_text(0)[-200:])
        if k == 0:
            emu.screenshot(HERE / "build" / "t_crash.png")
        if k < 2:
            g.wait(lambda p: p["state"] == 1 and p["outcome"] == 0, 800)
    check("game over", emu.run_until("ld: gameover", 600))
    emu.run(150)
    emu.press("FIRE", hold_frames=4)
    check("back to the title", emu.run_until("ld: title", 1500))
    fps = [int(x) for x in re.findall(r"fps10=(\d+)", "\n".join(emu.debug_log))]
    print(f"       fps x10: {fps[-12:]}")
    if failures:
        print("\n".join(emu.debug_log[-20:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("800XL: runs (no banks)", emu.run_until("ld: ready", 4000))

print(f"{'FAIL' if failures else 'PASS'}: lander_3d")
sys.exit(1 if failures else 0)
