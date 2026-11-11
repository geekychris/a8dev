"""Boing Pinball: the attract mode, then a game played with the joystick.

- Boot: the attract mode (the original's demo player) plays and scores.
- START: a game. A bot reads the ball from the game's probe (front.c) and
  plays the joystick like the original's pb_bot: down held to pull the
  plunger, let go to launch; left / right when a ball comes down near a
  flipper. It scores on the first ball, then stops flipping so the balls
  drain: game over, the initials (BOT), the high-score table.
- How many of the 25 steps a second run (the physics' speed).
- A 64K 800XL: the game says it needs a 130XE.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "boing_pinball.xex"
failures = []
FLIPS = [(2400, 2016, 0), (5248, 2016, 1)]       # pivots (P) and sides (table_0.c)


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(emu):
    return struct.unpack("<8h", emu.read_mem(emu.addr("_probe"), 16, space="ram"))


def ticks(emu):
    return struct.unpack("<H", emu.read_mem(emu.addr("_ticks"), 2, space="ram"))[0]


with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    check("boots: the attract mode", emu.run_until("pb: ready", 3000) and "pb: game demo=1" in emu.log(0))
    check("the demo player scores", emu.run_until("pb: score=", 3000))
    emu.run(200)
    emu.screenshot(HERE / "build" / "attract.png")
    t0, f0 = ticks(emu), emu.frame()
    emu.run(500)
    rate_demo = (ticks(emu) - t0) * 50.0 / (emu.frame() - f0)
    emu.hold("START")
    emu.run(4)
    emu.release()
    check("START: a game", emu.run_until("pb: game demo=0", 100))
    check("ball 1", emu.run_until("pb: ball n=1", 200))
    pulled = 0
    holds = {0: 0, 1: 0}
    scored = False
    frame = 0
    t0, f0 = ticks(emu), emu.frame()
    shot = False
    while frame < 60000:
        x, y, vx, vy, st, ball_no, gstate, nballs = probe(emu)
        keys = []
        if st == 1:                                   # in the plunger lane: pull, then let go
            pulled += 1
            if pulled < 40:
                keys.append("DOWN")
        else:
            pulled = 0
        if ball_no == 1 and st == 0 and vy < -240:    # coming down: flip at it (ball 1 only)
            for px, py, side in FLIPS:
                if abs(x - (px + (1216 if side == 0 else -1216) // 2)) < 1216 * 3 // 4 + 216 and 0 < y - py < 640 + abs(vy) // 5:
                    holds[side] = 12
        for side, k in ((0, "LEFT"), (1, "RIGHT")):
            if holds[side] > 0:
                holds[side] -= 2
                keys.append(k)
        if keys:
            emu.hold("+".join(keys))
        else:
            emu.release()
        emu.run(2)
        frame += 2
        log = emu.log(0)
        if not scored and "demo=0" in log and "pb: score=" in log.split("pb: game demo=0")[-1]:
            scored = True
        if not shot and st == 0 and y < 4000:
            shot = True
            emu.screenshot(HERE / "build" / "play.png")
        if "pb: gameover" in log.split("pb: game demo=0")[-1]:
            break
    emu.release()
    rate_play = (ticks(emu) - t0) * 50.0 / max(1, emu.frame() - f0)
    print(f"  steps a second: demo {rate_demo:.1f}, play {rate_play:.1f} (25 is real time)")
    log = emu.log(0).split("pb: game demo=0")[-1]
    check("the bot scores with the flippers", scored, log[-300:])
    check("three balls", "pb: ball n=3" in log, log[-300:])
    check("game over", "pb: gameover" in log, log[-300:])
    check("the physics runs at least 8 steps a second", rate_play >= 8, f"{rate_play:.1f}")
    emu.run(10)
    emu.screenshot(HERE / "build" / "gameover.png")
    if "place=" in log or emu.run_until("HIGH SCORE", 1) or True:
        emu.type("BOT")
        emu.run(10)
        emu.hold("FIRE")
        emu.run(4)
        emu.release()
        check("initials kept", emu.run_until("pb: hiscore name=BOT", 300), emu.log(0)[-300:])
        check("back to the attract mode", emu.run_until("pb: game demo=1", 300))
    if failures:
        (HERE / "build" / "test-log.txt").write_text(emu.log(0))

with Emulator(machine="800xl") as emu:                 # 64K: no banks
    emu.load(PROG)
    emu.run_until("A8:", 1500)
    emu.run(2)
    log = emu.log(0)
    check("800XL: needs a 130XE", "pb: no banks" in log and "A8:FAIL" in log, log[-200:])

print(f"{'FAIL' if failures else 'PASS'}: boing_pinball")
sys.exit(1 if failures else 0)
