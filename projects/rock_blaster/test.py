"""Rock Blaster (Atari 8-bit) - plays a game through the harness.

A bot reads the game's state by symbol (ship_*, rk_*, gs_*) and turns to
the nearest rock, firing all the time; after the first wave is cleared
(wave 2) it also thrusts at the rocks, so the ship is hit and the game
ends. It enters BOT in the high-score table and returns to the title. The
game is recorded and replayed twice (every frame hash must match); a 64K
800XL runs it too. The log's "rb: fps=" lines give the frame rate.
Screenshots and the GIF go to screenshots/ with --shots (build/shots otherwise).
"""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "rock_blaster.xex"
# the README's pictures are written with --shots; a plain run (selftest)
# leaves them alone and writes its pictures under build/
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
W, H = 320, 216
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def arr(e, sym, n, fmt="B"):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", e.read_mem(sym, n * size))


def state(e):
    act = arr(e, "rk_active", 24)
    px, py = arr(e, "rk_px", 24, "h"), arr(e, "rk_py", 24, "h")
    return {"x": arr(e, "ship_px", 1, "h")[0], "y": arr(e, "ship_py", 1, "h")[0],
            "angle": e.peek("ship_angle"), "alive": e.peek("ship_alive"),
            "rocks": [(px[i], py[i]) for i in range(24) if act[i]],
            "state": e.peek("gs_state"), "level": e.peek("gs_level")}


def wrapd(d, size):
    return (d + size // 2) % size - size // 2


def keys(s, suicide):
    out = ["FIRE"]
    if s["alive"] and s["rocks"]:
        dx, dy = min(((wrapd(rx - s["x"], W), wrapd(ry - s["y"], H)) for rx, ry in s["rocks"]),
                     key=lambda r: r[0] ** 2 + r[1] ** 2)
        want = round(math.atan2(dy, dx) * 128 / math.pi) & 255
        diff = (want - s["angle"] + 128) % 256 - 128
        if diff < -2:
            out.append("LEFT")
        elif diff > 2:
            out.append("RIGHT")
        if suicide and abs(diff) < 20:
            out.append("UP")
    return "+".join(out)


GIF_FROM, GIF_TO = 100, 500


def save_gif(frames, path):
    small = [f.convert("P", palette=1, colors=32) for f in frames]
    small[0].save(path, save_all=True, append_images=small[1:], duration=40, loop=0, optimize=True)


def bot_game(e, max_frames=12000):
    log = ""
    t = 0
    gif = []
    shots = set()
    while "state=gameover" not in log and t < max_frames:
        s = state(e)
        e.release()
        e.hold(keys(s, suicide="level=2" in log or t > 6000))
        e.run(4)
        t += 4
        if GIF_FROM <= t < GIF_TO:
            gif.append(e.screenshot(None).convert("RGB"))
        elif gif and t >= GIF_TO:
            save_gif(gif, SHOTS / "rock_blaster.gif")
            gif = []
        if t >= 300 and "play" not in shots:
            e.screenshot(SHOTS / "play.png", scale=2)
            shots.add("play")
        log += e.log()
        if "particles=" in log and "explode" not in shots and "score=" in log and t > 600:
            e.screenshot(SHOTS / "explode.png", scale=2)
            shots.add("explode")
    e.release()
    e.run(5)
    return log + e.log()


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("rb: ready", 600))
    e.run(60)
    e.screenshot(SHOTS / "title.png", scale=2)
    e.press("FIRE")
    check("FIRE starts the game", e.run_until("rb: state=playing", 100))
    log = bot_game(e)
    if "--log" in sys.argv:
        print(log)
    scores = [int(s) for s in re.findall(r"rb: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"rb: lives=(\d+)", log)]
    fps = [(int(f), int(r)) for f, r in re.findall(r"rb: fps=(\d+) rocks=(\d+)", log)]
    check("shooting rocks scores", scores and max(scores) >= 200, str(scores[-5:]))
    check("all large rocks split down: small rocks score 100", any(b - a == 100 for a, b in zip([0] + scores, scores)))
    check("wave 2", "rb: level=2" in log, log[-300:])
    check("a life is lost", lives and min(lives) < 3, str(lives))
    check("game over", "state=gameover" in log)
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    print(f"  frame rate (fps, rocks): {fps}")
    check("frame rate at least 20 fps", fps and min(f for f, _ in fps) >= 20, str(fps))
    e.run(30)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    more = ""
    for _ in range(60):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "rb: title" in more:
            break
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "rb: name=BOT" in more, more[-200:])
        more += e.log() if e.run_until("rb: title", 300) else ""
        more += "rb: title"
    else:
        check(f"score {final} below the table: no entry", final <= 1000)
    check("back to the title", "rb: title" in more)
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
    ok = e.run_until("rb: ready", 600)
    e.run(30)
    e.press("FIRE")
    check("800XL (64K): boots and starts a game", ok and e.run_until("rb: state=playing", 100))

print(f"{'FAIL' if failures else 'PASS'}: rock_blaster")
sys.exit(1 if failures else 0)
