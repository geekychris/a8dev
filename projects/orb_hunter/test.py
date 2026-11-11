"""Orb Hunter: a bot plays a game (./a8 test orb_hunter).

Boots to the title, starts with FIRE in the start room (2,2); the bot
(tools/bot.py) runs right down the gap into the caverns and on to the
Spring Boots in room 3,3 (a power-up: its message, the high jump), shoots
the creatures there (scoring), then lets them wear its energy down: dead,
GAME OVER, FIRE, BOT goes in the high-score table, back to the title.
Checks the world (the rooms it passes are the original's, by their
tiles), the frame rate in rooms, and that the screens have content.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import Bot, step, probe  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "orb_hunter.xex"
WORLD = Path.home() / "code/claude_world/spectrum_dev/projects/orb_hunter/gfx/world.bin"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("orb: ready", 600))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 4)
    emu.press("FIRE")
    check("FIRE starts a game in the start room", emu.run_until("orb: start room=2,2", 100))
    emu.run(70)
    # the room in memory is the original's (levels.c, via the Spectrum port's world.bin)
    if WORLD.exists():
        w = WORLD.read_bytes()
        room = emu.read_mem("room", 320)
        check("the start room is the original's", room == w[18 * 320:19 * 320])
    shot = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(shot.getcolors(1 << 16) or []) >= 5)

    bot, held = Bot(target=200), [""]
    rooms, item = set(), False
    for f in range(9000):
        p = step(emu, bot, held)
        rooms.add((p["room_x"], p["room_y"]))
        if p["state"] == 3:                         # GAMEOVER
            break
    emu.release()
    log = emu.log(0)
    check("into the caverns (another room)", len(rooms) >= 2, str(rooms))
    check("a power-up: SPRING BOOTS", "orb: item SPRING BOOTS" in log, log[-400:])
    scores = [int(m) for m in re.findall(r"orb: score=(\d+)", log)]
    check("scores (creatures shot)", scores and max(scores) >= 100, str(scores[-3:]))
    check("hit by creatures", "orb: hit health=" in log)
    check("energy runs out: dead, GAME OVER", "orb: dead" in log and "orb: gameover" in log)
    perf = [d for d in (dict(re.findall(r"(\w+)=(\d+)", l)) for l in log.splitlines()
                        if l.startswith("orb: perf")) if "room" in d]
    frames = sum(int(p["frames"]) for p in perf)
    draws = sum(int(p["draws"]) for p in perf)
    ticks = sum(int(p["ticks"]) for p in perf)
    if frames:
        print(f"    {draws} draws, {ticks} steps in {frames} frames: {50 * draws / frames:.1f} fps, "
              f"rules {50 * ticks / frames:.1f} Hz")
    check("rules at 50 Hz (a step every frame)", frames and ticks * 100 >= 97 * frames, f"{ticks}/{frames}")
    check(">= 40 fps on average", frames and draws * 50 >= 40 * frames, f"{draws}/{frames}")
    emu.run(80)
    emu.press("FIRE")
    check("a high score asks for initials", emu.run_until("orb: initials rank=", 300))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("orb: title", 200))
    if failures:
        print("\n".join(emu.log(0).splitlines()[-25:]))

print(f"{'FAIL' if failures else 'PASS'}: orb_hunter")
sys.exit(1 if failures else 0)
