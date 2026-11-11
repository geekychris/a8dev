"""Warden Keep: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game through the
Emulator API with the shortest routes (tools/routes.txt): the title, the
guard room (the original's) won, the store's barrels pushed, then the
crypt, then the vault.
Run from the repo root: python3 projects/warden_keep/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
ROUTES = (HERE / "tools" / "routes.txt").read_text().split()
STICK = {"U": "UP", "D": "DOWN", "L": "LEFT", "R": "RIGHT"}
frames = []


def run(emu, n, every=2):
    for i in range(0, n, every):
        emu.run(every)
        frames.append(emu.screenshot())


with Emulator() as emu:
    emu.load(HERE / "build" / "warden_keep.xex")
    assert emu.run_until("warden: ready", 900)
    emu.run(40)
    emu.screenshot(OUT / "title.png")
    run(emu, 40, 4)
    emu.press("FIRE")
    assert emu.run_until("warden: room 1", 300)
    run(emu, 30)
    emu.screenshot(OUT / "play.png")
    for r in range(3):
        for d in ROUTES[r]:
            emu.hold(STICK[d])
            run(emu, 4)
            emu.release()
            run(emu, 6)
            if r == 1 and d == ROUTES[1][9]:
                emu.screenshot(OUT / "store.png")
        assert emu.run_until(f"warden: room {r + 2}", 300)
        run(emu, 24)
    emu.screenshot(OUT / "vault.png")
frames[0].save(OUT / "warden_keep.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{len(frames)} frames")
