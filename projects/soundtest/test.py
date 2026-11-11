"""soundtest: the asm sound_tick against a model of the original C player,
frame by frame (POKEY AUDF/AUDC), and its cost idle vs. busy."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


clk = 1773447 / 28
NOTE = [max(0, min(255, round(clk / (2 * 440 * 2 ** ((m - 69) / 12))) - 1)) for m in range(48, 108)]


class Model:
    """sound.c as it was in C (SDK 1.1), POKEY registers as a dict."""
    def __init__(self):
        self.audf, self.audc = [0] * 4, [0] * 4
        self.tr = [None] * 4
        self.sfx = [None] * 4

    def tone(self, ch, f, d, v):
        self.audf[ch], self.audc[ch] = f & 255, d | (v & 15)

    def note(self, ch, n, v):
        if n < 48 or n > 107:
            self.audc[ch] = 0
        else:
            self.tone(ch, NOTE[n - 48], 0xA0, v)

    def music(self, ch, track, vol):
        self.tr[ch] = {"t": track, "i": 0, "left": 0, "vol": vol}

    def stop(self):
        for i in range(4):
            if self.tr[i] and not self.sfx[i]:
                self.audc[i] = 0
            self.tr[i] = None

    def effect(self, ch, f, d, v, decay, sweep):
        self.sfx[ch] = {"f": f, "d": d, "v": v, "decay": decay or 1, "sweep": sweep}
        self.tone(ch, f, d, v)

    def tick(self):
        for i in range(4):
            t, s = self.tr[i], self.sfx[i]
            if t and t["left"] == 0:
                if t["t"][t["i"]][1] == 0:
                    t["i"] = 0
                t["left"] = t["t"][t["i"]][1]
                if not s:
                    self.note(i, t["t"][t["i"]][0], t["vol"])
                t["i"] += 1
            if t:
                t["left"] -= 1
            if s:
                if s["v"] <= s["decay"]:
                    self.sfx[i] = None
                    self.audc[i] = 0
                else:
                    s["v"] -= s["decay"]
                    s["f"] = (s["f"] + s["sweep"]) & 255
                    self.tone(i, s["f"], s["d"], s["v"])


A = [(60, 3), (64, 2), (0, 2), (67, 4), (0, 0)]
B = [(48, 5), (55, 1), (200, 3), (0, 0)]


def tick_cycles(emu):
    """cycles from sound_tick's entry to its return (DMA included)"""
    r = emu.run_to_addr("sound_tick", 3)
    if r.get("reason") != "to":
        return None
    c0 = emu.position()["cycles"]
    s = emu.regs()["S"]
    lo, hi = emu.read_mem(0x100 + ((s + 1) & 255), 2)
    emu.run_to_addr((lo | hi << 8) + 1, 3)
    return emu.position()["cycles"] - c0


with Emulator() as emu:
    emu.load(HERE / "build" / "soundtest.xex")
    check("idle", emu.run_until("soundtest: idle", 500))
    idle = tick_cycles(emu)
    check("start", emu.run_until("soundtest: start", 20))
    m = Model()
    m.music(0, A, 8)
    m.music(2, B, 6)
    m.effect(1, 100, 0xA0, 12, 1, 3)
    bad = None
    busy = None
    for t in range(1, 80):
        if t == 5:
            busy = tick_cycles(emu)
        if not emu.run_until(f"t {t}\n", 5):
            bad = (t, "no log line")
            break
        m.tick()
        p = emu.hw()["pokey"]
        if (p["audf"], p["audc"]) != (m.audf, m.audc) and bad is None:
            bad = (t, f"audf={p['audf']} audc={p['audc']}, model audf={m.audf} audc={m.audc}")
        if t == 20:
            m.effect(0, 40, 0x80, 15, 4, -2)
        if t == 60:
            m.stop()
    check("POKEY registers match the C player's model for 79 frames", bad is None, str(bad))
    check("A8:DONE", emu.run_until("A8:DONE", 20))
    print(f"    sound_tick: {idle} cycles idle, {busy} cycles with 2 tracks + 1 effect")
    check("idle sound_tick under 60 cycles", idle is not None and idle < 60, str(idle))
    check("busy sound_tick under 900 cycles", busy is not None and busy < 900, str(busy))

print(f"{'FAIL' if failures else 'PASS'}: soundtest")
sys.exit(1 if failures else 0)
