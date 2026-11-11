#!/usr/bin/env python3
"""The README's pictures and GIF: the attract mode (the original's demo
player) plays the Comet Tour table.

    .venv/bin/python projects/boing_pinball/tools/make_gif.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
SHOTS = HERE / "screenshots"
SHOTS.mkdir(exist_ok=True)
frames = []
with Emulator(machine="130xe") as emu:
    emu.load(HERE / "build" / "boing_pinball.xex")
    emu.run_until("pb: ready", 3000)
    emu.run(60)
    emu.screenshot(SHOTS / "title.png", scale=2)
    for i in range(240):
        emu.run(3)
        frames.append(emu.screenshot(None).convert("RGB"))
        if i == 120:
            emu.screenshot(SHOTS / "play.png", scale=2)
    import struct
    for i in range(3000):                      # the ball down at the flippers
        emu.run(2)
        x, y = struct.unpack("<2h", emu.read_mem(emu.addr("_probe"), 4, space="ram"))
        if 0 < y < 3500:
            emu.run(6)
            break
    emu.screenshot(SHOTS / "flippers.png", scale=2)
frames[0].save(SHOTS / "boing_pinball.gif", save_all=True, append_images=frames[1:], duration=60, loop=0, optimize=True)
print(len(frames), "frames")
