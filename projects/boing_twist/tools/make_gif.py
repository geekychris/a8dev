#!/usr/bin/env python3
"""The README's pictures and GIF: the title's bounce, the select screen, an
Easy game scrambled, twisted and flipped, a peek.

    .venv/bin/python projects/boing_twist/tools/make_gif.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
PROG = HERE / "build" / "boing_twist.xex"
SHOTS = HERE / "screenshots"
frames = []


def grab(emu, n, every=3):
    for _ in range(n // every):
        emu.run(every)
        frames.append(emu.screenshot(None).convert("RGB"))


def press(emu, k, n=4, rec=True):
    emu.hold(k)
    grab(emu, n, 2) if rec else emu.run(n)
    emu.release()


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="130xe") as emu:
    emu.load(PROG)
    emu.run_until("bt: ready", 1500)
    grab(emu, 150)
    emu.screenshot(SHOTS / "title.png", scale=2)
    press(emu, "FIRE", 6)
    emu.run_until("bt: screen=SELECT", 400)
    grab(emu, 45)
    press(emu, "RIGHT")
    emu.run_until("bt: preview", 300)
    press(emu, "DOWN")
    grab(emu, 12)
    press(emu, "RIGHT")
    emu.run_until("bt: preview", 300)
    grab(emu, 45)
    emu.screenshot(SHOTS / "select.png", scale=2)
    press(emu, "UP")
    grab(emu, 6)
    press(emu, "LEFT")                       # back to Easy
    emu.run_until("bt: preview", 300)
    press(emu, "FIRE", 6)
    emu.run_until("bt: screen=PLAY", 400)
    grab(emu, 150)                           # the scramble
    emu.run_until("bt: scrambled", 3000)
    grab(emu, 20)
    for k in ("RIGHT", "DOWN", "LEFT", "X", "FIRE", "UP", "RIGHT"):
        press(emu, k)
        grab(emu, 30)
    emu.screenshot(SHOTS / "play.png", scale=2)
    emu.hold("OPTION")
    grab(emu, 30)
    emu.screenshot(SHOTS / "peek.png", scale=2)
    emu.release()
    grab(emu, 20)

out = SHOTS / "boing_twist.gif"
out.parent.mkdir(parents=True, exist_ok=True)
frames[0].save(out, save_all=True, append_images=frames[1:], duration=60, loop=0, optimize=True)
print(out, len(frames), "frames")
