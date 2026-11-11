"""Bullion Dash (Atari 8-bit) - the editor, a cleared level, a whole game.

1. On the title, E opens the editor. The test builds a level with the
   cursor (joystick taps) and the tiles (X steps through them, FIRE puts
   one): a ladder from the floor to the top row, a gold, the runner.
   RETURN test-plays it: the runner takes the gold and climbs out: LEVEL
   COMPLETE, back to the editor; ESC to the title.
2. A game from level 4 (RIGHT on the title chooses it): each life the
   runner takes the gold next to him and the guard catches him; five lives,
   game over, initials (BOT), the title with the table.
3. Level 121 has five guards: the fourth and fifth are missiles.
The session is recorded and replayed twice (every frame hash must match),
and the XEX runs on a 64K 800XL (the levels load at $A000 behind a
BASIC-off segment). Screenshots go to screenshots/ with --shots (build/shots otherwise).
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "bullion_dash.xex"
# the README's pictures are written with --shots; a plain run (selftest)
# leaves them alone and writes its pictures under build/
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []
GS_PLAYER = 15                     # GameState: state, level, score, lives, gold x2, frame (cc65 packs)


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def player(e):
    px, py, gx, gy = struct.unpack("<hhbb", e.read_mem("gs", GS_PLAYER + 6)[GS_PLAYER:])
    return px, py, gx, gy


def tap(e, key, n=1):
    # letters: the OS keyboard handler drops the same key pressed again
    # within a few frames (debounce), so they get a longer gap
    for _ in range(n):
        e.press(key, hold_frames=2, release_frames=2 if len(key) > 1 else 8)


def edit_level(e):
    tap(e, "X", 2)                         # brick -> solid -> ladder
    tap(e, "RIGHT", 12)
    for _ in range(15):                    # the ladder, rows 0..14
        tap(e, "FIRE")
        tap(e, "DOWN")
    tap(e, "X", 2)                         # -> bar -> gold
    tap(e, "UP")
    tap(e, "LEFT", 6)
    tap(e, "FIRE")                         # gold at (6, 14)
    tap(e, "X", 2)                         # -> hidden -> player
    tap(e, "LEFT", 4)
    tap(e, "FIRE")                         # the runner at (2, 14)
    e.run(10)


def play_edited(e):
    e.hold("RIGHT")
    for _ in range(400):
        e.run(1)
        px, py, gx, gy = player(e)
        if px == 120:
            break
    e.release()
    e.hold("UP")
    ok = e.run_until("complete", 600)
    e.release()
    return ok


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("bullion: ready", 900))
    e.run(40)
    e.screenshot(SHOTS / "title.png", scale=2)
    tap(e, "E")
    check("E opens the editor", e.run_until("bullion: editor", 60))
    e.run(5)
    edit_level(e)
    ed = e.read_mem("ed_x", 1)[0], e.read_mem("ed_y", 1)[0]
    check("the cursor went where the taps took it", ed == (2, 14), str(ed))
    e.screenshot(SHOTS / "editor.png", scale=2)
    e.log()
    tap(e, "RETURN")
    check("RETURN test-plays the level", e.run_until("bullion: test play", 60))
    e.run(20)
    mark = e.log_end()
    won = play_edited(e)
    log = e.log_text(mark)
    check("the runner takes the gold", "gold=1/1" in log and "score=100" in log, log[-600:])
    check("and climbs out: level complete", won, log[-300:])
    e.run(10)
    e.screenshot(SHOTS / "cleared.png", scale=2)
    log += e.log()
    check("back to the editor", "bullion: state=editor" in log.split("complete")[-1] or e.run_until("bullion: state=editor", 200))
    e.run(10)
    tap(e, "ESC")
    check("ESC: the title", e.run_until("bullion: title", 100))
    e.run(20)

    tap(e, "RIGHT", 3)                     # level 4
    e.press("FIRE")
    check("a game from level 4", e.run_until("bullion: game started at level 4", 100))
    log = ""
    deaths = 0
    for life in range(6):
        e.hold("RIGHT")
        if life == 0:                      # the GIF: the first life
            gif = []
            for k in range(75):
                e.run(2)
                gif.append(e.screenshot(None).convert("RGB"))
            e.screenshot(SHOTS / "play.png", scale=2)
        else:
            e.run(100)
        e.release()
        if life == 0:
            for k in range(75):
                e.run(2)
                gif.append(e.screenshot(None).convert("RGB"))
            small = [f.convert("P", palette=1, colors=32) for f in gif]
            small[0].save(SHOTS / "bullion_dash.gif", save_all=True, append_images=small[1:],
                          duration=40, loop=0, optimize=True)
        log += e.log()
        if not e.run_until("bullion: death", 1500):
            break
        deaths += 1
        log += e.log()
        e.run_until("state=", 200)
        log += e.log()
        if "gameover" in log:
            break
    log += e.log()
    scores = [int(s) for s in re.findall(r"bullion: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"bullion: lives=(-?\d+)", log)]
    check("gold scores", scores and max(scores) >= 300, str(scores))
    check("the guard catches him: lives lost", lives and min(lives) <= 1, str(lives))
    check("game over", "gameover" in log or e.run_until("gameover", 300), log[-200:])
    perf = re.findall(r"perf ticks=(\d+) draws=(\d+)", log)
    if perf:
        t, d = map(int, perf[-1])
        print(f"  frame rate on level 4: {50 * d / t:.1f} fps ({d} screens in {t} ticks)")
    e.run(40)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    e.press("FIRE")
    more = e.log()
    for _ in range(40):
        if "hiscore rank" in more or "bullion: title" in more:
            break
        e.run(5)
        more += e.log()
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "bullion: name=BOT" in more, more[-200:])
        more += e.log() if e.run_until("bullion: title", 300) else ""
        more += "bullion: title"
    check("back to the title", "bullion: title" in more)
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png", scale=2)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

with Emulator() as e:                       # five guards: players 1-3, missiles
    e.load(PROG)
    e.run_until("bullion: ready", 900)
    e.write_mem("start_level", bytes([121, 0]))
    e.press("FIRE")
    ok = e.run_until("bullion: state=playing", 300)
    e.run(80)
    n = e.read_mem("gs", GS_PLAYER + 11 + 14 * 5 + 1)[-1]
    check("level 121: five guards", ok and n == 5, str(n))
    e.screenshot(SHOTS / "level121.png", scale=2)

with Emulator(machine="800xl") as e:
    e.load(PROG)
    ok = e.run_until("bullion: ready", 900)
    e.run(30)
    e.press("FIRE")
    ok = ok and e.run_until("bullion: state=playing", 300)
    e.run(5)
    tiles = e.read_mem("gs", 98 + 448)[98:]
    check("800XL (64K): the levels load at $A000 and a game starts", ok and tiles[15 * 28] == 2, tiles[15 * 28:15 * 28 + 4].hex())

print(f"{'FAIL' if failures else 'PASS'}: bullion_dash")
sys.exit(1 if failures else 0)
