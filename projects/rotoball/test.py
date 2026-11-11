"""Rotoball: a bot plays a match (./a8 test rotoball).

Boots to the title (the kick-off positions under the menu), FIRE starts a
match against the CPU: the skimmers at the original's kick-off places, the
floor of both views the original's checker (each line's pixels against its
formula). The bot (tools/bot.py) plays P1 on joystick 1 for the whole 90
seconds: lets the CPU score, then tackles it for the ball and carries it
into the CPU's goal, again and again. Checks the pickups, tackles and goals
(the score is the goals logged), full time after 4500 steps, the frame rate
and the rules at 50 Hz; FIRE, BOT in the best wins, the title; DOWN and FIRE
start a two-player match where joystick 2 drives P2 alone.
"""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import State, Bot, step, FULLTIME, ONE  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "rotoball.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def floor_errors(emu, st):
    """the floor pixels the shown display list points at against the
    original's draw_checker_floor (with exact row distances): wrong, all,
    the worst row's wrong (of 160)"""
    dl = struct.unpack("<H", emu.read_mem(0x230, 2))[0]
    cams = struct.unpack("<4h", emu.read_mem(emu.addr("view_cam") + (8 if dl == 0xA200 else 0), 8))
    p = {"p1": cams[0:2], "p2": cams[2:4]}            # the cameras that list was made for
    d = emu.read_mem(dl, 420)
    i, line, bad, tot, worst = 3, 0, 0, 0, 0
    while i < len(d) and d[i] != 0x41:
        op, m = d[i], d[i] & 15
        if op & 0x40 and m in (2, 0xD, 0xE):
            a = d[i + 1] | d[i + 2] << 8
            i += 3
            if m == 0xD:
                pane = 0 if line < 96 else 1
                k = line - 96 * pane - 32 + 0.5          # the row's two lines
                cam = 1 - pane
                s = 1 if cam == 0 else -1
                cx, cz = p["p1" if cam == 0 else "p2"]
                zd = 270 / k
                data = emu.read_mem(a, 40)
                px = [(data[x >> 2] >> ((3 - (x & 3)) * 2)) & 3 for x in range(160)]
                # the row's cell along the pitch, for the camera +/- 1/64
                # unit (the game's fixed point rounds there: right at a cell
                # edge the whole row may be the next cell's)
                best = 160
                for e in (-1 / 64, 0, 1 / 64):
                    cellx = math.floor((cx / ONE + e + s * zd) / 5)
                    n = 0
                    for x in range(160):
                        cellz = math.floor((cz / ONE - s * (x + 0.5 - 80) * zd / 90) / 5)
                        n += px[x] != (1 if (cellz + cellx) % 2 == 0 else 2)
                    best = min(best, n)
                bad += best
                tot += 160
                if k >= 5:                     # (the far rows' stripes are 2 pixels)
                    worst = max(worst, best)
            line += {2: 8, 0xD: 2, 0xE: 1}[m]
        elif m == 0:
            line += ((op >> 4) & 7) + 1
            i += 1
        else:
            i += 1
    return bad, tot, worst


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("roto: ready", 900))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 6)
    emu.press("FIRE")
    check("FIRE starts a match against the CPU", emu.run_until("roto: start players=1", 200))
    st = State(emu)
    p = st.read(emu)
    check("the kick-off places (P1 -75, P2 75, the ball 0)",
          p["p1"] == (-75 * ONE, 0) and p["p2"] == (75 * ONE, 0) and p["ball"] == (0, 0), str(p))
    emu.run(10)
    bad, tot, worst = floor_errors(emu, st)
    check("both views' floor is the original's checker (< 4% of pixels off, a row < 20%)",
          tot > 9000 and bad * 100 < 4 * tot and worst < 32, f"{bad}/{tot}, a row {worst}/160")
    shot = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(shot.getcolors(1 << 16) or []) >= 6)

    bot, held = Bot(), [""]
    for f in range(5000):
        p = step(emu, st, bot, held)
        if f == 600:
            bad, tot, worst = floor_errors(emu, st)
        if p["state"] == FULLTIME:
            break
    emu.release()
    emu.run_until("roto: fulltime", 50)
    emu.run(5)
    log = emu.log(0)
    goals = [tuple(int(v) for v in m) for m in re.findall(r"roto: goal p1=(\d+) p2=(\d+)", log)]
    check("pickups", "roto: pickup" in log)
    check("tackles", log.count("roto: tackle") >= 3, str(log.count("roto: tackle")))
    check("the CPU scores", any(g[1] for g in goals), str(goals[:3]))
    check("P1 scores", any(g[0] for g in goals), str(goals[-3:]))
    check("full time after 90 s (4500 steps)", "roto: fulltime" in log and 4400 <= f <= 4600, str(f))
    full = re.search(r"roto: fulltime p1=(\d+) p2=(\d+)", log)
    final = (int(full.group(1)), int(full.group(2))) if full else None
    check("the score is the goals", final and goals and final == goals[-1], f"{final} {goals[-1:]}")
    check("the bot wins", final and final[0] > final[1], str(final))
    perf = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in log.splitlines()
            if l.startswith("roto: perf") and l.count("=") == 5]
    frames = sum(int(q["frames"]) for q in perf)
    draws = sum(int(q["draws"]) for q in perf)
    ticks = sum(int(q["ticks"]) for q in perf)
    if frames:
        view = sum(int(q["view"]) for q in perf) // len(perf)
        rules = sum(int(q["rules"]) for q in perf) // len(perf)
        print(f"    {draws} draws, {ticks} steps in {frames} frames: {50 * draws / frames:.1f} fps, "
              f"rules {50 * ticks / frames:.1f} Hz; a picture {view} cycles, a step {rules}")
    check("rules at 50 Hz (a step every frame)", frames and ticks * 100 >= 99 * frames, f"{ticks}/{frames}")
    check(">= 20 fps on average", frames and draws * 50 >= 20 * frames, f"{draws}/{frames}")
    check("the floor in play (< 4% off, a row < 20%)", bad * 100 < 4 * tot and worst < 32,
          f"{bad}/{tot}, a row {worst}/160")
    emu.run(10)
    over = emu.screenshot(HERE / "build" / "test_over.png")
    check("full time screen has content", len(over.getcolors(1 << 16) or []) >= 6)
    emu.run(60)
    emu.press("FIRE")
    check("a best win asks for initials", emu.run_until("roto: initials rank=", 300))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("roto: title", 300))
    emu.run(40)
    emu.press("DOWN")
    emu.run(10)
    emu.press("FIRE")
    check("DOWN, FIRE: a two-player match", emu.run_until("roto: start players=2", 200))
    emu.run(10)
    a = st.read(emu)
    emu.joystick("UP+LEFT", port=1)
    emu.run(60)
    emu.joystick(None, port=1)
    b = st.read(emu)
    check("joystick 2 drives P2 (forward is -X, left is -Z)", b["p2"][0] < a["p2"][0] and b["p2"][1] < a["p2"][1],
          f"{a['p2']} -> {b['p2']}")
    check("... and P1 stays", b["p1"] == a["p1"], f"{a['p1']} -> {b['p1']}")
    if failures:
        print("\n".join(emu.log(0).splitlines()[-25:]))

print(f"{'FAIL' if failures else 'PASS'}: rotoball")
sys.exit(1 if failures else 0)
