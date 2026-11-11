"""ScAIry Fun (Atari edition) - plays a game through the harness.

1. A bot reads the game's probe (`sc_probe`: state, route, autopilot,
   cover, score, segment, lateral offset, speed) and plays like the
   original's test drivers. On the Levee Road it takes the wheel before the
   first flood (the autopilot would aquaplane into the river), crosses it
   slowly in its lane and hands back: a good catch (+500). It pauses and
   resumes. It leaves the old lane lines to the autopilot (over the drop:
   the tow truck, cover -40) and hands back after the tow; it leaves the
   second flood to it too (the river: -40). At the marina: the arrival and
   the software update. On Rush Hour the autopilot's blind spots take the
   rest of the cover: game over, the initials (BOT), the title. Recorded.
2. The recording is replayed twice: every frame hash must match.
3. A 64K 800XL runs it (the RAM under the OS ROM is used, no 130XE banks).
4. NTSC: the simulation keeps its 12.5 steps a second.
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
PROG = HERE / "build" / "scairy_fun.xex"
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    f, st, rd, late, state, route, ap, cover, score, seg, d, v = struct.unpack(
        "<4H4BihhH", e.read_mem("_sc_probe", 22))
    return dict(frames=f, steps=st, renders=rd, state=state, route=route, ap=ap, cover=cover,
                score=score, seg=seg, d=d >> 4, v=v, kmh=(v * 45) >> 7)


class Driver:
    """holds keys; changes them only when they change"""

    def __init__(self, e):
        self.e = e
        self.held = None

    def hold(self, keys):
        if keys != self.held:
            self.e.release()
            if keys:
                self.e.hold(keys)
            self.held = keys

    def drive(self, p, kmh=(26, 36), lane_d=28):
        """by hand: keep the speed in a band and the car in its lane"""
        k = []
        if p["ap"] or p["kmh"] > kmh[1] + 10:     # (down takes the wheel; brakes hard)
            k.append("DOWN")
        elif p["kmh"] < kmh[0]:
            k.append("UP")                        # (else coast: the game reads the stick
                                                  #  once a frame, every ~10 of the TV's)
        if p["d"] < lane_d - 4:
            k.append("RIGHT")
        elif p["d"] > lane_d + 4:
            k.append("LEFT")
        self.hold("+".join(k) if k else None)


def bot_game(e, gif):
    """the levee: by hand through the first flood; the old lines and the
    second flood left to the autopilot (the tow truck each time), then by
    hand through them; Rush Hour left to it until the cover runs out"""
    drv = Driver(e)
    log = ""
    shots = set()
    paused = False
    rolled = splashed = False
    cool = 0
    for k in range(12000):
        e.run(4)
        p = probe(e)
        log += e.log()
        rolled = rolled or "ev=roll" in log
        splashed = splashed or "ev=splash" in log
        if gif is not None and k % 6 == 0 and len(gif) < 120 and p["state"] == 2:
            gif.append(e.screenshot(None).convert("RGB"))
        if p["state"] == 5:                       # game over
            break
        if p["state"] != 2:
            drv.hold(None)
            continue
        r, seg = p["route"], p["seg"]
        manual = r == 0 and (96 <= seg < 126 or (rolled and 140 <= seg < 182) or
                             (splashed and 336 <= seg < 366))
        if cool:
            cool -= 1
        if manual:
            drv.drive(p)                          # (down: takes the wheel)
            if "flood" not in shots and 108 <= seg:
                e.screenshot(SHOTS / "flood.png", scale=2)
                shots.add("flood")
        else:
            drv.hold(None)
            if not p["ap"] and not cool and abs(p["d"] - (28 if r != 1 else 140)) < 10:
                e.press("FIRE", hold_frames=6)    # hand back
                cool = 12
            elif not p["ap"] and not cool and p["v"] < 60:
                drv.drive(p, lane_d=28 if r != 1 else 140)
        if not paused and r == 0 and seg > 132 and p["ap"]:
            e.press("START", hold_frames=6)
            e.run(40)
            e.screenshot(SHOTS / "paused.png", scale=2)
            e.press("START", hold_frames=6)
            paused = True
        if r == 0 and 156 <= seg <= 166 and p["ap"] and "ghost" not in shots:
            e.screenshot(SHOTS / "ghost.png", scale=2)
            shots.add("ghost")
        if r == 1 and "rush" not in shots and seg > 30:
            e.screenshot(SHOTS / "rushhour.png", scale=2)
            shots.add("rush")
    drv.hold(None)
    return log + e.log()


# ------------------------------------------------------------ 1. the bot's game (recorded)
gif = []
with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("scairy: ready", 900) and e.run_until("scairy: title", 200))
    e.run(120)
    e.screenshot(SHOTS / "title.png", scale=2)
    t0 = probe(e)
    e.run(200)
    check("the title's demo drives itself", probe(e)["seg"] > t0["seg"] + 5, str(probe(e)))
    e.log()
    e.press("FIRE", hold_frames=4)
    check("FIRE starts a game", e.run_until("scairy: state=1 route=1", 200))
    # the speed, on the levee with the autopilot driving (rows: the frame
    # loop; steps: the simulation, 12.5 a second)
    e.run_until("scairy: state=2", 300)
    e.run(100)
    a = probe(e)
    e.run(1000)
    b = probe(e)
    fps = 50 * (b["renders"] - a["renders"]) / (b["frames"] - a["frames"])
    hz = 50 * (b["steps"] - a["steps"]) / (b["frames"] - a["frames"])
    print(f"  speed on the levee: {fps:.1f} fps, the simulation {hz:.1f} steps a second")
    check("the simulation keeps 12.5 steps a second", abs(hz - 12.5) < 0.6, f"{hz:.1f}")
    check("at least 4.5 fps (the road; README: speed)", fps >= 4.5, f"{fps:.1f}")
    e.screenshot(SHOTS / "play.png", scale=2)
    log = bot_game(e, gif)
    e.run(10)
    log += e.log()
    ev = re.findall(r"scairy: ev=(\w+) a=(-?\d+) seg=(-?\d+) cover=(\d+)", log)
    names = [x[0] for x in ev]
    check("a takeover before the flood is a good catch (+500)", "save" in names and
          names.index("ap_off") < names.index("save"), str(names[:8]))
    check("the autopilot follows the old lines over the drop: the tow truck", "roll" in names, str(names))
    check("it aquaplanes into the river at the second flood", "splash" in names, str(names))
    check("the car is towed back", "respawn" in names)
    check("pause and resume", "scairy: paused" in log and "scairy: resumed" in log)
    check("the marina: arrived, then the software update",
          "scairy: state=3 route=1" in log and "scairy: state=4 route=1" in log)
    check("Rush Hour starts", "scairy: state=1 route=2" in log)
    check("cover runs out: game over", "scairy: state=5" in log, log[-300:])
    m = re.findall(r"scairy: state=5 route=\d score=(\d+)", log)
    final = int(m[0]) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    e.run(120)                                # (the end screen waits 2 s for FIRE)
    e.press("FIRE", hold_frames=6)
    more = ""
    for _ in range(80):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more:
            break
    check("a high score asks for initials", "scairy: hiscore rank=" in more, more[-200:])
    e.type("BOT")
    e.run(30)
    more += e.log()
    check("initials BOT", "scairy: name=BOT" in more, more[-200:])
    check("back on the title", "scairy: title" in more or e.run_until("scairy: title", 300))
    e.run(60)
    e.screenshot(SHOTS / "hiscores.png", scale=2)
    rec = e.record_input("save", REC)
    print(f"  final score {final}; recorded {rec['frames']} frames, {rec['events']} input changes")

if gif:
    small = [f.convert("P", palette=1, colors=32) for f in gif]
    small[0].save(SHOTS / "scairy_fun.gif", save_all=True, append_images=small[1:], duration=160, loop=0,
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
    ok = e.run_until("scairy: title", 900)
    e.run(60)
    e.press("FIRE", hold_frames=4)
    check("800XL (64K): boots and starts a game", ok and e.run_until("scairy: state=1", 200))

# ------------------------------------------------------------ 4. NTSC
with Emulator() as e:
    e.set_tv("ntsc")
    e.load(PROG)
    ok = e.run_until("scairy: title", 900)
    e.run(60)
    a = probe(e)
    e.run(600)
    b = probe(e)
    hz = 60 * (b["steps"] - a["steps"]) / max(1, b["frames"] - a["frames"])
    check("NTSC: 12.5 simulation steps a second", ok and abs(hz - 12.5) < 0.7, f"{hz:.1f}")

print(f"{'FAIL' if failures else 'PASS'}: scairy_fun")
sys.exit(1 if failures else 0)
