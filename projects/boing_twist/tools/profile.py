#!/usr/bin/env python3
"""Where Boing Twist's time goes: sample the PC at 8 points a frame over a
stretch of play and count the functions (the nearest symbol at or below
the PC, from build/boing_twist.lbl; the C runtime's helpers are charged to
their own names, the caller is in the backtrace's next frame).

    .venv/bin/python projects/boing_twist/tools/profile.py [frames] [title|scramble|play]
"""
import collections
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

PROG = Path(__file__).resolve().parents[1] / "build" / "boing_twist.xex"
frames = int(sys.argv[1]) if len(sys.argv) > 1 else 200
where = sys.argv[2] if len(sys.argv) > 2 else "scramble"


def press(emu, k, n=12):
    emu.hold(k)
    emu.run(n)
    emu.release()


with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    emu.run_until("bt: ready", 1500)
    if where != "title":
        emu.run(30)
        press(emu, "FIRE")
        emu.run_until("screen=SELECT", 2000)
        emu.run(30)
        if where == "play":
            press(emu, "RIGHT")             # Medium
            emu.run(60)
        press(emu, "FIRE")
        emu.run_until("screen=PLAY", 2000)
    cyc = emu.cycles_per_frame
    own, caller = collections.Counter(), collections.Counter()
    for f in range(frames):
        for k in range(8):
            bt = emu.backtrace(3)
            s0 = bt[0].get("symbol", "?").split("+")[0]
            pc = bt[0].get("pc", 0)
            if 0x4000 <= pc < 0x8000:
                cb = emu.memory_map().get("cpu_bank")
                if cb is not None:           # a bank is mapped for the CPU: a view's code
                    s0 = f"(bank {cb} view code)"
                    own[s0] += 1
                    caller[s0] += 1
                    emu.stepi(cyc // 8 // 4)
                    continue
            own[s0] += 1
            c = next((b.get("caller_symbol", "?").split("+")[0] for b in bt[1:] if b.get("caller_symbol", "").startswith("_")), s0)
            caller[c if not s0.startswith("_") else s0] += 1
            emu.stepi(cyc // 8 // 4)       # about an eighth of a frame (4 cycles an instruction)
    total = sum(own.values())
    print(f"{total} samples over {frames} frames ({where})")
    print("own:")
    for s, n in own.most_common(15):
        print(f"  {100 * n / total:5.1f}% {s}")
    print("C function (helpers charged to their caller):")
    for s, n in caller.most_common(15):
        print(f"  {100 * n / total:5.1f}% {s}")
    print(emu.log(0)[-300:])
