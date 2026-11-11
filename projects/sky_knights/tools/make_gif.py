"""Sky Knights: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game with the
test's bot (tools/bot.py) through the Emulator API: the title, wave 1
(the bot jousts the swoopers and takes their pods), then wave 6 (eight
swoopers, raiders and wraiths).
Run from the repo root: .venv/bin/python projects/sky_knights/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import World, Bot, step, PLAYING  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "sky_knights.xex")
    assert emu.run_until("knights: ready", 600)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    for i in range(25):
        frames.append(emu.screenshot())
        emu.run(3)
    emu.press("FIRE")
    assert emu.run_until("knights: start", 100)
    world = World(emu)
    bot, held = Bot(target=100000), [""]
    for f in range(3000):
        w = step(emu, world, bot, held)
        if f % 2 == 0:
            frames.append(emu.screenshot())
        if f == 40:
            emu.screenshot(OUT / "wave.png")
        if f == 170:
            emu.screenshot(OUT / "play.png")
        if w["wave"] == 2:
            break
    # on to wave 6: its number poked during wave 2's intro
    emu.write_mem(world.base + world.off["gs"] + 8, bytes([6, 0]))
    for f in range(400):
        w = step(emu, world, bot, held)
        if f % 2 == 0:
            frames.append(emu.screenshot())
        if f == 200:
            emu.screenshot(OUT / "wave6.png")
    emu.release()
frames[0].save(OUT / "sky_knights.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{len(frames)} frames")
