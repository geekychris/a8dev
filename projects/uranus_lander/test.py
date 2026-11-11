"""Uranus Lander: the autopilot plays (./a8 test uranus_lander).

Boots to the title, starts with FIRE; checks every level's terrain and pads
against a Python copy of the original's generator (its LCG and seeds), and
the difficulty (gravity, fuel). Switches the autopilot on (OPTION), which
lands on levels 1 and 2 (the bonus is the original's: multiplier x 50 + the
fuel left); switches it off on level 3: the ship falls and crashes three
times; GAME OVER, FIRE, the initials BOT go in the high-score table, back to
the title. Also: the logic runs at 50 Hz and the screen is drawn every
frame (the game's perf log), and the screens have content.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "uranus_lander.xex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def original_level(level):
    """The original's generate_terrain(): (checksum, pads)."""
    state = (level * 31337 + 54321) & 0xFFFFFFFF

    def rng():
        nonlocal state
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        return (state >> 16) & 0x7FFF

    def cmod(a, b):                     # C's % (truncating)
        return a - b * int(a / b)
    rough = min(2 + level, 8)
    y, ter = 200, []
    for _ in range(320):
        y += cmod(rng(), rough * 2 + 1) - rough
        if y < 130:
            y = 135
        if y > 238:
            y = 233
        ter.append(y)
    mult_table = [[1, 1, 2, 2], [1, 2, 2, 3], [2, 2, 3, 5], [2, 3, 5, 5]]
    widths = {1: 40, 2: 30, 3: 22, 5: 14}
    diff = min((level - 1) // 2, 3)
    pads = []
    for i in range(4):
        mult = mult_table[diff][i]
        width = widths[mult]
        pad_x = i * 80 + 10 + cmod(rng(), 80 - width - 20)
        pad_x = max(pad_x, 5)
        if pad_x + width >= 315:
            pad_x = 320 - width - 5
        pad_y = min(max(ter[pad_x + width // 2], 140), 233)
        for px in range(pad_x, min(pad_x + width, 320)):
            ter[px] = pad_y
        if pad_x > 2:
            d = ter[pad_x - 1] - pad_y
            if d > 3 or d < -3:
                ter[pad_x - 1] = pad_y + int(d / 2)
        if pad_x + width < 318:
            d = ter[pad_x + width] - pad_y
            if d > 3 or d < -3:
                ter[pad_x + width] = pad_y + int(d / 2)
        pads.append((pad_x, width, pad_y, mult))
    s = run = 0
    for v in reversed(ter):
        run = (run + v) & 0xFFFF
        s = (s + run) & 0xFFFF
    return s, pads


def level_line(log, level):
    m = re.search(rf"uranus: level={level} terrain=(\d+) pads=([\d,;]+) gravity=(\d+) fuel=(\d+)", log)
    if not m:
        return None
    pads = [tuple(int(v) for v in p.split(",")) for p in m.group(2).split(";") if p]
    return int(m.group(1)), pads, int(m.group(3)), int(m.group(4))


def landed(log):
    out = []
    for l in log.splitlines():
        if l.startswith("uranus: landed"):
            d = dict(re.findall(r"(\w+)=(-?\d+)", l))
            d["x"] = re.search(r" x(\d)", l).group(1)
            out.append(d)
    return out


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("uranus: ready", 600))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 4)

    emu.press("FIRE")
    check("FIRE starts a game", emu.run_until("uranus: state=PLAYING", 300))
    check("level 1 drawn", emu.run_until("uranus: level=1", 300))
    emu.run(15)
    log = emu.log(0)
    lv = level_line(log, 1)
    check("level 1: the original's terrain and pads", lv and lv[:2] == original_level(1),
          f"{lv} vs {original_level(1)}")
    check("level 1: gravity 1000, fuel 600", lv and lv[2:] == (1000, 600))
    play = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(play.getcolors(1 << 16) or []) >= 8)

    emu.press("OPTION")
    check("OPTION: autopilot on", emu.run_until("uranus: autopilot=1", 20))
    check("the autopilot lands on level 1", emu.run_until("uranus: landed level=1", 3000))
    check("level 2 drawn", emu.run_until("uranus: level=2", 400))
    emu.run(15)
    log = emu.log(0)
    lv = level_line(log, 2)
    check("level 2: the original's terrain and pads", lv and lv[:2] == original_level(2))
    check("level 2: gravity 1150, fuel 570 (harder)", lv and lv[2:] == (1150, 570))
    check("the autopilot lands on level 2", emu.run_until("uranus: landed level=2", 3000))
    emu.run(10)                         # (the rest of the line)
    log = emu.log(0)
    lands = landed(log)
    pads = {1: original_level(1)[1], 2: original_level(2)[1]}
    ok = len(lands) == 2
    total = 0
    for i, d in enumerate(lands):
        mult = int(d["x"])
        total += int(d["bonus"])
        ok = ok and int(d["bonus"]) == mult * 50 + int(d["fuel"]) and int(d["score"]) == total
    check("landing bonus = multiplier x 50 + fuel, scores add up", ok, str(lands))
    check("level 3 drawn", emu.run_until("uranus: level=3", 400))
    emu.run(15)
    log = emu.log(0)
    lv = level_line(log, 3)
    check("level 3: the original's terrain and pads", lv and lv[:2] == original_level(3))
    emu.press("OPTION")
    check("OPTION again: autopilot off", emu.run_until("uranus: autopilot=0", 20))
    check("the ship falls and the game ends", emu.run_until("uranus: gameover", 4000))
    emu.run(10)
    log = emu.log(0)
    crashes = [l for l in log.splitlines() if l.startswith("uranus: crash")]
    check("three crashes", len(crashes) == 3, str(crashes))
    # the perf windows (100 frames) with no state change in them: flying
    perf, clean = [], True
    for l in log.splitlines():
        if l.startswith("uranus: perf"):
            d = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", l)}
            d["clean"] = clean
            perf.append(d)
            clean = True
        elif l.startswith("uranus: state="):
            clean = False
    fly = [p for p in perf if p["clean"]]
    frames = sum(p["frames"] for p in fly)
    ticks = sum(p["ticks"] for p in fly)
    draws = sum(p["draws"] for p in fly)
    check("flying: the logic at 50 Hz", frames and ticks >= frames - 2, f"{ticks}/{frames}")
    check("flying: drawn every frame (>= 49 fps)", frames and draws * 50 >= 49 * frames, f"{draws}/{frames}")
    if frames:
        print(f"    flying: {draws} draws in {frames} frames: {50 * draws / frames:.1f} fps")
    frames = sum(p["frames"] for p in perf)
    ticks = sum(p["ticks"] for p in perf)
    draws = sum(p["draws"] for p in perf)
    check("the whole game: logic >= 45 Hz (levels are made while it waits)",
          frames and ticks * 50 >= 45 * frames, f"{ticks}/{frames}")
    if frames:
        print(f"    whole game: {50 * draws / frames:.1f} fps, logic {50 * ticks / frames:.1f} Hz")
    emu.run(70)
    emu.press("FIRE")
    check("a high score asks for initials", emu.run_until("uranus: initials rank=", 200))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("uranus: state=TITLE", 200))
    if failures:
        print("\n".join(emu.log(0).splitlines()[-30:]))

print(f"{'FAIL' if failures else 'PASS'}: uranus_lander")
sys.exit(1 if failures else 0)
