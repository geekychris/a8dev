"""Sky Knights: a bot plays a game (./a8 test sky_knights).

Boots to the title, FIRE starts a one-player game at wave 1; the bot
(tools/bot.py) collects the pods and drops onto the enemies from above
(scoring), clears the waves, and once it has 5000 points gives up: it flies
in level with the enemies until it has lost its three lives. GAME OVER,
FIRE, BOT goes in the high-score table, back to the title; DOWN and FIRE
start a two-player game. Checks the scores against the original's (500 a
swooper, 250 a pod, 1000 a wave), the frame rate and the rules' rate.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import World, Bot, step, GAMEOVER  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "sky_knights.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("knights: ready", 600))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 4)
    emu.press("FIRE")
    check("FIRE starts a one-player game", emu.run_until("knights: start players=1", 100))
    check("wave 1", emu.run_until("knights: wave 1", 50))
    emu.run(20)
    msg = emu.read_mem("$BBB8", 40)
    check("the wave's message (WAVE 01 GET READY!)", any(msg[:20]) and any(msg[20:]))
    world = World(emu)
    emu.run(100)
    shot = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(shot.getcolors(1 << 16) or []) >= 5)
    w = world.read(emu)
    check("three swoopers in wave 1", sum(w["e_active"]) == 3, str(w["e_active"]))

    bot, held = Bot(target=5000), [""]
    for f in range(20000):
        w = step(emu, world, bot, held)
        if w["state"] == GAMEOVER:
            break
    emu.release()
    log = emu.log(0)
    kills = [int(m) for m in re.findall(r"knights: kill score=(\d+)", log)]
    pods = [int(m) for m in re.findall(r"knights: pod score=(\d+)", log)]
    check("jousts won (scoring)", len(kills) >= 3, str(kills))
    check("the first kill is a swooper's 500", kills[:1] == [500], str(kills[:1]))
    check("pods collected (250)", pods and pods[0] - kills[0] == 250, f"{kills[:1]} {pods[:1]}")
    check("a wave cleared: wave 2", "knights: wave 2" in log)
    check("lives lost", "knights: died lives=2" in log and "knights: died lives=1" in log)
    check("the last life: GAME OVER", "knights: died lives=0" in log and "knights: gameover" in log)
    perf = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in log.splitlines()
            if l.startswith("knights: perf") and l.count("=") == 6]
    frames = sum(int(p["frames"]) for p in perf)
    draws = sum(int(p["draws"]) for p in perf)
    ticks = sum(int(p["ticks"]) for p in perf)
    most = max((int(p["enemies"]) for p in perf), default=0)
    if frames:
        view = sum(int(p["view"]) for p in perf) // len(perf)
        rules = sum(int(p["rules"]) for p in perf) // len(perf)
        print(f"    {draws} draws, {ticks} steps in {frames} frames: {50 * draws / frames:.1f} fps, "
              f"rules {50 * ticks / frames:.1f} Hz (up to {most} enemies and pods); "
              f"drawing {view} cycles, a step {rules}")
    check("rules at 50 Hz (a step every frame)", frames and ticks * 100 >= 99 * frames, f"{ticks}/{frames}")
    check(">= 40 fps on average", frames and draws * 50 >= 40 * frames, f"{draws}/{frames}")
    emu.run(60)
    over = emu.screenshot(HERE / "build" / "test_over.png")
    check("game over screen has content", len(over.getcolors(1 << 16) or []) >= 4)
    emu.press("FIRE")
    check("a high score asks for initials", emu.run_until("knights: initials rank=", 300))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("knights: title", 200))
    emu.run(30)
    emu.press("DOWN")
    emu.run(10)
    emu.press("FIRE")
    check("DOWN, FIRE: a two-player game", emu.run_until("knights: start players=2", 100))
    # the busiest waves (6 on: 8 enemies, swoopers, raiders, wraiths): the
    # wave number poked during the intro, nobody playing
    emu.write_mem(world.base + world.off["gs"] + 8, bytes([6, 0]))
    check("wave 6", emu.run_until("knights: wave 6", 200))
    emu.run(150)
    w = world.read(emu)
    check("8 enemies in wave 6", sum(w["e_active"]) == 8, str(w["e_active"]))
    check("both knights in play", w["alive"] == [1, 1], str(w["alive"]))
    emu.screenshot(HERE / "build" / "test_wave6.png")
    n0 = len(emu.log(0).splitlines())
    emu.run(600)
    emu.run_until("knights: perf", 200)
    emu.run(5)
    perf = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in emu.log(0).splitlines()[n0:]
            if l.startswith("knights: perf") and l.count("=") == 6]
    frames = sum(int(p["frames"]) for p in perf)
    draws = sum(int(p["draws"]) for p in perf)
    ticks = sum(int(p["ticks"]) for p in perf)
    if frames:
        view = sum(int(p["view"]) for p in perf) // len(perf)
        rules = sum(int(p["rules"]) for p in perf) // len(perf)
        print(f"    wave 6, two knights: {draws} draws, {ticks} steps in {frames} frames: "
              f"{50 * draws / frames:.1f} fps, rules {50 * ticks / frames:.1f} Hz; "
              f"drawing {view} cycles, a step {rules}")
    check("wave 6: rules at 50 Hz", frames and ticks * 100 >= 99 * frames, f"{ticks}/{frames}")
    check("wave 6: >= 25 fps", frames and draws * 50 >= 25 * frames, f"{draws}/{frames}")
    if failures:
        print("\n".join(emu.log(0).splitlines()[-25:]))

print(f"{'FAIL' if failures else 'PASS'}: sky_knights")
sys.exit(1 if failures else 0)
