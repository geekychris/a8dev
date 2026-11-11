"""Boing Twist: a game played from boot by a bot.

- The original's self-test (SELECT held at start-up): bt_vector_hash() on
  the 6502 must be the original's host value, 3B792758 (tests/vectors.h).
- The title, the select screen, an Easy game (the Boing ball): the
  scramble; then the bot reads the scramble from memory and plays it
  backwards through the controls (the band by up/down, twists by
  left/right, the half by X and by fire + right, flips by a tap of fire),
  with a twist and its undo (SELECT), a peek (OPTION held) and a pause
  (START) on the way, until the ball is whole: solved and scored (the
  peek counted), initials typed, the high-score table on the title.
- A Medium game (a picture from bank 3), given up (R): it solves itself.
- The frame rate while moves animate (pictures of the ball drawn a second).
- A 64K 800XL: the game says it needs a 130XE.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "boing_twist.xex"
failures = []
VECTOR_HASH = "3B792758"


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def press(emu, keys, n=4):
    emu.hold(keys)
    emu.run(n)
    emu.release()
    emu.run(2)


def puzzle(emu):
    """P: bands, segs, slot[128], sig[128][2], hist[1024], nhist, nscramble, moves"""
    base = emu.addr("_P")
    head = emu.read_mem(base, 4)
    bands, segs = struct.unpack("<hh", head)
    tail = emu.read_mem(base + 3332, 6)
    nhist, nscr, moves = struct.unpack("<hhh", tail)
    hist = struct.unpack(f"<{nhist}h", emu.read_mem(base + 1284, 2 * nhist)) if nhist else ()
    return dict(bands=bands, segs=segs, nhist=nhist, nscramble=nscr, moves=moves, hist=hist)


def var(emu, name):
    return emu.read_mem(emu.addr(name), 1)[0]


def do_move(emu, m, segs, how_half="key"):
    """one move through the controls; waits for the game's 'move' line"""
    if m & 0x100:                                     # a flip of half k
        k = m & 0xFF
        for _ in range(2 * segs):
            if var(emu, "_sel_half") == k:
                break
            if how_half == "fire":                   # fire held + right
                emu.hold("FIRE")
                emu.run(3)
                emu.hold("FIRE+RIGHT")
                emu.run(3)
                emu.hold("FIRE")
                emu.run(3)
                emu.release()
                emu.run(3)
            else:
                d = (k - var(emu, "_sel_half")) % segs
                press(emu, "X" if d <= segs // 2 else "Z")
            emu.run(4)
        press(emu, "FIRE")
    else:
        b, west = m >> 1, m & 1
        for _ in range(16):
            if var(emu, "_sel_band") == b:
                break
            press(emu, "DOWN" if var(emu, "_sel_band") < b else "UP")
        press(emu, "LEFT" if west else "RIGHT")
    return emu.run_until("bt: move", 600)


with Emulator(machine="130xe") as emu:
    # ---- the self-test
    emu.load(PROG)
    emu.hold("SELECT")
    emu.run(100)
    emu.release()
    for _ in range(40):                               # ~250,000 frames: 83 minutes of 6502 time, seconds headless
        emu.run(10000)
        if "selftest hash=" in emu.log(0):
            break
    emu.run_until("bt: ready", 3000)
    log = emu.log(0)
    check("self-test: bt_vector_hash() is the original's", f"selftest hash={VECTOR_HASH}" in log, log[-200:])

    # ---- title, select, an Easy game
    emu.reset()
    emu.load(PROG)
    check("boots to the title", emu.run_until("bt: ready", 1500))
    emu.run(80)
    img = emu.screenshot(HERE / "build" / "title.png")
    check("title drawn", len(img.getcolors(1 << 16) or []) >= 5)
    press(emu, "FIRE", 6)
    check("select screen", emu.run_until("bt: screen=SELECT", 400))
    emu.run(40)
    press(emu, "FIRE", 6)
    check("Easy game starts", emu.run_until("bt: play pic=0 level=0 grid=4x8 scramble=8", 400))
    check("scramble shown move by move", emu.run_until("bt: scrambled n=8", 3000))
    emu.run(20)
    p = puzzle(emu)
    check("the puzzle in memory is 4 x 8, scrambled by 8", p["bands"] == 4 and p["segs"] == 8 and p["nhist"] == 8, p)

    # a twist and its undo
    b0 = var(emu, "_sel_band")
    press(emu, "RIGHT")
    check("a twist", emu.run_until("bt: move", 300))
    emu.run(20)
    check("moves counted", puzzle(emu)["moves"] == 1)
    press(emu, "SELECT")
    check("undo (SELECT)", emu.run_until("bt: move", 300) and puzzle(emu)["moves"] == 0)
    emu.run(20)
    # a peek and a pause
    emu.hold("OPTION")
    check("peek (OPTION held)", emu.run_until("bt: peek n=1", 100))
    emu.run(10)
    emu.screenshot(HERE / "build" / "peek.png")
    emu.release()
    emu.run(30)
    press(emu, "START")
    check("pause", emu.run_until("bt: paused=1", 60))
    press(emu, "START")
    check("unpause", emu.run_until("bt: paused=0", 60))

    # the bot: the scramble backwards
    p = puzzle(emu)
    todo = [m if m & 0x100 else m ^ 1 for m in reversed(p["hist"][:p["nscramble"]])]
    n_ok = 0
    shot = False
    used_fire = False
    for i, m in enumerate(todo):
        if "bt: solved" in emu.log(0):                # the Boing ball's tiles look alike: whole early
            n_ok += len(todo) - i
            break
        how = "fire" if (m & 0x100) and not used_fire else "key"
        if how == "fire":
            used_fire = True
        if do_move(emu, m, p["segs"], how):
            n_ok += 1
        else:
            print(f"    move {i} m={m} ({how}) not seen; sel_band={var(emu, '_sel_band')} sel_half={var(emu, '_sel_half')}")
        emu.run(20)                                   # the texture rebuilt, the solved test done
        if not shot and (m & 0x100) == 0:
            shot = True
            emu.screenshot(HERE / "build" / "play.png")
    check(f"the bot played {len(todo)} moves", n_ok == len(todo), f"{n_ok}/{len(todo)}")
    solved = "bt: solved" in emu.log(0) or emu.run_until("bt: solved", 600)
    emu.run(10)                                       # the rest of the line
    check("solved and scored (the peek counted)", solved and "peeks=1" in emu.log(0)
          and "gave_up=0" in emu.log(0), emu.log(0)[-300:])
    log = emu.log(0)
    line = ([ln for ln in log.splitlines() if ln.startswith("bt: solved")] or ["bt: solved score=0"])[-1]
    score = int(line.split("score=")[1].split()[0])
    check("score > 0", score > 0, line)
    emu.run(80)
    emu.screenshot(HERE / "build" / "solved.png")
    draws = struct.unpack("<HH", emu.read_mem(emu.addr("_draws"), 2) + emu.read_mem(emu.addr("_draw_frames"), 2))
    fps = 50.0 * draws[0] / max(1, draws[1])
    print(f"  ball pictures while moves animate: {draws[0]} in {draws[1]} frames = {fps:.1f} a second")
    check("frame rate while moves animate >= 8", fps >= 8, f"{fps:.1f}")
    press(emu, "FIRE", 6)
    check("high-score entry", emu.run_until("bt: screen=ENTRY", 200))
    emu.run(10)
    emu.type("BOT")
    emu.run(10)
    press(emu, "FIRE", 6)
    check("initials kept", emu.run_until("bt: hiscore name=BOT", 200))
    check("back to the title", emu.run_until("bt: screen=TITLE", 300))

    # ---- a Medium game, given up
    emu.run(30)
    press(emu, "FIRE", 6)
    emu.run_until("bt: screen=SELECT", 400)
    emu.run(30)
    press(emu, "RIGHT")                                  # level: Medium (6 x 12)
    check("Medium's preview", emu.run_until("bt: preview pic=1 level=1", 300))
    emu.run(5)
    press(emu, "DOWN")
    emu.run(10)
    press(emu, "RIGHT")                                  # the next picture Medium offers
    check("the next picture's preview", emu.run_until("bt: preview pic=3 level=1", 300))
    emu.run(30)
    emu.screenshot(HERE / "build" / "select.png")
    press(emu, "FIRE", 6)
    started = emu.run_until("bt: play pic=3 level=1 grid=6x12 scramble=20", 400)
    check("Medium game starts with Mars (bank 3's picture)", started, emu.log(0)[-200:])
    check("scrambled", emu.run_until("bt: scrambled n=20", 6000))
    emu.run(30)
    emu.screenshot(HERE / "build" / "medium.png")
    press(emu, "R")
    check("give up", emu.run_until("bt: give up", 100))
    solved = emu.run_until("bt: solved", 6000)
    emu.run(10)
    check("it solves itself", solved and "gave_up=1" in emu.log(0))
    emu.run(60)
    press(emu, "FIRE", 6)
    check("no high score for a give-up: back to select", emu.run_until("bt: screen=SELECT", 300))
    if failures:
        (HERE / "build" / "test-log.txt").write_text(emu.log(0))

with Emulator(machine="800xl") as emu:                 # 64K: no banks
    emu.load(PROG)
    emu.run_until("A8:", 1500)
    emu.run(2)
    log = emu.log(0)
    check("800XL: needs a 130XE", "bt: no banks" in log and "A8:FAIL" in log, log[-200:])

print(f"{'FAIL' if failures else 'PASS'}: boing_twist")
sys.exit(1 if failures else 0)
