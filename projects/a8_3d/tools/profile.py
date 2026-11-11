#!/usr/bin/env python3
"""Sampling profiler for a8dev programs: where does the CPU time go?

Runs the program headless to a log line, then stops the emulator at evenly
spaced machine cycles (run_to(frame, cycle)), reads PC and counts samples
per symbol (the nearest public label at or below PC: asm routines, C
functions, cc65 runtime helpers).

    .venv/bin/python projects/a8_3d/tools/profile.py projects/a8_3d/build/a8_3d.xex \
        --until "a8_3d: ready" --frames 100 --every 997 [--press RIGHT@50] [--fine]

--fine keeps name+offset (hot instructions inside a routine).
"""
import argparse
import collections
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xex")
    ap.add_argument("--until", default=None)
    ap.add_argument("--settle", type=int, default=20, help="frames after --until before sampling")
    ap.add_argument("--frames", type=int, default=50)
    ap.add_argument("--every", type=int, default=997, help="cycles between samples")
    ap.add_argument("--fine", action="store_true")
    ap.add_argument("--machine", default="130xe")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--press", action="append", default=[], help="KEYS before sampling (press, 4 frames)")
    ap.add_argument("--hold", default=None, help="KEYS held while sampling")
    a = ap.parse_args()

    with Emulator(machine=a.machine) as emu:
        emu.load(a.xex)
        if a.until and not emu.run_until(a.until, 6000):
            sys.exit(f"never saw {a.until!r}")
        for k in a.press:
            emu.press(k, hold_frames=4)
            emu.run(10)
        if a.hold:
            emu.hold(a.hold)
        emu.run(a.settle)
        cpf = emu.cycles_per_frame
        pos = emu.position()
        start = pos["since_load"] * cpf + pos["tstates"]
        end = start + a.frames * cpf
        counts = collections.Counter()
        n = 0
        t = start + a.every
        while t < end:
            emu.run_to(t // cpf, t % cpf)
            pc = emu.regs()["PC"]
            name = emu.symbols.name_of(pc) or f"${pc:04x}"
            if not a.fine:
                name = name.split("+")[0]
            counts[name] += 1
            n += 1
            t += a.every
        print(f"{n} samples over {a.frames} frames ({a.every} cycles apart)")
        for name, c in counts.most_common(a.top):
            print(f"{100.0 * c / n:6.1f}%  {c:5d}  {name}")


if __name__ == "__main__":
    main()
