"""Uranus Lander: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game through the
Emulator API: the title, then a game where the autopilot (OPTION) lands
on level 1. Run from the repo root:
  .venv/bin/python projects/uranus_lander/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "uranus_lander.xex")
    assert emu.run_until("uranus: ready", 600)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    for i in range(20):
        frames.append(emu.screenshot())
        emu.run(3)
    emu.press("FIRE")
    assert emu.run_until("uranus: level=1", 300)
    emu.run(20)
    emu.press("OPTION")
    landed = False
    for i in range(400):
        emu.run(2)
        frames.append(emu.screenshot())
        if i == 60:
            emu.screenshot(OUT / "play.png")
        if "uranus: landed" in emu.log(0) and not landed:
            landed = True
            emu.run(4)
            emu.screenshot(OUT / "landed.png")
            for k in range(20):
                emu.run(2)
                frames.append(emu.screenshot())
            break
frames[0].save(OUT / "uranus_lander.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{len(frames)} frames, landed: {landed}")
