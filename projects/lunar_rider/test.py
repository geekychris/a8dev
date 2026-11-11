"""Lunar Rider: a bot plays a game (./a8 test lunar_rider).

Boots to the title, starts with FIRE and rides: it holds FIRE (forward and
upward shots: rocks and UFOs score) and jumps (joystick UP) when a crater,
rock or mine is just ahead, reading the game's terrain ring and
lunar_probe. It must pass a checkpoint (zone 2, +1000) and score. Then it
stops jumping, loses its three lives (deaths logged with their cause), sees
GAME OVER, enters BOT in the high-score table and returns to the title.
Also checks the frame rate while riding (the game's perf log), and that
the screens have content.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
from bot import ride  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "lunar_rider.xex"
failures = []

def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("lunar: ready", 600))
    emu.run(30)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 6)

    emu.press("FIRE")
    check("FIRE starts a game", emu.run_until("lunar: state=PLAYING", 200))
    emu.hold("FIRE")
    emu.hold("RIGHT")                   # full speed

    # ride with the bot until the first checkpoint
    bot = {}
    for f in range(1200):
        ride(emu, bot)
        if f >= 400 and "lunar: checkpoint zone=2" in emu.log(0):
            break
    emu.release("UP")
    log = emu.log(0)
    check("reaches checkpoint 2 (+1000)", "lunar: checkpoint zone=2" in log, log[-300:])
    check("scores", any(l.startswith("lunar: score=") and l != "lunar: score=0"
                        for l in log.splitlines()))
    emu.run(10)
    play = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(play.getcolors(1 << 16) or []) >= 8)
    perf = [int(l.split("late=")[1]) for l in log.splitlines() if l.startswith("lunar: perf")]
    check("frame rate while riding: at most 5 frames in 100 late", perf and max(perf) <= 5, str(perf))
    if perf:
        print(f"    late frames per 100 (full speed, shooting): {perf}  ->  "
              f"{50 * 100 * len(perf) / (100 * len(perf) + sum(perf)):.1f} fps on average, "
              f"{50 * 100 / (100 + max(perf)):.1f} in the worst 2 s")

    # stop jumping: lose the three lives
    emu.release("FIRE")
    emu.release("RIGHT")
    check("loses its lives and the game ends", emu.run_until("lunar: state=GAMEOVER", 6000))
    log = emu.log(0)
    deaths = [l for l in log.splitlines() if l.startswith("lunar: death")]
    check("three deaths, with causes", len(deaths) == 3 and all("cause=" in d for d in deaths), str(deaths))
    emu.run(80)
    emu.press("FIRE")
    check("a high score asks for initials", emu.run_until("lunar: initials rank=", 200))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("lunar: state=TITLE", 200))
    if failures:
        print("\n".join(emu.log(0).splitlines()[-25:]))

print(f"{'FAIL' if failures else 'PASS'}: lunar_rider")
sys.exit(1 if failures else 0)
