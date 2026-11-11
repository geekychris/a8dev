"""Rolling Steel: a game played through with the joystick (`./a8 test rolling_steel`).

- Boot: the title (course 1's picture flying by).
- FIRE: course 1. The stick rolls the marble off the start's edge: a fall
  (FELL OFF), three seconds off the clock, back on the course.
- B: the autopilot (the original's demo driver) rolls on to the finish:
  COURSE CLEAR with a medal, the score, the next course (a level change).
- B again: the autopilot off; the marble sits until the clock runs out:
  game over, the score qualifies: initials (BOT), the high-score table,
  back to the title.
- Speed: 1/50 s steps a second (50 is real time; the game takes two at a
  time) and frames drawn a second, while rolling.
- A 64K 800XL: the game says it needs a 130XE.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "rolling_steel.xex"
OUT = HERE / "build"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def word(emu, sym):
    return struct.unpack("<H", emu.read_mem(emu.addr(sym), 2, space="ram"))[0]


def since(emu, mark):
    return emu.log(0)[mark:]


with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("rs: ready", 1500) and "rs: state=0" in emu.log(0))
    emu.run(60)
    emu.screenshot(OUT / "title.png")
    emu.press("FIRE")
    check("FIRE: course 1", emu.run_until("rs: state=1 level=1", 400))
    # off the start's edge
    emu.hold("LEFT+DOWN")
    fell = emu.run_until("rs: death FELL OFF course=1", 400)
    emu.release()
    check("a fall off the edge", fell)
    check("back on the course", emu.run_until("rs: run course=1", 600))
    falls = emu.read_mem(emu.addr("_probe") + 2, 1, space="ram")[0]
    check("one fall counted", falls == 1, str(falls))
    # the autopilot rolls on
    emu.press("B")
    check("B: the autopilot", emu.run_until("rs: autopilot=1", 50))
    s0, f0, l0 = word(emu, "_steps"), word(emu, "_frames"), word(emu, "_loops")
    emu.run(400)
    emu.screenshot(OUT / "play.png")
    s1, f1, l1 = word(emu, "_steps"), word(emu, "_frames"), word(emu, "_loops")
    steps_s = 100.0 * (s1 - s0) / max(1, f1 - f0)
    fps = 50.0 * (l1 - l0) / max(1, f1 - f0)
    print(f"  rolling: {steps_s:.1f} of 50 steps a second, {fps:.1f} frames a second")
    check("the physics keeps up (40+ of 50 steps a second)", steps_s >= 40, f"{steps_s:.1f}")
    goal = emu.run_until("rs: goal course=1", 3000)
    emu.run(2)
    log = emu.log(0)
    check("course 1 cleared", goal, log[-300:])
    m = re.search(r"rs: goal course=1 time=(\d+) medal=(\d) best=1 score=(\d+)", log)
    check("a time, a medal and a score", m and int(m.group(3)) >= 1000, m.group(0) if m else "")
    emu.run(20)
    emu.screenshot(OUT / "clear.png")
    check("on to course 2 (the clock carries over)", emu.run_until("rs: state=1 level=2", 600))
    emu.press("B")
    check("B: the autopilot off", emu.run_until("rs: autopilot=0", 50))
    over = emu.run_until("rs: gameover", 12000)
    emu.run(2)
    log = emu.log(0)
    check("out of time: game over", over and "won=0" in log.split("rs: gameover")[-1][:30], log[-200:])
    emu.run(80)
    emu.screenshot(OUT / "gameover.png")
    emu.type("BOT")
    emu.run(10)
    emu.press("FIRE")
    check("initials kept", emu.run_until("rs: hiscore name=BOT", 300), emu.log(0)[-300:])
    check("back to the title", emu.run_until("rs: state=0", 600))
    if failures:
        (OUT / "test-log.txt").write_text(emu.log(0))

with Emulator(machine="800xl") as emu:                 # 64K: no banks
    emu.load(PROG)
    emu.run_until("A8:", 1500)
    emu.run(2)
    log = emu.log(0)
    check("800XL: needs a 130XE", "rs: no banks" in log and "A8:FAIL" in log, log[-200:])

print(f"{'FAIL' if failures else 'PASS'}: rolling_steel")
sys.exit(1 if failures else 0)
