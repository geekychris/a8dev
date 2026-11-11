"""Spectral Keep: the GIF and the pictures (screenshots/).

`./a8 video` records without input, so this drives the game through the
Emulator API with the test's bot (tools/bot.py): the title, then the bot
starts The Gatehouse, walks into the Great Hall and round to the relic.
Run from the repo root: python3 projects/spectral_keep/tools/make_gif.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import Bot, decode, PROBE_SIZE, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A  # noqa: E402

OUT = HERE / "screenshots"
OUT.mkdir(exist_ok=True)
KEYS = [(B_UP, "UP"), (B_DOWN, "DOWN"), (B_LEFT, "LEFT"), (B_RIGHT, "RIGHT")]
frames = []
with Emulator() as emu:
    emu.load(HERE / "build" / "spectral_keep.xex")
    assert emu.run_until("sk: ready", 1500)
    emu.run(60)
    emu.screenshot(OUT / "title.png")
    for i in range(25):
        emu.run(2)
        frames.append(emu.screenshot())
    bot, tick, held_now, shot = Bot(), None, set(), set()
    for f in range(6000):
        p = decode(emu.read_mem("keep_probe", PROBE_SIZE, space="ram"))
        if p["tick"] != tick:
            tick = p["tick"]
            held, pressed = bot.step(p)
            want = {k for b, k in KEYS if held & b}
            if held & B_A or pressed & B_A:
                want.add("FIRE")
            if want != held_now:
                emu.release()
                if want:
                    emu.hold("+".join(sorted(want)))
                held_now = want
        emu.run(1)
        if p["state"] == 1 and f % 2 == 0:
            frames.append(emu.screenshot())
        if p["state"] == 1 and p["room"] == 0 and "gate" not in shot and p["pz"] > 3.0:
            emu.screenshot(OUT / "play.png")
            shot.add("gate")
        if p["state"] == 1 and p["room"] == 3 and "hall" not in shot and p["pz"] > 4.0:
            emu.screenshot(OUT / "hall.png")
            shot.add("hall")
        if p["relics"] > 0:
            for i in range(40):
                emu.run(2)
                frames.append(emu.screenshot())
            break
    emu.release()
frames[0].save(OUT / "spectral_keep.gif", save_all=True, append_images=frames[1:], loop=0,
               duration=40, optimize=True)
print(f"{len(frames)} frames", shot)
