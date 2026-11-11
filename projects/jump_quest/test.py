"""Jump Quest (Atari 8-bit) - plays the game through the harness.

1. Recorded from reset: left alone, the title starts the demo, the bot: the
   solver's routes (tools/solve.sh, a search over the game's own jq_tick built
   for the host, from the ZX Spectrum port) played inside the game. It must
   clear level 1, the Office Park, with no life lost, while the view scrolls
   after the hero. START stops it; FIRE then starts a real game, which walks
   right into the first beetle and the first pit until the game is over (each
   life takes the diskette on the way: 300 points), enters BOT in the high
   scores and is back on the title.
2. The recording is replayed twice: every frame hash must match.
3. The demo again, to the end: all three levels, the CONGRATULATIONS screen,
   the frame rate (jq_probe counts frames drawn and logic ticks), the GIF.
4. Two players: DOWN on the title, then RIGHT picks Dale for player 1.
5. A 64K 800XL runs the demo through level 1.
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
PROG = HERE / "build" / "jump_quest.xex"
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    """jq_probe in main.c"""
    v = struct.unpack("<HBBBBhhhHHHH", e.read_mem("jq_probe", 20))
    keys = ("level_ticks", "state", "level", "lives", "health", "x", "y", "view_x",
            "frames", "ticks", "overruns", "scrolls")
    return dict(zip(keys, v))


def shot(e, name):
    e.screenshot(SHOTS / name, scale=2)


def perf(log):
    """fps drawn over the perf lines, overall and per level"""
    m = [tuple(map(int, x)) for x in
         re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+) level=(\d+)", log)]
    m = list(dict.fromkeys(m))
    per = {}
    for (f0, t0, _, l0), (f1, t1, _, l1) in zip(m, m[1:]):
        if l0 == l1 and t1 > t0:
            f, t = per.get(l1, (0, 0))
            per[l1] = (f + f1 - f0, t + t1 - t0)
    f = sum(a for a, _ in per.values())
    t = sum(b for _, b in per.values())
    if not t:
        return None
    return 50.0 * f / t, {lv: 50.0 * a / b for lv, (a, b) in sorted(per.items())}


# ------------------------------------------------------------ 1. the bot, then a real game (recorded)
with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("jq: ready", 900))
    e.run(60)
    shot(e, "title.png")
    check("the title starts the demo (the bot) after 12 s", e.run_until("jq: demo start", 700))
    log = ""
    for k in range(100):
        e.run(25)
        log += e.log()
        if k == 20:
            shot(e, "play.png")
        if "cleared level=1" in log or "died" in log:
            break
    p = probe(e)
    check("the bot clears level 1, the Office Park", "cleared level=1" in log,
          "; ".join(x for x in log.splitlines() if "jq:" in x)[-300:])
    check("... without losing a life", "died" not in log)
    check("the view scrolls after the hero along the level", p["scrolls"] >= 100 and p["x"] >= 1500, str(p))
    m = re.search(r"cleared level=1 score=(\d+)", log)
    check("level 1 scores (items, ? blocks, stomps)", m and int(m.group(1)) >= 200, m and m.group(0))
    e.run(30)
    shot(e, "cleared.png")
    e.press("START")
    check("START stops the demo", e.run_until("jq: title", 100))
    e.run(30)
    e.press("FIRE")
    check("FIRE starts a game as RJ, one player", e.run_until("jq: start character=0 players=1 demo=0", 300))

    more = ""                               # right into the first beetle and the first pit
    for life in range(3):
        e.hold("RIGHT")
        for _ in range(300):
            e.run(4)
            more += e.log()
            if more.count("died") > life:
                break
        e.release()
        e.run(100)
        more += e.log()
        if "gameover" in more:
            break
    log = more
    lives = [int(v) for v in re.findall(r"died lives=(-?\d+)", log)]
    check("a life is lost (the beetle, then the pit)", lives[:1] == [2] and "health=2" in log, str(lives))
    check("three lives, then game over", lives == [2, 1, 0] and "jq: gameover" in log, str(lives))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("the score is kept from life to life (the diskette each time: 300)", final == 300, str(final))
    e.run(20)
    shot(e, "gameover.png")
    e.run(140)                              # the game over screen waits 150 ticks for FIRE
    e.press("FIRE")
    end = ""
    for _ in range(60):
        e.run(5)
        end += e.log()
        if "hiscore rank" in end:
            break
    check("a high score asks for initials", "jq: hiscore rank=" in end, end[-200:])
    e.run(10)
    e.type("BOT")
    e.run(20)
    end += e.log()
    check("initials BOT", "jq: name=BOT" in end, end[-200:])
    check("back to the title", "jq: title" in end or e.run_until("jq: title", 200))
    e.run(30)
    shot(e, "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

# ------------------------------------------------------------ 2. replays
seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

# ------------------------------------------------------------ 3. the demo: all three levels
with Emulator() as e:
    e.load(PROG)
    e.run_until("jq: demo start", 1500)
    e.run(200)
    gif = []
    for k in range(300):
        e.run(1)
        if k % 2 == 0:
            gif.append(e.screenshot(None).convert("RGB"))
    small = [f.convert("P", palette=1, colors=32) for f in gif]
    small[0].save(SHOTS / "jump_quest.gif", save_all=True, append_images=small[1:], duration=40, loop=0,
                  optimize=True)
    for lv in (2, 3):
        e.run_until(f"state=playing level={lv}", 3000)
        e.run(300)
        shot(e, f"level{lv}.png")
    e.run_until("jq: gameover score", 3000)
    e.run(5)
    log = e.log_text(0)
    cleared = list(dict.fromkeys(re.findall(r"cleared level=(\d+)", log)))
    check("the demo clears all three levels", cleared == ["1", "2", "3"], str(cleared))
    check("no life lost on the way", "died" not in log)
    check("the end: CONGRATULATIONS", "won=1" in log, log[-300:])
    m = re.search(r"gameover score=(\d+)", log)
    check("the demo's score (the solver's routes)", m and int(m.group(1)) == 4200, m and m.group(0))
    fps = perf(log)
    if fps:
        print(f"  speed: {fps[0]:.1f} fps drawn, logic 50 ticks/s; per level: "
              + ", ".join(f"{lv}: {v:.1f}" for lv, v in fps[1].items()))
    check("draws at least 20 frames a second (debug build)", fps and fps[0] >= 20, str(fps))
    e.run(20)
    shot(e, "won.png")
    check("back to the title after the demo", e.run_until("jq: title", 900))

# ------------------------------------------------------------ 3b. logic at 50 Hz
with Emulator() as e:
    e.load(PROG)
    e.run_until("jq: demo start", 1500)
    e.run(60)
    a, f0 = probe(e), e.position()["since_load"]
    e.run(500)
    b, f1 = probe(e), e.position()["since_load"]
    hz = 50.0 * (b["ticks"] - a["ticks"]) / (f1 - f0)
    fps = 50.0 * (b["frames"] - a["frames"]) / (f1 - f0)
    print(f"  level 1, 500 frames: logic {hz:.1f} ticks/s, {fps:.1f} frames drawn/s")
    check("the logic keeps 50 ticks a second", hz >= 48.5, f"{hz:.1f}")

# ------------------------------------------------------------ 4. two players, Dale
with Emulator() as e:
    e.load(PROG)
    e.run_until("jq: title", 900)
    e.run(20)
    e.press("DOWN")                         # 2 players
    e.run(5)
    e.press("RIGHT")                        # Dale
    e.run(10)
    e.press("FIRE")
    check("two players, Dale first", e.run_until("jq: start character=1 players=2 demo=0", 300))

# ------------------------------------------------------------ 5. 64K 800XL
with Emulator(machine="800xl") as e:
    e.load(PROG)
    check("800XL (64K): the demo starts", e.run_until("jq: demo start", 1500))
    check("800XL (64K): the demo clears level 1", e.run_until("cleared level=1", 2500))

print(f"{'FAIL' if failures else 'PASS'}: jump_quest")
sys.exit(1 if failures else 0)
