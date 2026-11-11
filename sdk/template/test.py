"""Smoke test for __NAME__ (run by `./a8 test __NAME__`).

Boots headless, waits for the ready line, moves with the joystick and the
keys, fires, presses START, checks the log and that something was drawn.
Extend it as the program grows.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "__NAME__.xex"
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("boots to ready", emu.run_until("__NAME__: ready", 500))
    emu.run(5)
    emu.press("RIGHT", hold_frames=2)                  # joystick 0
    check("joystick moves right", emu.run_until("__NAME__: move x=121", 20))
    emu.press("A", hold_frames=2)                      # keyboard: A = left
    check("A moves left", emu.run_until("__NAME__: move x=120", 20))
    emu.press("FIRE")
    check("FIRE beeps", emu.run_until("__NAME__: fire", 20))
    emu.press("START")
    check("START", emu.run_until("__NAME__: start", 20))
    img = emu.screenshot(HERE / "build" / "test.png")
    check("screen has content", len(img.getcolors(1 << 16) or []) >= 3)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: __NAME__")
sys.exit(1 if failures else 0)
