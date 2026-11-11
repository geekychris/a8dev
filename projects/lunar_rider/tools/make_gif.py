"""Lunar Rider: the GIF and the checkpoint picture (screenshots/).

`./a8 video` records without input, so this drives the game with the
test's bot (tools/bot.py) through the Emulator API: the title for a
second, then a ride at full speed, shooting, to the first checkpoint.
Run from the repo root: .venv/bin/python projects/lunar_rider/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import ride  # noqa: E402

OUT = HERE / "screenshots"
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "lunar_rider.xex")
    assert emu.run_until("lunar: ready", 600)
    for i in range(25):                          # the title, 1 s
        frames.append(emu.screenshot())
        emu.run(2)
    emu.press("FIRE")
    assert emu.run_until("lunar: state=PLAYING", 200)
    emu.hold("FIRE")
    emu.hold("RIGHT")
    bot = {}
    saved = False
    for f in range(330):
        ride(emu, bot)
        if f % 2 == 0:
            frames.append(emu.screenshot())
        if not saved and "lunar: checkpoint" in emu.log(0):
            emu.run(4)
            emu.screenshot(OUT / "checkpoint.png")
            saved = True
OUT.mkdir(exist_ok=True)
frames[0].save(OUT / "lunar_rider.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{OUT / 'lunar_rider.gif'}: {len(frames)} frames; checkpoint.png: {saved}")
