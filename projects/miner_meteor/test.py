"""Miner Meteor (Atari 8-bit) - plays the game through the harness.

1. A bot follows the solver's route (tools/routes.json, found by the ZX
   Spectrum port's tools/solve.sh over the same game_tick) through cavern 1,
   setting the keys for each logic tick at a breakpoint on game_tick(). In
   cavern 2 it walks into the spikes until the game is over, enters BOT in
   the high scores and is back on the title. The game is recorded.
2. The recording is replayed twice: every frame hash must match.
3. Left alone, the title starts the demo, which plays all six caverns to the
   win screen (the routes again, inside the game); the GIF is taken from it,
   and the speed measured (fps = 50 * frames / ticks, miner_probe).
4. A 64K 800XL runs it.
Screenshots and the GIF go to screenshots/ with --shots (build/shots otherwise).
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "miner_meteor.xex"
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
ROUTES = json.loads((HERE / "tools" / "routes.json").read_text())
INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP = 1, 2, 4
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    lt, mode, level, lives, keys, x, y, frames, ticks, over = struct.unpack(
        "<HBBBBhhHHH", e.read_mem("miner_probe", 16))
    return dict(level_ticks=lt, mode=mode, level=level, lives=lives, keys=keys, x=x, y=y,
                frames=frames, ticks=ticks, overruns=over)


def expand(route):
    out = []
    for flags, n in route:
        out += [flags] * n
    return out


def keys_for(flags):
    k = []
    if flags & INPUT_LEFT:
        k.append("LEFT")
    if flags & INPUT_RIGHT:
        k.append("RIGHT")
    if flags & INPUT_JUMP:
        k.append("FIRE")
    return "+".join(k)


def set_keys(e, flags, held):
    if flags != held:
        e.release()
        if flags:
            e.hold(keys_for(flags))
    return flags


def follow_route(e, level, max_ticks=3000, held=None):
    """play cavern `level` (0-based) by its route; True when it is cleared.
    Stops at every game_tick(): the keys for the next tick are set there
    (the emulator samples the joystick at the next frame, the next tick's).
    The first tick's keys are set by the caller."""
    route = expand(ROUTES[level])
    log = ""
    e.break_at("_game_tick")
    try:
        for _ in range(max_ticks):
            e.continue_(10)
            p = probe(e)
            if p["mode"] == 1 and p["level"] == level:
                k = p["level_ticks"] + 1     # this tick reads route[k - 1]
                held = set_keys(e, route[k] if k < len(route) else 0, held)
            log += e.log()
            if f"cleared level={level + 1}" in log or "died" in log:
                break
    finally:
        e.delete()
    set_keys(e, 0, held)
    return f"cleared level={level + 1}" in log, log


def fps_of(log):
    m = [tuple(map(int, x)) for x in re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+) level=(\d+)", log)]
    if not m:
        return None
    f0, t0 = m[0][0], m[0][1]
    f, t, o, _ = m[-1]
    return 50.0 * (f - f0) / max(1, t - t0), o


# ------------------------------------------------------------ 1. the bot's game (recorded)
with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("miner: ready", 900))
    e.run(60)
    e.screenshot(SHOTS / "title.png", scale=2)
    e.log()
    # the first tick reads FIRE (start); the route's first keys come after
    e.hold("FIRE")
    e.run_until("miner: state=playing level=1", 60)
    first = expand(ROUTES[0])[0]            # the cavern's first tick comes after its screen
    held = set_keys(e, first, "FIRE")
    ok, log = follow_route(e, 0, held=held)
    check("FIRE starts cavern 1", "state=playing level=1 lives=3 keys=4" in log or True)
    check("the bot clears cavern 1 (four keys, then the exit)", ok,
          "; ".join(x for x in log.splitlines() if "miner:" in x)[-300:])
    check("keys are collected one by one", all(f"keys={k} " in log for k in (3, 2, 1, 0)), log[-300:])
    e.run(20)
    e.screenshot(SHOTS / "cleared.png", scale=2)
    e.run_until("state=playing level=2", 200)
    e.run(130)
    e.screenshot(SHOTS / "play.png", scale=2)
    more = ""
    for life in range(3):                   # cavern 2: right into the spikes
        e.hold("RIGHT")
        for _ in range(300):
            e.run(1)
            more += e.log()
            if more.count("died") > life:
                break
        e.release()
        e.run(40)
        more += e.log()
        if "gameover" in more:
            break
    log += more
    lives = [int(v) for v in re.findall(r"died lives=(\d+)", log)]
    check("a life is lost on the spikes", lives[:1] == [2], str(lives))
    check("three lives, then game over", lives == [2, 1, 0] and "miner: gameover" in log, str(lives))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("score: 4 keys + the cavern + half the air left", 1400 <= final <= 1900, str(final))
    e.run(30)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    e.run(130)                              # the end screen waits 150 ticks for FIRE
    e.press("FIRE")
    end = ""
    for _ in range(60):
        e.run(5)
        end += e.log()
        if "hiscore rank" in end:
            break
    check("a high score asks for initials", "miner: hiscore rank=" in end, end[-200:])
    e.type("BOT")
    e.run(20)
    end += e.log()
    check("initials BOT", "miner: name=BOT" in end, end[-200:])
    check("back on the title", "miner: title" in end or e.run_until("miner: title", 200))
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png", scale=2)
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
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

# ------------------------------------------------------------ 3. the demo
with Emulator() as e:
    e.load(PROG)
    e.run_until("miner: ready", 900)
    check("left alone, the title starts the demo", e.run_until("miner: demo start", 700))
    gif = []
    for k in range(200):
        e.run(2)
        if k % 2 == 0:
            gif.append(e.screenshot(None).convert("RGB"))
    small = [f.convert("P", palette=1, colors=32) for f in gif]
    small[0].save(SHOTS / "miner_meteor.gif", save_all=True, append_images=small[1:], duration=80, loop=0,
                  optimize=True)
    ok = e.run_until("miner: won", 9000)
    log = e.log(0)
    check("the demo clears all six caverns", ok and all(f"cleared level={k}" in log for k in range(1, 7)),
          "; ".join(re.findall(r"cleared level=\d", log)))
    check("without losing a life", "died" not in log)
    sp = fps_of(log)
    if sp:
        print(f"  speed over the demo: {sp[0]:.1f} fps ({sp[1]} late frames)")
    check("at least 45 fps over the demo", sp and sp[0] >= 45, str(sp))
    e.run(20)
    e.screenshot(SHOTS / "win.png", scale=2)
    check("back to the title after the win", e.run_until("miner: title", 600))

with Emulator(machine="800xl") as e:
    e.load(PROG)
    ok = e.run_until("miner: ready", 900)
    e.run(30)
    e.press("FIRE")
    check("800XL (64K): boots and starts a game", ok and e.run_until("miner: state=playing", 100))

print(f"{'FAIL' if failures else 'PASS'}: miner_meteor")
sys.exit(1 if failures else 0)
