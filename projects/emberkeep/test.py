"""Emberkeep: a bot plays (./a8 test emberkeep).

Boots to the title, FIRE begins in the first chamber. The lantern: the
screen's lit lines are the chamber's image (in a 130XE bank) inside the
light and black outside it. The bot (tools/bot.py) walks the shortest way
to the runes (scoring), an oil flask when the lantern is low, the open
portal: chambers 1 and 2; in chamber 3 it walks into the spikes until its
lives are gone (a life lost, the chamber restarted, GAME OVER). Its score
goes in the high scores (BOT), back to the title. Then the same on a 64K
800XL (the image in main memory) for chamber 1. Checks the frame rate and
the rules at 50 Hz.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import State, Bot, step, GM_PLAYING, GM_LOSE, GM_WIN  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "emberkeep.xex"
FIELD = 0xA010
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def lamp_ok(emu, banked):
    """the lit square's middle lines: the screen's bytes inside it are the
    image's, the lines above and below it black"""
    lr = emu.read_mem("lr", 7, space="ram")
    x0, x1, y0, y1, ch, cv = lr[0], lr[1], lr[2], lr[3], lr[4], lr[5]
    img = struct.unpack("<H", emu.read_mem("lp_img", 2, space="ram"))[0]
    bank = emu.read_mem("lp_bank", 1, space="ram")[0]
    bad = 0
    for y in range(y0 + cv, y1 - cv):
        scr = emu.read_mem(FIELD + y * 40, 40, space="ram")
        im = (emu.read_mem(img - 0x4000 + y * 40, 40, bank=bank) if bank != 0xFF
              else emu.read_mem(img + y * 40, 40, space="ram"))
        for b in range((x0 + 3) // 4, x1 // 4):          # the bytes wholly lit
            bad += scr[b] != im[b]
        for b in list(range(0, x0 // 4)) + list(range((x1 + 3) // 4, 40)):
            bad += scr[b] != 0
    dark = [y for y in (y0 - 3, y1 + 3) if 0 <= y < 180]
    for y in dark:
        bad += sum(1 for v in emu.read_mem(FIELD + y * 40, 40, space="ram") if v)
    return bad == 0 and (bank != 0xFF) == banked, f"{bad} bytes off, bank {bank}"


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("ember: ready", 900))
    emu.run(40)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("title has content", len(title.getcolors(1 << 16) or []) >= 3)
    emu.press("FIRE")
    check("FIRE begins: chamber 1", emu.run_until("ember: chamber 1", 300))
    emu.run(30)
    ok, why = lamp_ok(emu, banked=True)
    check("the lantern: the image (in a 130XE bank) lit, the rest black", ok, why)
    shot = emu.screenshot(HERE / "build" / "test_play.png")
    check("game screen has content", len(shot.getcolors(1 << 16) or []) >= 4)

    st = State(emu)
    bot, held = Bot(die_in=2), [""]
    lamp_moving = None
    for f in range(20000):
        s = step(emu, st, bot, held)
        if f == 300:
            lamp_moving = lamp_ok(emu, banked=True)
        if s["mode"] in (GM_LOSE, GM_WIN):
            break
    emu.release()
    emu.run(5)
    log = emu.log(0)
    runes = [int(m) for m in re.findall(r"ember: rune score=(\d+)", log)]
    check("the lantern while walking", lamp_moving and lamp_moving[0], lamp_moving and lamp_moving[1])
    check("runes: 100 each", runes[:1] == [100] and len(runes) >= 4, str(runes[:5]))
    check("oil taken (25, the lantern refilled)", "ember: oil score=" in log)
    check("through the portal: chamber 2", "ember: portal score=" in log and "ember: chamber 2" in log)
    check("chamber 3", "ember: chamber 3" in log)
    check("lives lost (the spikes)", "ember: died lives=2" in log and "ember: died lives=0" in log)
    check("GAME OVER", "ember: gameover" in log)
    perf = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in log.splitlines()
            if l.startswith("ember: perf") and l.count("=") == 5]
    perf = [q for q in perf if int(q["frames"]) <= 101]                 # (not a chamber's drawing)
    frames = sum(int(q["frames"]) for q in perf)
    draws = sum(int(q["draws"]) for q in perf)
    ticks = sum(int(q["ticks"]) for q in perf)
    if frames:
        view = sum(int(q["view"]) for q in perf) // len(perf)
        rules = sum(int(q["rules"]) for q in perf) // len(perf)
        print(f"    {draws} draws, {ticks} steps in {frames} frames: {50 * draws / frames:.1f} fps, "
              f"rules {50 * ticks / frames:.1f} Hz; a picture {view} cycles, a step {rules}")
    check("rules at 50 Hz (a step every frame)", frames and ticks * 100 >= 98 * frames, f"{ticks}/{frames}")
    check(">= 30 fps on average", frames and draws * 50 >= 30 * frames, f"{draws}/{frames}")
    emu.run(10)
    over = emu.screenshot(HERE / "build" / "test_over.png")
    check("end screen has content", len(over.getcolors(1 << 16) or []) >= 2)
    check("a high score asks for initials", emu.run_until("ember: initials rank=", 400))
    emu.run(10)
    emu.type("BOT")
    check("BOT goes in the table", emu.run_until("name=BOT", 200))
    check("back to the title", emu.run_until("ember: title", 300))
    if failures:
        print("\n".join(emu.log(0).splitlines()[-25:]))

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    emu.run_until("ember: ready", 900)
    emu.run(40)
    emu.press("FIRE")
    emu.run_until("ember: chamber 1", 300)
    st = State(emu)
    bot, held = Bot(), [""]
    for f in range(600):
        s = step(emu, st, bot, held)
        if "ember: rune" in emu.log_text(0):
            break
    emu.release()
    emu.run(3)
    ok, why = lamp_ok(emu, banked=False)
    check("64K (800XL): the image in main memory, the lantern the same", ok, why)
    check("64K: a rune", "ember: rune score=100" in emu.log_text(0))

print(f"{'FAIL' if failures else 'PASS'}: emberkeep")
sys.exit(1 if failures else 0)
