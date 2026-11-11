"""Tank Duel: the pictures and the GIF (screenshots/). Drives the game
through the Emulator API: the title (the autopilot fighting behind it),
then a game started with FIRE and handed to the autopilot (SELECT).
Run from the repo root: python3 projects/tank_duel/tools/make_gif.py
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
    emu.load(HERE / "build" / "tank_duel.xex")
    assert emu.run_until("td: ready", 1500)
    emu.run(200)
    emu.screenshot(OUT / "title.png")
    emu.press("FIRE", hold_frames=6)
    emu.run_until("td: start", 300)
    emu.press("SELECT", hold_frames=6)
    for i in range(300):
        emu.run(4)
        frames.append(emu.screenshot())
        if i == 120:
            emu.screenshot(OUT / "play.png")
        if i == 220:
            emu.screenshot(OUT / "fight.png")
frames[0].save(OUT / "tank_duel.gif", save_all=True, append_images=frames[1:], loop=0, duration=80, optimize=True)
print(len(frames), "frames")
