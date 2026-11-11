"""block_breaker_3d: the title's demo, the paddle, serving, bricks, a level, a lost ball, game over.

1. The title: the autopilot plays the demo; RIGHT/LEFT choose the start
   level, FIRE starts level 1 with the ball on the paddle.
2. The joystick moves the paddle (RIGHT, UP); FIRE serves and the ball flies
   down the tunnel. START pauses (the ball stops) and starts again.
3. The autopilot (B, the original's bot) plays level 1: bricks break (the
   score), a capsule is caught, the level is cleared, level 2 (Chequers)
   starts with its bricks drawn.
4. Autopilot off: serve and move the paddle away: the ball is lost (a life).
5. One life left: lost again: game over, FIRE, the title.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "block_breaker_3d.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Game:
    # BBGame: seed, rng, cell[72], hits[72], level, lives, over, won, left, stuck, between,
    # score, px, py, bx, by, bz, vx, vy, vz, ...
    OFS_CELL, OFS_LEVEL, OFS_SCORE = 8, 152, 159

    def __init__(self, emu):
        self.emu = emu
        self.G = emu.addr("_G")

    def probe(self):
        p = self.emu.read_mem("_bb_probe", 10)
        return {"state": p[0], "level": p[1], "lives": p[2], "ap": p[3], "over": p[4], "between": p[5],
                "paused": p[6], "pick": p[7], "stuck": p[8], "left": p[9]}

    def pos(self):
        v = struct.unpack("<H8h", self.emu.read_mem(self.G + self.OFS_SCORE, 18))
        return dict(zip(("score", "px", "py", "bx", "by", "bz", "vx", "vy", "vz"), v))

    def wait(self, cond, frames):
        for _ in range(frames // 10):
            p = self.probe()
            if cond(p):
                return p
            self.emu.run(10)
        return self.probe()

    def box_matches(self):
        """the bricks' box on the screen (both buffers) is the clean box"""
        y0 = self.emu.addr("_cl_lo")
        lo, hi = self.emu.read_mem("_cl_lo", 60), self.emu.read_mem("_cl_hi", 60)
        bad = 0
        for y in range(29, 60):
            src = lo[y] | hi[y] << 8
            want = self.emu.read_mem(src + 14, 13)
            for base in (0xA010, 0xB010):
                if self.emu.read_mem(base + y * 40 + 14, 13) != want:
                    bad += 1
        del y0
        return bad


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("bb: title", 4000))
    check("ready", emu.run_until("bb: ready", 200))
    g = Game(emu)
    emu.run(100)
    p0 = g.pos()
    emu.run(100)
    p1 = g.pos()
    check("the title's demo plays (autopilot, the ball moves)",
          g.probe()["ap"] == 1 and g.probe()["state"] == 0 and (p0["bx"], p0["bz"]) != (p1["bx"], p1["bz"]), f"{p0} {p1}")
    emu.screenshot(HERE / "build" / "t_title.png")

    emu.press("RIGHT", hold_frames=4)
    emu.run(20)
    check("RIGHT picks level 2", g.probe()["pick"] == 2, str(g.probe()))
    emu.press("LEFT", hold_frames=4)
    emu.run(20)
    check("LEFT back to level 1", g.probe()["pick"] == 1, str(g.probe()))
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE starts level 1", emu.run_until("bb: start level=1", 300))
    p = g.wait(lambda p: p["state"] == 1, 200)
    check("playing level 1, the ball on the paddle", p["state"] == 1 and p["level"] == 1 and p["stuck"] == 1
          and p["ap"] == 0 and p["lives"] == 3, str(p))
    emu.run(10)
    check("level 1's bricks on the screen", g.box_matches() == 0, f"{g.box_matches()} lines differ")

    # the paddle
    a = g.pos()
    emu.hold("RIGHT")
    emu.run(20)
    emu.release()
    b = g.pos()
    check("RIGHT moves the paddle right (the ball with it)", b["px"] > a["px"] and b["bx"] == b["px"], f"{a} {b}")
    emu.hold("UP")
    emu.run(20)
    emu.release()
    c = g.pos()
    check("UP moves it up", c["py"] > b["py"], f"{b} {c}")
    emu.screenshot(HERE / "build" / "t_paddle.png")

    # serve
    emu.log()
    emu.press("FIRE", hold_frames=4)
    check("FIRE serves", emu.run_until("bb: serve", 50))
    a = g.pos()
    emu.run(20)
    b = g.pos()
    check("the ball flies down the tunnel", b["bz"] > a["bz"] and g.probe()["stuck"] == 0, f"{a} {b}")
    emu.log()
    emu.press("START", hold_frames=4)
    check("START pauses", emu.run_until("bb: paused=1", 50))
    a = g.pos()
    emu.run(50)
    check("the ball stops while paused", g.pos() == a)
    emu.press("START", hold_frames=4)
    check("START again: playing", emu.run_until("bb: paused=0", 50))

    # the autopilot plays level 1
    emu.log()
    emu.type("B")
    check("B: the autopilot", emu.run_until("bb: autopilot=1", 100))
    check("a brick breaks (score)", emu.run_until("bb: break", 3000), emu.log_text(0)[-300:])
    emu.run(2)
    emu.screenshot(HERE / "build" / "t_break.png")
    check("the score", g.pos()["score"] > 0, str(g.pos()))
    emu.press("START", hold_frames=4)        # paused: both buffers get the new bricks
    emu.run(10)
    check("the bricks on the screen after the break", g.box_matches() == 0, f"{g.box_matches()} lines differ")
    emu.press("START", hold_frames=4)
    emu.log()
    check("level 1 cleared", emu.run_until("bb: clear level=1", 30000))
    emu.type("B")                             # the autopilot off while the ball waits for the next level
    check("B: the autopilot off", emu.run_until("bb: autopilot=0", 100))
    caps = [l for l in emu.debug_log if "capsule type" in l]
    check("a capsule caught on the way", len(caps) > 0)
    print("       " + " | ".join(l for l in emu.debug_log if "capsule" in l or "clear" in l or "lost" in l)[-200:])
    check("level 2", emu.run_until("bb: level=2", 300))
    p = g.wait(lambda p: p["between"] == 0, 300)
    check("on level 2 (Chequers), its bricks drawn", p["level"] == 2 and p["state"] == 1 and p["left"] > 0
          and g.box_matches() == 0, f"{p} {g.box_matches()}")
    emu.screenshot(HERE / "build" / "t_level2.png")

    # a lost ball
    p = g.wait(lambda p: p["stuck"] == 1, 300)
    lives = p["lives"]
    emu.log()
    emu.press("FIRE", hold_frames=4)
    emu.hold(["LEFT", "DOWN"])
    lost = emu.run_until("bb: lost", 1500)
    emu.release()
    check("the ball lost past the paddle", lost, emu.log_text(0)[-300:])
    p = g.wait(lambda p: p["stuck"] == 1, 100)
    check("a life gone, the ball back on the paddle", p["lives"] == lives - 1 and p["stuck"] == 1, str(p))

    # the last life
    emu.write_mem(g.G + 153, bytes([1]))
    emu.log()
    emu.press("FIRE", hold_frames=4)
    emu.hold(["RIGHT", "UP"])
    over = emu.run_until("bb: gameover", 1500)
    emu.release()
    check("the last ball lost: game over", over)
    p = g.wait(lambda p: p["state"] == 2, 100)
    check("the game-over state", p["state"] == 2 and p["over"] & 1, str(p))
    emu.screenshot(HERE / "build" / "t_over.png")
    emu.run(150)
    emu.press("FIRE", hold_frames=4)
    check("back to the title", emu.run_until("bb: title", 300))
    fps = [l for l in emu.debug_log if "ticks5s" in l]
    print(f"       {fps[-3:]}")
    if failures:
        print("\n".join(emu.debug_log[-20:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("800XL: runs (no banks)", emu.run_until("bb: ready", 4000))

print(f"{'FAIL' if failures else 'PASS'}: block_breaker_3d")
sys.exit(1 if failures else 0)
