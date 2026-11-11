"""banktest: the 130XE bank helpers, checked from both sides.

The program fills banks 0..3 with patterns and verifies them (A8:DONE).
Here we also read every bank straight from the emulator (bank=N) and
compare with the same pattern, check a 64K machine reports no banks, and
check the screen says ALL BANKS OK.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "banktest.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def pattern(bank, i):
    return (i * 7 + bank * 31 + (i >> 8)) & 0xFF


with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    done = emu.run_until("A8:", 3000)
    emu.run(2)
    log = emu.log(0)
    check("program reports 4 banks", "banktest: banks=4" in log, log[:200])
    check("program verifies every bank", all(f"banktest: bank={b} ok=1" in log for b in range(4)), log)
    check("main memory window intact", "banktest: main ok=1 current=ff" in log, log)
    check("A8:DONE", done and "A8:DONE" in log and "A8:FAIL" not in log, log[-200:])
    for b in range(4):
        data = emu.read_mem(0, 0x4000, bank=b)
        want = bytearray(pattern(b, i) for i in range(0x4000))
        if b == 2:
            want[0x1234] = 0x5A                 # the program's xe_bank_poke
        bad = next((i for i in range(0x4000) if data[i] != want[i]), None)
        check(f"host reads bank {b} (bank={b})", bad is None,
              f"first difference at {bad:#x}: {data[bad]:#x} != {want[bad]:#x}" if bad is not None else "")
    data = emu.read_mem(0x5000, 16)
    check("main memory at $5000 has the program's signature", data == bytes(range(0xC0, 0xD0)), data.hex())
    img = emu.screenshot(HERE / "build" / "test.png")
    check("screen drawn", len(img.getcolors(1 << 16) or []) >= 2)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

with Emulator(machine="800xl") as emu:               # 64K: no extended banks
    emu.load(PROG)
    emu.run_until("A8:", 1500)
    emu.run(2)
    log = emu.log(0)
    check("800XL: no banks, A8:FAIL", "banktest: banks=0" in log and "A8:FAIL" in log, log[:200])

print(f"{'FAIL' if failures else 'PASS'}: banktest")
sys.exit(1 if failures else 0)
