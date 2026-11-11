"""The arcade disk: boots into the menu, moves the selection, starts a game,
and RESET comes back to the menu. Builds the disk first (./a8 arcade --image-only)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev import arcade  # noqa: E402
from a8dev.emu import Emulator  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


m = arcade.build(do_build=True, log=lambda *_: None)
gs = m["games"]
check("games registered", len(gs) >= 2, str([g["project"] for g in gs]))
hello = arcade.find_game(gs, "hello")
with Emulator() as emu:
    emu.load(m["image"])
    sel = arcade.menu_ready(emu, 3000)
    check("disk boots into the menu", sel == 0)
    emu.run(2)                                 # the last frame was drawn mid-update
    img = emu.screenshot(ROOT / "build" / "arcade" / "menu.png")
    check("menu drawn", len(img.getcolors(1 << 16) or []) >= 3)
    check("choose hello and start it", arcade.menu_choose(emu, hello, len(gs), sel=sel))
    emu.run(10)
    emu.reset(hard=False)                      # RESET key: warm start -> DOSINI -> menu
    check("RESET returns to the menu", arcade.menu_ready(emu, 3000) is not None)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: arcade")
sys.exit(1 if failures else 0)
