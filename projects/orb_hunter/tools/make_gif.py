"""Orb Hunter: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game with the
test's bot (tools/bot.py) through the Emulator API: the title, the start
room, down into the caverns, a fight, the Spring Boots.
Run from the repo root: .venv/bin/python projects/orb_hunter/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import Bot, step  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "orb_hunter.xex")
    assert emu.run_until("orb: ready", 600)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    for i in range(20):
        frames.append(emu.screenshot())
        emu.run(3)
    emu.press("FIRE")
    assert emu.run_until("orb: start", 100)
    emu.run(70)
    emu.screenshot(OUT / "start.png")
    bot, held = Bot(target=10000), [""]
    got = fight = False
    for f in range(900):
        p = step(emu, bot, held)
        if f % 2 == 0:
            frames.append(emu.screenshot())
        if not fight and p["enemies"] and f > 150 and abs(p["ey"] - p["hy"]) < 8:
            emu.screenshot(OUT / "play.png")
            fight = True
        if p["state"] == 4 and not got:
            emu.screenshot(OUT / "item.png")
            got = True
            for k in range(25):
                emu.run(2)
                frames.append(emu.screenshot())
            break
    emu.release()
frames[0].save(OUT / "orb_hunter.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{len(frames)} frames; fight {fight}, item {got}")
