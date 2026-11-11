"""Planet Chomp: the title, the attract mode, then a game played with the joystick.

- Boot: the title; after 15 s the attract mode (the original's autopilot)
  plays and scores.
- START leaves the demo; START again: a game. A joystick bot wanders the
  maze: it holds a direction until the chomper stops, then tries the next
  (a fixed pseudo-random order: deterministic). It eats crumbs, and a spook
  catches it: a life lost.
- The level: the test sets the crumbs left to 1 (pc_probe); the next crumb
  eaten clears it: level 2.
- The bot plays level 2 a while, then lets go: the spooks catch the
  chomper until game over; the score makes the table: initials BOT, back
  to the title.
- How many of the 25 steps a second run in play (at least 20).
- The same on a 64K 800XL (it needs no banks).
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "planet_chomp.xex"
SHOTS = HERE / "build"
failures = []
GS_PLAYING, GS_OVER = 2, 5


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def u16(emu, a):
    return struct.unpack("<H", emu.read_mem(a, 2, space="ram"))[0]


class Probe:
    def __init__(self, emu):
        self.emu = emu
        self.left, self.moving, self.score, self.state, self.ways = struct.unpack(
            "<5H", emu.read_mem(emu.addr("_pc_probe"), 10, space="ram"))

    def get(self):
        e = self.emu
        return {"left": u16(e, self.left), "moving": e.read_mem(self.moving, 1, space="ram")[0],
                "score": struct.unpack("<i", e.read_mem(self.score, 4, space="ram"))[0],
                "state": e.read_mem(self.state, 1, space="ram")[0],
                "ways": e.read_mem(self.ways, 1, space="ram")[0]}


def press(emu, key, n=4):
    emu.hold(key)
    emu.run(n)
    emu.release()


class Bot:
    """greedy: a way with a crumb next (straight on first), else straight on,
    else a pseudo-random open way that isn't back"""
    DIRS = ["UP", "RIGHT", "DOWN", "LEFT"]

    def __init__(self):
        self.d, self.r, self.held = 1, 12345, None

    def rnd(self, n):
        self.r = (self.r * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.r >> 16) % n

    def step(self, emu, p):
        w = p["ways"]
        opened = [s for s in range(4) if w & (1 << s)]
        crumbs = [s for s in opened if w & (16 << s) and s != (self.d + 2) % 4]
        if self.d in crumbs:
            pass
        elif crumbs:
            self.d = crumbs[self.rnd(len(crumbs))]
        elif self.d not in opened and opened:
            fwd = [s for s in opened if s != (self.d + 2) % 4] or opened
            self.d = fwd[self.rnd(len(fwd))]
        if self.d != self.held:                      # (hold adds to what's held)
            emu.release()
            emu.hold(self.DIRS[self.d])
            self.held = self.d
        emu.run(4)


def play(machine, full):
    with Emulator(machine=machine) as emu:
        emu.load(PROG)
        check(f"{machine}: boots", emu.run_until("pc: ready", 3000))
        p = Probe(emu)
        emu.run(60)
        if full:
            emu.screenshot(SHOTS / "hiscores.png")
            emu.run(560)                             # the title text's turn
            emu.screenshot(SHOTS / "title.png")
        check(f"{machine}: the attract mode", emu.run_until("pc: game demo=1", 1000))
        emu.run(600)
        check(f"{machine}: the autopilot scores", p.get()["score"] > 0, str(p.get()))
        if full:
            emu.screenshot(SHOTS / "attract.png")
        press(emu, "START")                          # out of the demo
        for _ in range(50):
            emu.run(4)
            if p.get()["state"] == 0:
                break
        emu.run(30)
        press(emu, "START")
        check(f"{machine}: START: a game", emu.run_until("pc: game demo=0", 100), emu.log(0)[-200:])
        check(f"{machine}: playing", emu.run_until("pc: state=2", 300))
        bot = Bot()
        s0, f0 = u16(emu, emu.addr("_steps")), emu.frame()
        for _ in range(300):                         # 1200 frames
            bot.step(emu, p.get())
        rate = (u16(emu, emu.addr("_steps")) - s0) * 50.0 / (emu.frame() - f0)
        print(f"  {machine}: {rate:.1f} of 25 steps a second in play")
        check(f"{machine}: the rules keep up (>= 20 steps/s)", rate >= 20, f"{rate:.1f}")
        st = p.get()
        check(f"{machine}: the joystick eats crumbs", st["score"] >= 100, str(st))
        if full:
            emu.screenshot(SHOTS / "play.png")
        # the level: one crumb left
        emu.write_mem(p.left, struct.pack("<H", 1))
        for _ in range(500):
            bot.step(emu, p.get())
            if "pc: clear level=1" in emu.log(0):
                break
        check(f"{machine}: the last crumb clears the level", "pc: clear level=1" in emu.log(0), emu.log(0)[-200:])
        check(f"{machine}: level 2", emu.run_until("pc: level 2", 400))
        for _ in range(500):                         # level 2 a while
            bot.step(emu, p.get())
        emu.release()                                # then still: the spooks catch it
        bot.held = None
        emu.run_until("pc: gameover", 6000)
        log = emu.log(0).split("pc: game demo=0")[-1]
        check(f"{machine}: a spook takes a life", "pc: death by" in log)
        check(f"{machine}: game over", "pc: gameover" in log, log[-300:])
        if full:
            emu.run(20)
            emu.screenshot(SHOTS / "entry.png")
        emu.run(20)
        emu.type("BOT")
        emu.run(10)
        press(emu, "FIRE")
        check(f"{machine}: initials kept", emu.run_until("pc: hiscore name=BOT", 300), emu.log(0)[-300:])
        emu.run(10)
        check(f"{machine}: back to the title", p.get()["state"] == 0)
        if failures:
            (SHOTS / f"test-log-{machine}.txt").write_text(emu.log(0))


play("130xe", True)
play("800xl", False)
print(f"{'FAIL' if failures else 'PASS'}: planet_chomp")
sys.exit(1 if failures else 0)
