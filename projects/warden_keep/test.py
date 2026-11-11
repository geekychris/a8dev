"""Warden Keep: a scripted play-through (./a8 test warden_keep).

Game 1 (timed): FIRE begins; the shortest routes (tools/rooms.py,
tools/routes.txt) win all four rooms with the joystick, one press a step:
jewels (100 each), pushes, a time bonus each room, the rooms' changes; the
win screen, the initials ACE, back to the title.
Game 2: one jewel, then the clock runs out (time up: the game lost).
Game 3: SELECT picks the relaxed game; a barrel pushed, the warden hidden
by a barrel in front of it, OPTION restarts the room, 65 seconds without a
time up, ESC back to the title.
Holding the stick: the original's auto-repeat (12 frames, then every 6).
Then the same first room on a 64K 800XL. Checks the frame rate.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "warden_keep.xex"
ROUTES = (HERE / "tools" / "routes.txt").read_text().split()
STICK = {"U": "UP", "D": "DOWN", "L": "LEFT", "R": "RIGHT"}
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def walk(emu, route):
    """one tap a step (3 frames down, 5 up: a quick player's)"""
    for d in route:
        emu.press(STICK[d], hold_frames=3, release_frames=3)
        emu.run(2)


def start(emu):
    emu.press("FIRE")
    return emu.run_until("warden: room 1", 300)


def warden_bits(emu):
    """the bits set in players 0-3 around the warden, and in its picture"""
    pm = emu.read_mem(0x9C00, 1024, space="ram")
    return sum(bin(b).count("1") for b in pm)


def player_pm_bits(emu, facing):
    h = 36
    d = emu.read_mem("player_pm", 4 * 4 * h, space="ram")
    return sum(bin(b).count("1") for b in d[facing * 4 * h:(facing + 1) * 4 * h])


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("warden: ready", 900))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 3)

    # ---- game 1: the four rooms
    check("FIRE begins: room 1", start(emu))
    emu.run(10)
    check("timed game", "warden: start mode=timed" in emu.log_text(0))
    shot = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(shot.getcolors(1 << 16) or []) >= 5)
    full = emu.read_mem("player_pm", 1, space="ram") is not None and player_pm_bits(emu, 0)
    check("the warden: whole when nothing is in front of it", warden_bits(emu) == full,
          f"{warden_bits(emu)} vs {full}")
    for i, route in enumerate(ROUTES):
        mark = emu.log_end()
        walk(emu, route)
        ok = emu.run_until(f"warden: cleared room={i + 1}", 200, since=mark)
        log = emu.log_text(mark)
        check(f"room {i + 1}: the route ({len(route)} steps) takes every jewel", ok)
        if i == 0:
            jw = [int(m) for m in re.findall(r"warden: jewel score=(\d+)", log)]
            m = re.search(r"cleared room=1 bonus=(\d+) score=(\d+)", log)
            check("jewels: 100 each (the last with the bonus)", m and jw == [100, 200, int(m[2])], str(jw))
            check("room 1: a time bonus (10 a second left)", m and int(m[1]) > 0 and int(m[1]) % 10 == 0
                  and int(m[2]) == 300 + int(m[1]), m and m[0])
        if i < 3:
            check(f"room {i + 2}", emu.run_until(f"warden: room {i + 2}", 200))
            emu.run(10)
            if i == 1:
                emu.screenshot(HERE / "build" / "test_room3.png")
    log = emu.log_text(0)
    check("barrels pushed on the way", log.count("warden: push") >= 3, str(log.count("warden: push")))
    check("the win", emu.run_until("warden: win score=", 300))
    emu.run(10)
    win = int(re.findall(r"warden: win score=(\d+)", emu.log_text(0))[-1])
    check("4 rooms: 1200 + the time bonuses", win > 1200, str(win))
    emu.screenshot(HERE / "build" / "test_win.png")
    check("a high score asks for initials (after 200 frames)", emu.run_until("warden: initials rank=1", 400))
    emu.run(10)
    emu.type("ACE")
    check("ACE goes in the table", emu.run_until("name=ACE score=", 200))
    check("back to the title", emu.run_until("warden: title", 300))
    emu.run(30)

    # ---- game 2: time up
    check("game 2", start(emu))
    walk(emu, "L")
    check("a jewel", emu.run_until("warden: jewel score=100", 60))
    check("the clock runs out: time up", emu.run_until("warden: timeup score=100", 60 * 50 + 100))
    emu.run(200)
    emu.screenshot(HERE / "build" / "test_timeup.png")
    emu.press("FIRE")
    check("no table for 100: back to the title", emu.run_until("warden: title", 300))
    emu.run(30)

    # ---- game 3: relaxed; a push, the warden hidden, OPTION, no clock, ESC
    emu.press("SELECT")
    check("SELECT: the relaxed game", emu.run_until("warden: mode relaxed", 30))
    check("game 3", start(emu))
    check("relaxed", "warden: start mode=relaxed" in emu.log_text(0))
    emu.run(10)
    mark = emu.log_end()
    walk(emu, "U")
    check("a push: the barrel at 3,5 to 3,4", "warden: push 3,4" in emu.log_text(mark))
    walk(emu, "DRUU")
    emu.run(3)
    check("at 4,4", "warden: at 4,4" in emu.log_text(mark))
    hidden = warden_bits(emu)
    full = player_pm_bits(emu, 0)
    check("the barrel at 5,5 in front hides the warden's foot", 0 < hidden < full, f"{hidden} of {full}")
    emu.screenshot(HERE / "build" / "test_behind.png")
    # auto-repeat: hold DOWN 30 frames: steps at 0, 12, 18, 24 (4,5 4,6 4,7 then the edge)
    mark = emu.log_end()
    emu.hold("DOWN")
    emu.run(30)
    emu.release()
    emu.run(2)
    steps = re.findall(r"warden: at (\d,\d)", emu.log_text(mark))
    check("held: the original's auto-repeat (12 frames, then 6)", steps == ["4,5", "4,6", "4,7"], str(steps))
    emu.press("OPTION")
    check("OPTION restarts the room", emu.run_until("warden: restart room=1", 30))
    mark = emu.log_end()
    emu.run(65 * 50)
    check("relaxed: no clock (65 s)", "timeup" not in emu.log_text(mark))
    perf = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in emu.log_text(0).splitlines()
            if l.startswith("warden: perf")]
    frames = sum(int(q["frames"]) for q in perf)
    loops = sum(int(q["loops"]) for q in perf)
    worst = max(int(q["max"]) for q in perf) if perf else 0
    avg = sum(int(q["cycles"]) for q in perf) // max(1, len(perf))
    print(f"    {loops} frames drawn in {frames}: {50 * loops / max(1, frames):.1f} fps; "
          f"a frame's work {avg} cycles on average, {worst} at most")
    check("50 fps (a picture every frame)", frames and loops * 100 >= 99 * frames, f"{loops}/{frames}")
    emu.press("ESC")
    check("ESC: back to the title", emu.run_until("warden: quit", 30) and emu.run_until("warden: title", 100))
    if failures:
        print("\n".join(emu.log(0).splitlines()[-30:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    emu.run_until("warden: ready", 900)
    emu.run(40)
    start(emu)
    walk(emu, ROUTES[0])
    check("64K (800XL): room 1 won", emu.run_until("warden: cleared room=1", 200))

print(f"{'FAIL' if failures else 'PASS'}: warden_keep")
sys.exit(1 if failures else 0)
