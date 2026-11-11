"""tank_duel: the title's demo, driving, firing, pause, the autopilot
clearing round 1 (kills, the score, round 2), armour lost, game over, and
the 800XL's message.

1. The title: the autopilot fights the demo; RIGHT/LEFT pick the round,
   FIRE starts round 1.
2. The stick: UP drives ahead, LEFT turns; FIRE fires (a shell, the gun
   reloads). START pauses (nothing moves) and starts again.
3. B: the autopilot (the original's bot) kills the enemies (the score)
   and clears round 1: round 2 comes up.
4. Armour: the enemies hurt the tank; at 1 armour the next hit ends the
   game: game over, the best score, FIRE, the title.
5. An 800XL (no banks): the "needs a 130XE" message.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "tank_duel.xex"
failures = []

# TDGame offsets (src/td.h)
O_ROUND, O_HP, O_X, O_Z, O_YAW = 4, 33, 49, 65, 97


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Game:
    def __init__(self, emu):
        self.emu = emu
        self.G = emu.addr("_G")

    def probe(self):
        p = self.emu.read_mem("_td_probe", 10)
        return {"state": p[0], "round": p[1], "armour": p[2], "ap": p[3], "over": p[4], "between": p[5],
                "paused": p[6], "pick": p[7], "kills": p[8], "left": p[9]}

    def me(self):
        b = self.emu.read_mem(self.G, O_YAW + 2)
        x, = struct.unpack_from("<h", b, O_X)
        z, = struct.unpack_from("<h", b, O_Z)
        yaw, = struct.unpack_from("<H", b, O_YAW)
        return {"x": x, "z": z, "yaw": yaw}

    def wait(self, cond, frames):
        for _ in range(frames // 10):
            p = self.probe()
            if cond(p):
                return p
            self.emu.run(10)
        return self.probe()


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("td: title", 4000))
    check("ready", emu.run_until("td: ready", 200))
    g = Game(emu)
    emu.run(60)
    a = g.me()
    emu.run(150)
    b = g.me()
    p = g.probe()
    check("the title's demo fights (autopilot, the tank moves)", p["ap"] == 1 and p["state"] == 0 and a != b, f"{a} {b} {p}")
    emu.screenshot(HERE / "build" / "t_title.png")

    # (input is read once a pass: a pass can take 20 frames and more)
    emu.press("RIGHT", hold_frames=4)
    p = g.wait(lambda p: p["pick"] == 2, 200)
    check("RIGHT picks round 2", p["pick"] == 2, str(p))
    emu.press("LEFT", hold_frames=4)
    p = g.wait(lambda p: p["pick"] == 1, 200)
    check("LEFT back to round 1", p["pick"] == 1, str(p))
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE starts round 1", emu.run_until("td: start round=1", 300))
    p = g.wait(lambda p: p["state"] == 1, 200)
    check("fighting round 1, autopilot off, full armour", p["state"] == 1 and p["round"] == 1 and p["ap"] == 0
          and p["armour"] == 100, str(p))
    emu.run(20)

    # the stick
    a = g.me()
    emu.hold("UP")
    emu.run(60)
    emu.release()
    b = g.me()
    check("UP drives ahead (+z at the start)", b["z"] > a["z"], f"{a} {b}")
    emu.hold("LEFT")
    emu.run(40)
    emu.release()
    c = g.me()
    check("LEFT turns", c["yaw"] != b["yaw"], f"{b} {c}")
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE fires", emu.run_until("td: fire", 100))
    emu.run(6)
    emu.screenshot(HERE / "build" / "t_fire.png")

    emu.log()
    emu.press("START", hold_frames=4)
    check("START pauses", emu.run_until("td: paused=1", 100))
    a = g.me()
    emu.hold("UP")
    emu.run(50)
    emu.release()
    check("nothing moves while paused", g.me() == a)
    emu.press("START", hold_frames=4)
    check("START again: fighting", emu.run_until("td: paused=0", 100))

    # the autopilot clears round 1
    emu.log()
    emu.type("B")
    check("B: the autopilot", emu.run_until("td: autopilot=1", 100))
    check("a kill (the score)", emu.run_until("td: kill", 3000), emu.log_text(0)[-300:])
    emu.run(3)
    emu.screenshot(HERE / "build" / "t_kill.png")
    check("round 1 cleared", emu.run_until("td: clear round=1", 6000), emu.log_text(0)[-300:])
    check("round 2", emu.run_until("td: round=2", 300))
    p = g.wait(lambda p: p["between"] == 0, 300)
    check("at round 2 with kills and a score", p["round"] == 2 and p["kills"] >= 2 and p["state"] == 1, str(p))
    emu.run(80)
    emu.screenshot(HERE / "build" / "t_round2.png")

    # armour: hurt, then the last hit
    p = g.wait(lambda p: p["armour"] < 100 or p["state"] != 1, 6000)
    check("the enemies hurt the tank (armour lost)", p["state"] == 1 and p["armour"] < 100, str(p))
    emu.write_mem(g.G + O_HP, bytes([1]))
    over = emu.run_until("td: gameover", 8000)
    check("at 1 armour the next hit: game over", over, emu.log_text(0)[-300:])
    p = g.wait(lambda p: p["state"] == 2, 100)
    check("the game-over state", p["state"] == 2 and p["over"] & 1, str(p))
    emu.run(10)
    emu.screenshot(HERE / "build" / "t_over.png")
    emu.run(150)
    emu.press("FIRE", hold_frames=4)
    check("back to the title", emu.run_until("td: title", 300))
    emu.run(20)
    fps = [l for l in emu.debug_log if "ticks5s" in l]
    print(f"       {fps[-3:]}")
    if failures:
        print("\n".join(emu.debug_log[-20:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("800XL: the needs-a-130XE message", emu.run_until("td: needs 130xe", 4000))
    emu.run(10)
    emu.screenshot(HERE / "build" / "t_800xl.png")

print(f"{'FAIL' if failures else 'PASS'}: tank_duel")
sys.exit(1 if failures else 0)
