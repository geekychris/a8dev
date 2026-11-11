#!/usr/bin/env python3
"""Where Spectral Keep's time goes: sample the PC every 1/32 frame (of machine
cycles: so the samples are time, not instructions) while the
attract demo plays, and name each sample from the linker's debug file, by
segment: code in the bank window is charged to the bank mapped at the time
(bank 0 the physics, bank 1 the rules, none the front end), so overlapping
addresses get the right names. C runtime helpers are also charged to the
C function that called them (the second list).

    .venv/bin/python projects/spectral_keep/tools/profile.py [frames] [skip]
    .venv/bin/python projects/spectral_keep/tools/profile.py trace FUNC [skip]
        (every instruction of one call of FUNC, counted by function)
"""
import bisect
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
PROG = HERE / "build" / "spectral_keep.xex"
trace = len(sys.argv) > 2 and sys.argv[1] == "trace"
frames = 0 if trace else int(sys.argv[1]) if len(sys.argv) > 1 else 60
skip = int(sys.argv[3 if trace else 2]) if len(sys.argv) > (3 if trace else 2) else 300

segs, syms = {}, collections.defaultdict(list)
for line in open(HERE / "build" / "spectral_keep.dbg"):
    if line.startswith("seg\t"):
        d = dict(re.findall(r'(\w+)=("[^"]*"|[^,\s]+)', line))
        segs[int(d["id"])] = (d["name"].strip('"'), int(d["start"], 16), int(d["size"], 16))
    elif line.startswith("sym\t") and "type=lab" in line:
        d = dict(re.findall(r'(\w+)=("[^"]*"|[^,\s]+)', line))
        if "seg" in d and "val" in d:
            name = d["name"].strip('"')
            if re.match(r"L[0-9A-F]{4}$", name) or name.startswith("@") or re.match(r"L\d+$", name):
                continue
            syms[int(d["seg"])].append((int(d["val"], 16), name))
for k in syms:
    syms[k].sort()
BANK_SEGS = {0: ("B0CODE", "B0RO"), 1: ("B1CODE", "B1RO")}


def name_of(pc, bank):
    want = None                         # (no banks here)
    for sid, (sname, start, size) in segs.items():
        if not (start <= pc < start + size):
            continue
        if want and sname not in want:
            continue
        lst = syms.get(sid, [])
        i = bisect.bisect_right(lst, (pc, "\xff")) - 1
        return lst[i][1] if i >= 0 else sname
    return f"${pc:04X}"


with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    emu.run_until("sk: ready", 3000)
    emu.run(skip)
    if trace:
        emu.break_at(sys.argv[2])
        emu.continue_(500)
        emu.delete()
        ret = emu.backtrace(2)[1]["return"]
        cnt, n, c0 = collections.Counter(), 0, emu.position()["cycles"]
        while n < 100000:
            emu.stepi(1)
            pc = emu.regs()["PC"]
            if pc == ret:
                break
            cnt[name_of(pc, emu.memory_map().get("cpu_bank"))] += 1
            n += 1
        print(f"{sys.argv[2]}: {n} instructions, {emu.position()['cycles'] - c0} cycles")
        for s_, k in cnt.most_common(25):
            print(f"  {k:6d} {s_}")
        sys.exit(0)
    cyc = emu.cycles_per_frame
    own, caller = collections.Counter(), collections.Counter()
    for f in range(frames):
        for k in range(32):
            bank = emu.memory_map().get("cpu_bank")
            bt = emu.backtrace(6)
            s0 = name_of(bt[0]["pc"], bank)
            own[s0] += 1
            c = s0
            if not s0.startswith("_"):
                for b in bt[1:]:
                    if "caller" in b:
                        c = name_of(b["caller"], bank)
                        if c.startswith("_"):
                            break
            caller[c] += 1
            pos = emu.position()                 # every 1/32 frame of machine cycles
            nxt = pos["cycles"] + cyc // 32
            emu.run_to(nxt // cyc, nxt % cyc)
    t = sum(own.values())
    print(f"{t} samples over {frames} frames")
    for s, n in own.most_common(18):
        print(f"  {100 * n / t:5.1f}% {s}")
    print("C functions (runtime helpers charged to their caller):")
    for s, n in caller.most_common(18):
        print(f"  {100 * n / t:5.1f}% {s}")
    print(emu.log(0)[-240:])
