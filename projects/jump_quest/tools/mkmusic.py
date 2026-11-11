#!/usr/bin/env python3
"""Jump Quest: the original's tune (the ZX Spectrum port's music_data.c,
made there from the original's pattern data) -> src/tune.c, note lists for
the SDK's music_play: the melody and the bass, a row 3 ticks (50 Hz), a
note lasting until the next one. Amiga periods to MIDI: 428 = MIDI 60.
Run: python3 tools/mkmusic.py"""
import math
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
src = (HERE / "tools" / "music_data.c").read_text()


def table(name):
    body = src[src.index(name):]
    body = body[body.index("{") + 1:body.index("};")]
    return [int(x) for x in re.findall(r"\b\d+\b", body)]


period = table("music_period[")
melody, bass, order = table("music_melody["), table("music_bass["), table("music_order[")
SPEED = 3


def midi(n, shift):
    return 0 if n == 0 else round(60 + 12 * math.log2(428 / period[n])) + shift


def track(data, shift):
    rows = [data[p * 64 + r] if p * 64 + r < len(data) else 0 for p in order for r in range(64)]
    out = []
    for n in rows:
        if n or not out:
            out.append([midi(n, shift), SPEED])
        else:
            out[-1][1] += SPEED
    res = []
    for note, ln in out:
        while ln > 255:
            res.append((note, 255))
            ln -= 255
        res.append((note, ln))
    lo = min(n for n, _ in res if n)
    assert lo >= 48 and max(n for n, _ in res) <= 107, (lo, max(n for n, _ in res))
    return res


m = track(melody, 0)
b = track(bass, 12 if min(midi(n, 0) for n in bass if n) < 48 else 0)
lines = ["/* made by tools/mkmusic.py from music_data.c - do not edit */", '#include "a8game.h"', '#include "tune.h"', ""]
for name, t in (("tune_melody", m), ("tune_bass", b)):
    lines.append(f"const a8_note {name}[{len(t) + 1}] = {{")
    for i in range(0, len(t), 10):
        lines.append("    " + ", ".join(f"{{{n},{l}}}" for n, l in t[i:i + 10]) + ",")
    lines.append("    {0, 0}\n};")
(HERE / "src" / "tune.c").write_text("\n".join(lines) + "\n")
(HERE / "src" / "tune.h").write_text("#ifndef TUNE_H\n#define TUNE_H\n#include \"a8game.h\"\n"
                                     f"extern const a8_note tune_melody[{len(m) + 1}], tune_bass[{len(b) + 1}];\n#endif\n")
print(len(m), len(b))
