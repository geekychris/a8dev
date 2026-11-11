"""Nova Defense (Atari 8-bit) - plays a game through the harness.

A bot reads the swarm (alive_mask, grid_x/y), the gun and the bullets by
symbol, moves under the alien its shot will meet (leading the march) and
fires, dodging bullets that are about to hit; once wave 2 has started it
stops dodging, so lives are lost and the game ends. It enters BOT in the
high-score table and returns to the title. The game is recorded and
replayed twice (every frame hash must match); a 64K 800XL runs it too.
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
PROG = HERE / "build" / "nova_defense.xex"
# the README's pictures are written with --shots; a plain run (selftest)
# leaves them alone and writes its pictures under build/
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []
DEBUG = "--debug" in sys.argv
CELL_W, CELL_H, SHIFT, ALIEN_W, ALIEN_H = 26, 16, 12, 14, 10
PLAYER_Y, PLAYER_W = 232, 15


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def i16(e, sym):
    return struct.unpack("<h", e.read_mem(sym, 2))[0]


def world(e):
    masks = list(e.read_mem("alive_mask", 6))
    pb = struct.unpack("<hhB", e.read_mem("player_bullet", 5))
    ab = e.read_mem("alien_bullets", 15)
    abs_ = [struct.unpack("<hhB", ab[i * 5:i * 5 + 5]) for i in range(3)]
    return {"masks": masks, "gx": i16(e, "grid_x"), "gy": i16(e, "grid_y"),
            "dir": struct.unpack("b", e.read_mem("swarm_dir", 1))[0], "px": i16(e, "player_x"),
            "timer": e.peek("move_timer"), "mdelay": e.peek("move_delay"), "alive": e.peek("alive_count"),
            "shields": e.read_mem("shield_px", 3 * 14 * 32), "sx": struct.unpack("<3h", e.read_mem("shield_x", 6)),
            "pb": pb, "ab": [b for b in abs_ if b[2]], "state": e.peek("state")}


def march(w, frames):
    """the swarm's (grid_x, grid_y) for the next frames (game.c's march)"""
    gx, gy, d, timer = w["gx"], w["gy"], w["dir"], w["timer"]
    alive = w["alive"]
    delay = 1 if alive <= 20 else 2 if alive <= 35 else w["mdelay"]
    step = 10 if alive <= 1 else 8 if alive <= 5 else 5 if alive <= 10 else 4 if alive <= 20 else 3
    cells = [(r, c) for r in range(6) for c in range(8) if w["masks"][r] >> c & 1]
    out = []
    for _ in range(frames):
        out.append((gx, gy))
        timer += 1
        if timer >= delay and cells:
            timer = 0
            left = min(gx + c * CELL_W + (SHIFT if r & 1 else 0) for r, c in cells)
            right = max(gx + c * CELL_W + (SHIFT if r & 1 else 0) + CELL_W for r, c in cells)
            if (d > 0 and right >= 312) or (d < 0 and left <= 8):
                gy += 6
                d = -d
            else:
                gx += d * step
    return out


def shot_hits(w, x, start, path):
    """a shot fired at x, `start` frames from now: the alien it hits, or None
    (a shield in the way, or nothing)"""
    y = PLAYER_Y - 2
    for k in range(start, start + 22):
        prev, y = y, y - 10
        if y < 30:
            return None
        if 200 <= y < 214:
            for s, sx in enumerate(w["sx"]):
                bx = x - sx
                if 0 <= bx < 32 and w["shields"][s * 448 + (y - 200) * 32 + bx]:
                    return None
        gx, gy = path[k]
        for r in range(6):
            ay = gy + r * CELL_H
            if not (prev >= ay and y < ay + ALIEN_H):
                continue
            rel = x - gx - (SHIFT if r & 1 else 0)
            c = rel // CELL_W
            if 0 <= rel < 8 * CELL_W and rel - c * CELL_W < ALIEN_W and w["masks"][r] >> c & 1:
                return (r, c)
    return None


def choose(w, delay, careless):
    """keys: towards the nearest place a shot would hit from, fire there"""
    px = w["px"]
    path = march(w, 100)
    keys = []
    best, best_cost = None, 1e9
    low = max((r for r in range(6) if w["masks"][r]), default=0)
    for dx in range(0, 150, 3):
        for side in ((1, -1) if dx else (1,)):
            nx = px + side * dx
            if not 8 <= nx <= 297:
                continue
            frames = dx // 3                      # 3 px a frame
            hit = shot_hits(w, nx + 7, delay + frames, path)
            if hit:
                r, c = hit
                # the lowest row first (it decides when the swarm lands),
                # then the outer columns (fewer drops), then the nearest
                cost = frames + 10 * (low - r) + 6 * min(c, 7 - c)
                if cost < best_cost:
                    best, best_cost = (dx, side), cost
    if best:
        dx, side = best
        if dx == 0:
            if not w["pb"][2]:
                keys.append("FIRE")
        else:
            keys.append("RIGHT" if side > 0 else "LEFT")
    if not careless:                                      # a bullet about to hit: step aside
        for bx, by, _ in w["ab"]:
            if PLAYER_Y - 30 < by < PLAYER_Y + 8 and px - 4 <= bx < px + PLAYER_W + 4:
                keys = [k for k in keys if k == "FIRE"] + (["RIGHT"] if bx < px + 7 else ["LEFT"])
                break
    return "+".join(keys)


GIF_FROM, GIF_TO = 60, 460


def save_gif(frames, path):
    small = [f.convert("P", palette=1, colors=32) for f in frames]
    small[0].save(path, save_all=True, append_images=small[1:], duration=40, loop=0, optimize=True)


def bot_game(e, max_frames=25000):
    log, t, gif, shots = "", 0, [], set()
    held = ""
    shots_log = []
    while "state=gameover" not in log and t < max_frames:
        e.run_to_addr("_frame_wait", 3)                  # the game's frame is done
        # a press now is seen at the next tick if the vertical blank (line
        # 248) is still to come in this frame, else the one after
        delay = 0 if e.position()["tstates"] < 248 * 114 else 1
        w = world(e)
        keys = choose(w, delay, careless="wave=2" in log or t > 20000) if w["state"] == 1 else ""
        if "FIRE" in held and "FIRE" in keys:         # a shot needs a fresh press
            keys = keys.replace("FIRE", "").strip("+")
        if DEBUG and "FIRE" in keys and "FIRE" not in held:
            pred = shot_hits(w, w["px"] + 7, delay, march(w, 100))
            shots_log.append((t, pred, list(w["masks"])))
        if keys != held:
            e.release()
            if keys:
                e.hold(keys)
            held = keys
        e.run(1)
        t += 1
        if GIF_FROM <= t < GIF_TO:
            gif.append(e.screenshot(None).convert("RGB"))
        elif gif and t >= GIF_TO:
            save_gif(gif, SHOTS / "nova_defense.gif")
            gif = []
        if t >= 400 and "play" not in shots:
            e.screenshot(SHOTS / "play.png", scale=2)
            shots.add("play")
        log += e.log()
        if "waveclear" in log and "clear" not in shots:
            e.screenshot(SHOTS / "waveclear.png", scale=2)
            shots.add("clear")
    e.release()
    e.run(5)
    if DEBUG:
        for t0, pred, masks in shots_log[-30:]:
            print("  shot", t0, "predicted", pred, "masks", masks)
    return log + e.log()


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("nova: ready", 600))
    e.run(60)
    e.screenshot(SHOTS / "title.png", scale=2)
    e.press("FIRE")
    check("FIRE starts the game", e.run_until("nova: state=playing", 100))
    log = bot_game(e)
    if "--log" in sys.argv:
        print(log)
    scores = [int(s) for s in re.findall(r"nova: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"nova: lives=(\d+)", log)]
    fps = [int(f) for f in re.findall(r"nova: fps=(\d+)", log)]
    check("shooting aliens scores", scores and max(scores) >= 300, str(scores[-5:]))
    check("wave cleared: wave 2", "nova: wave=2" in log, log[-300:])
    check("a life is lost", lives and min(lives) < 3, str(lives))
    check("game over", "state=gameover" in log)
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    print(f"  frame rate: {fps}")
    check("at least 45 fps", fps and min(fps) >= 45, str(fps))
    e.run(30)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    more = ""
    for _ in range(80):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "nova: title" in more:
            break
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "nova: name=BOT" in more, more[-200:])
        more += e.log() if e.run_until("nova: title", 300) else ""
        more += "nova: title"
    else:
        check(f"score {final} below the table: no entry", final <= 1000)
    check("back to the title", "nova: title" in more)
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png", scale=2)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

with Emulator(machine="800xl") as e:
    e.load(PROG)
    ok = e.run_until("nova: ready", 600)
    e.run(30)
    e.press("FIRE")
    check("800XL (64K): boots and starts a game", ok and e.run_until("nova: state=playing", 100))

print(f"{'FAIL' if failures else 'PASS'}: nova_defense")
sys.exit(1 if failures else 0)
