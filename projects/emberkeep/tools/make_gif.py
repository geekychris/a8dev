"""Emberkeep: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game with the
test's bot (tools/bot.py) through the Emulator API: the title, the first
chamber by the lantern's light (a rune, the oil, the portal), the second.
Run from the repo root: .venv/bin/python projects/emberkeep/tools/make_gif.py
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
    emu.load(HERE / "build" / "emberkeep.xex")
    assert emu.run_until("ember: ready", 900)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    for i in range(16):
        frames.append(emu.screenshot())
        emu.run(4)
    emu.press("FIRE")
    assert emu.run_until("ember: chamber 1", 300)
    st = State(emu)
    bot, held = Bot(), [""]
    shots = set()
    for f in range(3000):
        s = step(emu, st, bot, held)
        if f % 3 == 0:
            frames.append(emu.screenshot())
        if f == 150:
            emu.screenshot(OUT / "play.png")
        if s["level"] == 0 and s["runes"] == 0 and "portal" not in shots:
            emu.screenshot(OUT / "portal.png")
            shots.add("portal")
        if s["level"] == 1 and "cellar" not in shots and f % 3 == 0 and s["px"] > 100:
            emu.screenshot(OUT / "cellar.png")
            shots.add("cellar")
        if s["level"] == 2:
            break
    emu.release()
frames[0].save(OUT / "emberkeep.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=60, optimize=True)
print(f"{len(frames)} frames", shots)
