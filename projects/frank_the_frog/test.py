"""Frank the Frog (Atari 8-bit) - plays a whole game through the harness.

A bot reads the lanes (the game's `lanes` array and `frank_probe`, by symbol)
and hops when the next lane is safe: across the road, onto logs and rafts,
into a free pond. It plays carefully until all four ponds are filled (level
2), then carelessly, so lives are lost and the game ends; it enters BOT in
the high-score table and returns to the title. The game is recorded and
replayed twice (every frame hash must match). A 64K 800XL runs it too.
Screenshots go to screenshots/ with --shots (build/shots otherwise). --debug prints the bot's last moves before
each death.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "frank_the_frog.xex"
# the README's pictures are written with --shots; a plain run (selftest)
# leaves them alone and writes its pictures under build/
SHOTS = HERE / "screenshots" if "--shots" in sys.argv else HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
NUM_LANES = 10                       # lanes.h
HOMES = [40, 120, 216, 280]          # pond centres (lanes.c)
VBI_TSTATES = 248 * 114              # PAL: the vertical blank starts at line 248
failures = []
DEBUG = "--debug" in sys.argv


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def arr(e, sym, n, fmt="B"):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", e.read_mem(sym, n * size))


def read_world(e):
    rows, dirs, sps = arr(e, "lane_row", 10), arr(e, "lane_dir", 10, "b"), arr(e, "lane_speed", 10)
    rivers, nums, ws = arr(e, "lane_river", 10), arr(e, "lane_num", 10), arr(e, "lane_width", 10)
    pers, shifts, bases = arr(e, "lane_period", 10, "h"), arr(e, "lane_shift", 10, "h"), arr(e, "lane_base", 30, "h")
    lanes = {}
    for i in range(NUM_LANES):
        lanes[rows[i]] = dict(d=dirs[i], sp=sps[i], river=rivers[i], w=ws[i], per=pers[i],
                              shift=shifts[i], base=bases[i * 3:i * 3 + nums[i]])
    fx, frow, state = struct.unpack("<3h", e.read_mem("frank_probe", 6))
    filled = list(e.read_mem("home_filled", 4))
    return fx, frow, state, lanes, filled


def objs_at(L, t):
    """Object x positions after t more ticks (lanes.c lane_obj_x)."""
    sh = (L["shift"] + L["d"] * L["sp"] * t) % L["per"]
    out = []
    for b in L["base"]:
        x = (b + sh) % L["per"]
        if x >= (336 if L["d"] > 0 else 320):
            x -= L["per"]
        out.append(x)
    return out


def road_safe(px, L, frames):
    for t in range(1, frames + 1):
        for ox in objs_at(L, t):
            if px + 16 - 5 > ox + 2 - 3 and px + 5 < ox + L["w"] - 2 + 3:   # the game's test + margin
                return False
    return True


def raft_under(px, L, t):
    return any(px + 16 - 6 > ox and px + 6 < ox + L["w"] for ox in objs_at(L, t))


def ride(L):
    return L["d"] * L["sp"]


def reaches_pond(sx, L, filled):
    """Hopping into the top lane at sx: does it carry Frank under a free pond?"""
    for k in range(1, 80):
        x = sx + ride(L) * k
        if not raft_under(x, L, k) or not -4 <= x <= 308:
            return False
        if any(not f and abs(x + 8 - cx) <= 10 for cx, f in zip(HOMES, filled)):
            return True
    return False


def choose(fx, frow, lanes, filled):
    nxt = frow - 1
    sx = (fx // 16) * 16 if fx >= 0 else 0      # a hop snaps Frank to his column
    if frow in lanes and lanes[frow]["river"]:   # riding in the river
        L = lanes[frow]
        r = ride(L)
        stays = all(raft_under(fx + r * k, L, k) and -4 <= fx + r * k <= 308 for k in range(1, 7))
        if nxt == 0:
            free = [cx for cx, f in zip(HOMES, filled) if not f]
            near = min(free, key=lambda cx: abs(fx + 8 - cx)) if free else None
            if near is not None and abs(fx + 8 - near) <= 12:
                return "UP"
            if not stays:
                return "DOWN" if raft_under(sx, lanes[frow + 1], 2) else None
            if near is not None:
                step = "LEFT" if near < fx + 8 else "RIGHT"
                nx = sx - 16 if step == "LEFT" else sx + 16
                if all(raft_under(nx + r * k, L, k) for k in range(1, 5)):
                    return step
            return None
        nl = lanes[nxt]
        if nxt == 1 and not reaches_pond(sx, nl, filled):
            pass                                 # the top lane wouldn't take him to a pond
        elif raft_under(sx, nl, 1) and raft_under(sx + ride(nl) * 8, nl, 8) and -4 <= sx + ride(nl) * 8 <= 308:
            return "UP"
        if not stays:
            down = frow + 1
            if down not in lanes or raft_under(sx, lanes[down], 2):
                return "DOWN"
        return None
    if nxt in lanes and lanes[nxt]["river"]:     # from the median onto a log/raft
        nl = lanes[nxt]
        if raft_under(sx, nl, 1) and raft_under(sx + ride(nl) * 8, nl, 8) and -4 <= sx + ride(nl) * 8 <= 308:
            return "UP"
        return None
    if nxt in lanes:                             # road
        if road_safe(sx, lanes[nxt], 14):
            return "UP"
    elif nxt >= 0:
        return "UP"                              # the median
    # standing in a road lane that is about to get dangerous: up, else back
    if frow in lanes and not lanes[frow]["river"] and not road_safe(fx, lanes[frow], 8):
        if nxt == 6 or (nxt in lanes and road_safe(sx, lanes[nxt], 8)):
            return "UP"
        if frow + 1 not in lanes or road_safe(sx, lanes[frow + 1], 10):
            return "DOWN"
    return None


GIF_FROM, GIF_TO = 150, 650          # frames of the bot's game in the GIF


def save_gif(frames, path):
    small = [f.convert("P", palette=1, colors=64) for f in frames]
    small[0].save(path, save_all=True, append_images=small[1:], duration=40, loop=0, optimize=True)


def bot_game(e, max_frames=30000):
    log = ""
    t = 0
    shots = set()
    history = []
    gif = []
    while "state=gameover" not in log and t < max_frames:
        e.run_to_addr("_frame_wait", 3)          # the game loop has finished its frame
        fx, frow, state, lanes, filled = read_world(e)
        # The emulator samples the joystick at the start of a frame and the OS
        # copies it at the vertical blank (line 248): a press made now is seen
        # by the game's next tick if that blank is still to come this frame,
        # else by the one after (the game then ticks once before the hop).
        if e.position()["tstates"] < VBI_TSTATES:
            for L in lanes.values():
                L["shift"] = (L["shift"] + L["d"] * L["sp"]) % L["per"]
            if frow in lanes and lanes[frow]["river"]:
                fx += ride(lanes[frow])
        careless = "level=2" in log or t > 20000
        hop = None
        if state == 1:
            hop = "UP" if careless else choose(fx, frow, lanes, filled)
        history.append((t, frow, fx, hop))
        if hop:
            e.press(hop, hold_frames=2, release_frames=1)
            t += 3
        else:
            e.run(1)
            t += 1
        if GIF_FROM <= t < GIF_TO and len(gif) < (t - GIF_FROM) // 2 + 1:
            gif.append(e.screenshot(None).convert("RGB"))
        if t >= GIF_TO and gif:
            save_gif(gif, SHOTS / "frank_the_frog.gif")
            gif = []
        new = e.log()
        log += new
        if DEBUG and "death" in new:
            print("DEATH", [l for l in new.splitlines() if "death" in l])
            for h in history[-8:]:
                print("   ", h)
        if "home slot" in log and "home" not in shots:
            e.screenshot(SHOTS / "home.png", scale=2)
            shots.add("home")
        if t > 400 and "play" not in shots and frow <= 4 and state == 1:
            e.screenshot(SHOTS / "play.png", scale=2)
            shots.add("play")
    return log


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("frank: ready", 600))
    e.run(60)
    e.screenshot(SHOTS / "title.png", scale=2)
    e.press("FIRE")
    check("FIRE starts the game", e.run_until("frank: state=playing", 100))
    fc0, fo0 = e.peek16("frame_count"), e.peek16("frame_overruns")
    log = bot_game(e)
    fc, fo = e.peek16("frame_count") - fc0, e.peek16("frame_overruns") - fo0
    print(f"  frame rate during the bot's game: {50 * fc / (fc + fo):.1f} fps ({fo} late frames in {fc + fo})")
    scores = [int(s) for s in re.findall(r"frank: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"frank: lives=(\d+)", log)]
    check("hops score points", scores and max(scores) >= 50, str(scores[-5:]))
    check("Frank reaches a pond", "home slot=" in log)
    check("all four ponds: level 2", "frank: level=2" in log,
          "; ".join(l for l in log.splitlines() if "death" in l or "home" in l))
    check("a life is lost", lives and min(lives) < 3, str(lives))
    deaths = re.findall(r"death (\w+)", log)
    check("deaths by traffic or water", deaths and set(deaths) <= {"splat", "splash", "carried", "bank"}, str(deaths))
    check("game over", "state=gameover" in log)
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png", scale=2)
    e.press("FIRE")
    more = e.log()
    for _ in range(40):
        if "hiscore rank" in more or "frank: title" in more:
            break
        e.run(5)
        more += e.log()
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "frank: name=BOT" in more, more[-200:])
        more += e.log() if e.run_until("frank: title", 300) else ""
        more += "frank: title"
    else:
        check(f"score {final} below the table: no entry", final <= 1000)
    check("back to the title", "frank: title" in more)
    e.run(20)
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

with Emulator(machine="800xl") as e:                # 64K, no extra banks
    e.load(PROG)
    ok = e.run_until("frank: ready", 600)
    e.run(30)
    e.press("FIRE")
    check("800XL (64K): boots and starts a game", ok and e.run_until("frank: state=playing", 100))

print(f"{'FAIL' if failures else 'PASS'}: frank_the_frog")
sys.exit(1 if failures else 0)
