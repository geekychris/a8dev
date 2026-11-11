"""StakAttack (Atari 8-bit) - plays a whole game through the harness.

1. A bot reads the game state (`gs`, by symbol) and places each piece where
   a classic stacking heuristic likes it best (few holes, a low and flat
   stack, complete rows), steering with the joystick: UP rotates, LEFT /
   RIGHT move, FIRE slams. It clears ten rows (stage 2), pauses and resumes
   once, then slams every piece into the middle until the stack tops out
   (the original has no lives: topping out is the only way to lose). Game
   over, BOT in the high scores, the title. The game is recorded.
2. The recording is replayed twice: every frame hash must match.
3. A 64K 800XL runs it.
4. On NTSC the logic runs 5 frames in 6 (the original's 50 Hz).
Screenshots and the GIF go to screenshots/ with --shots (build/shots otherwise).
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "stakattack.xex"
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
DEBUG = "--debug" in sys.argv
W, H = 12, 19
failures = []

# piece_shapes[] in game.c: rows of 4 columns per rotation
SHAPES = [
    ["....|XXXX|....|....", "..X.|..X.|..X.|..X.", "....|....|XXXX|....", ".X..|.X..|.X..|.X.."],
    [".XX.|.XX.|....|...."] * 4,
    [".X..|XXX.|....|....", ".X..|.XX.|.X..|....", "....|XXX.|.X..|....", ".X..|XX..|.X..|...."],
    [".XX.|XX..|....|....", ".X..|.XX.|..X.|....", "....|.XX.|XX..|....", "X...|XX..|.X..|...."],
    ["XX..|.XX.|....|....", "..X.|.XX.|.X..|....", "....|XX..|.XX.|....", ".X..|XX..|X...|...."],
    ["..X.|XXX.|....|....", ".X..|.X..|.XX.|....", "....|XXX.|X...|....", "XX..|.X..|.X..|...."],
    ["X...|XXX.|....|....", ".XX.|.X..|.X..|....", "....|XXX.|..X.|....", ".X..|.X..|XX..|...."],
]
CELLS = [[[(r, c) for r, row in enumerate(s.split("|")) for c, ch in enumerate(row) if ch == "X"]
          for s in rots] for rots in SHAPES]


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def read_gs(e):
    """GameState as cc65 lays it out (int = 16 bits, no padding)"""
    b = e.read_mem("_gs", 277)
    field = [list(b[r * W:(r + 1) * W]) for r in range(H)]
    ptype, rot, x, y, nxt, state = struct.unpack("<6h", b[228:240])
    score, = struct.unpack("<I", b[240:244])
    level, lines = struct.unpack("<2h", b[244:248])
    clear_timer, = struct.unpack("<h", b[254:256])
    return dict(field=field, type=ptype, rot=rot, x=x, y=y, next=nxt, state=state,
                score=score, level=level, lines=lines, clear_timer=clear_timer)


def fits(field, cells, x, y):
    for r, c in cells:
        fx, fy = x + c, y + r
        if fx < 0 or fx >= W or fy < 0 or fy >= H or field[fy][fx]:
            return False
    return True


def evaluate(field, cells, x, y):
    f = [row[:] for row in field]
    for r, c in cells:
        f[y + r][x + c] = 1
    full = [i for i in range(H) if all(f[i])]
    f = [[0] * W for _ in full] + [row for i, row in enumerate(f) if i not in full]
    heights = []
    holes = 0
    for c in range(W):
        top = next((r for r in range(H) if f[r][c]), H)
        heights.append(H - top)
        holes += sum(1 for r in range(top, H) if not f[r][c])
    bump = sum(abs(a - b) for a, b in zip(heights, heights[1:]))
    return 0.76 * len(full) - 0.51 * sum(heights) - 0.36 * holes * 2 - 0.18 * bump - 0.3 * max(heights)


def plan(g):
    """(rotation, x) with the best landing spot reachable straight down."""
    best = None
    for rot in range(4):
        cells = CELLS[g["type"]][rot]
        for x in range(-2, W):
            if not fits(g["field"], cells, x, g["y"]):
                continue
            y = g["y"]
            while fits(g["field"], cells, x, y + 1):
                y += 1
            s = evaluate(g["field"], cells, x, y)
            if best is None or s > best[0]:
                best = (s, rot, x)
    return best[1:] if best else (g["rot"], g["x"])


def tap(e, key, until=None, frames=8):
    """press for a frame, then run until `until(gs)` (the game took it) or
    `frames` pass; one release frame at least, so the next tap is a new
    press (a held direction auto-repeats after 10 frames, as the original)"""
    e.press(key, hold_frames=1, release_frames=1)
    n = 2
    while until and n < frames and not until(read_gs(e)):
        e.run(1)
        n += 1
    return n


def bot_game(e, smart_lines=10, max_frames=30000, gif=None):
    log = ""
    frames = 0
    pieces = 0
    shots = set()
    paused = False
    while frames < max_frames:
        g = read_gs(e)
        if g["state"] == 3:
            break
        if g["state"] != 1 or g["clear_timer"] > 0:
            if g["clear_timer"] == 9 and "clear" not in shots:
                e.screenshot(SHOTS / "lineclear.png", scale=2)
                shots.add("clear")
            e.run(1)
            frames += 1
            if gif is not None and frames % 2 == 0:
                gif.append(e.screenshot(None).convert("RGB"))
            log += e.log()
            continue
        if pieces == 6 and not paused:                     # pause and resume once
            tap(e, "START")
            e.run(20)
            e.screenshot(SHOTS / "paused.png", scale=2)
            tap(e, "START")
            frames += 24
            paused = True
        if g["lines"] < smart_lines:
            rot, x = plan(g)
            for _ in range(4):                             # rotate (wall kicks may move x)
                cur = read_gs(e)
                if cur["rot"] == rot:
                    break
                frames += tap(e, "UP", lambda a, r=cur["rot"]: a["rot"] != r)
            for _ in range(12):
                cur = read_gs(e)["x"]
                if cur == x:
                    break
                frames += tap(e, "LEFT" if x < cur else "RIGHT", lambda a, c=cur: a["x"] != c)
                if read_gs(e)["x"] == cur:                 # blocked
                    break
            if DEBUG:
                a = read_gs(e)
                print("type", g["type"], "plan", (rot, x), "got", (a["rot"], a["x"]))
            if gif is not None and len(gif) < 160:
                for _ in range(3):
                    e.run(2)
                    frames += 2
                    gif.append(e.screenshot(None).convert("RGB"))
        if pieces == 25 and "play" not in shots:
            e.screenshot(SHOTS / "play.png", scale=2)
            shots.add("play")
        frames += tap(e, "FIRE", lambda a: a["y"] < g["y"] or a["type"] != g["type"] or a["state"] != 1
                      or a["clear_timer"] > 0, 12)      # slam: a new piece (or a line clear)
        pieces += 1
        log += e.log()
    log += e.log()
    return log, pieces


def fps_of(log):
    m = re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+)", log)
    if not m:
        return None
    f, t, o = map(int, m[-1])
    return 50.0 * f / max(1, f + o), f, o


# ------------------------------------------------------------ 1. the bot's game (recorded)
gif = []
with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("stak: ready", 900) and e.run_until("stak: title", 200))
    e.run(40)
    e.screenshot(SHOTS / "title.png", scale=2)
    e.log()
    tap(e, "FIRE")
    check("FIRE starts the game", e.run_until("stak: start", 100))
    e.run(30)
    log, pieces = bot_game(e, gif=gif)
    locks = [int(s) for s in re.findall(r"stak: lock .*? score=(\d+)", log)]
    lines = [int(s) for s in re.findall(r"stak: cleared lines=(\d+)", log)]
    check("pieces lock and score points", len(locks) > 20 and max(locks) > 500, f"{len(locks)} locks, {locks[-3:]}")
    check("rows are cleared", lines and max(lines) >= 10, str(lines[-3:]))
    check("ten rows: stage 2", "stak: stage level=2" in log)
    check("pause and resume", "state=paused" in log and log.index("state=paused") < log.rindex("state=playing"))
    if "state=gameover" not in log:
        e.run_until("state=gameover", 600)
    e.run(5)                                # (run_until stops mid-line)
    log = e.log(0)                          # (everything since the load)
    check("the stack tops out: game over", "state=gameover" in log)
    m = re.search(r"state=gameover score=(\d+) lines=(\d+)", log)
    final = int(m.group(1)) if m else 0
    sp = fps_of(log)
    if sp:
        print(f"  speed over the game: {sp[0]:.1f} fps ({sp[2]} late frames in {sp[1]})")
    check("at least 48 fps over the bot's game", sp and sp[0] >= 48, str(sp))
    e.run(30)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    tap(e, "FIRE")
    more = ""
    for _ in range(60):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more:
            break
    check("a high score asks for initials", "stak: hiscore rank=" in more, more[-200:])
    e.run(10)
    e.screenshot(SHOTS / "hiscore.png", scale=2)
    e.type("BOT")
    e.run(20)
    more += e.log()
    check("initials BOT", "stak: name=BOT" in more, more[-200:])
    check("back on the title", "stak: title" in more or e.run_until("stak: title", 200))
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png", scale=2)
    rec = e.record_input("save", REC)
    print(f"  {pieces} pieces, final score {final}; recorded {rec['frames']} frames, {rec['events']} input changes")

if gif:
    small = [f.convert("P", palette=1, colors=32) for f in gif[:160]]
    small[0].save(SHOTS / "stakattack.gif", save_all=True, append_images=small[1:], duration=60, loop=0,
                  optimize=True)

# ------------------------------------------------------------ 2. replays
seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

# ------------------------------------------------------------ 3. 800XL
with Emulator(machine="800xl") as e:
    e.load(PROG)
    ok = e.run_until("stak: title", 900)
    e.run(30)
    e.press("FIRE")
    check("800XL (64K): boots and starts a game", ok and e.run_until("stak: start", 100))

# ------------------------------------------------------------ 4. NTSC
with Emulator() as e:
    e.set_tv("ntsc")
    e.load(PROG)
    ok = e.run_until("stak: title", 900)
    e.run(30)
    e.press("FIRE")
    e.run(600)
    frames, ticks = struct.unpack("<2H", e.read_mem("_stak_probe", 4))
    check("NTSC: the logic runs 5 frames in 6 (the original's 50 Hz)", ok and abs(ticks * 6 - frames * 5) <= 12,
          f"{ticks} ticks in {frames} frames")

print(f"{'FAIL' if failures else 'PASS'}: stakattack")
sys.exit(1 if failures else 0)
