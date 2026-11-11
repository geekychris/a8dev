"""Rotoball: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game with the
test's bot (tools/bot.py) through the Emulator API: the title, the start of
a match (the CPU takes the ball and scores), the bot's tackle and its run
to the CPU's goal.
Run from the repo root: .venv/bin/python projects/rotoball/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import State, Bot, step  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "rotoball.xex")
    assert emu.run_until("roto: ready", 900)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    for i in range(20):
        frames.append(emu.screenshot())
        emu.run(3)
    emu.press("FIRE")
    assert emu.run_until("roto: start", 200)
    st = State(emu)
    bot, held = Bot(), [""]
    shots = {"play": False, "carry": False}
    goals = 0
    for f in range(2400):
        p = step(emu, st, bot, held)
        if f % 3 == 0:
            frames.append(emu.screenshot())
        if not shots["play"] and p["carrier"] == 1 and f > 120:
            emu.screenshot(OUT / "play.png")
            shots["play"] = True
        if not shots["carry"] and p["carrier"] == 0 and p["p1"][0] > 30 * 256:
            emu.screenshot(OUT / "carry.png")
            shots["carry"] = True
        if p["score"][0] >= 1 and f % 3 == 0:
            goals += 1
            if goals > 30:
                break
    emu.release()
frames[0].save(OUT / "rotoball.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=60, optimize=True)
print(f"{len(frames)} frames", shots)
