"""Spectral Keep: a bot plays (./a8 test spectral_keep).

Boots to the title (the gate room drawn, the title tune on POKEY), and the
gate room's bare picture (bank 3) is checked against the host's own
composition of it (tools/gen_gfx.py). Then the Spectrum port's bot
(tools/bot.py) plays The Gatehouse with the joystick, reading keep_probe at
each logic step: through the gate room's north door into the Great Hall,
round its west side to the relic, then it stands still while the ghost
takes every life: game over, back to the title. Checks the rooms, the
relic, the deaths, the explorer and the ghost as players, the logic's rate.
Then the other keeps: the Crypt (its stairs, north into the catacomb) and
the Tower (its foyer, north into the stairwell), each chosen on the title
and walked with the joystick, their rooms drawn. Then a 64K 800XL: the
"needs a 130XE" screen.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from a8dev.emu import Emulator  # noqa: E402
from bot import Bot, decode, keys_towards, PROBE_SIZE, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A  # noqa: E402
import math  # noqa: E402

PROG = HERE / "build" / "spectral_keep.xex"
failures = []
KEYS = [(B_UP, "UP"), (B_DOWN, "DOWN"), (B_LEFT, "LEFT"), (B_RIGHT, "RIGHT")]


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def bare_room_ok(emu):
    """bank 3's copy of the gate room against the host's composition"""
    try:
        import gen_gfx
    except ImportError as e:
        return None, f"no numpy? {e}"
    host = HERE / "build" / "host"
    if not (host / "room_0_0.bin").exists():          # the original's renderer, on the host
        import subprocess
        host.mkdir(parents=True, exist_ok=True)
        o = HERE / "tools" / "orig"
        r = subprocess.run(["cc", "-O2", f"-I{o}", "-o", str(host / "export"), str(HERE / "tools" / "export.c")] +
                           [str(o / f) for f in ("vox.c", "textures.c", "levels.c", "room.c", "world.c", "scene.c")] +
                           ["-DSCENE_RGB15", "-lm"], capture_output=True)
        if r.returncode or subprocess.run([str(host / "export"), str(host)], cwd=HERE, capture_output=True).returncode:
            return None, "couldn't build tools/export.c"
    if not (host / "room_0_0.bin").exists():
        return None, "build/host not made (tools/export.c)"
    table, rooms = gen_gfx.main()
    lv = next(r for r in rooms if (r["l"], r["r"]) == (0, 0))["lv"]
    bad = 0
    for y in range(208):
        row = emu.read_mem(y * 40, 40, bank=3)
        for x in range(160):
            v = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3
            bad += v != lv[y, x]
    return bad == 0, f"{bad} pixels differ"


with Emulator() as emu:
    emu.load(PROG)
    check("boots to the title", emu.run_until("sk: ready", 1500))
    log = emu.log_text(0)
    check("the four banks are there", "sk: banks ok=1" in log)
    emu.run(60)
    title = emu.screenshot(HERE / "build" / "test_title.png")
    check("the title shows the gate room", len(title.getcolors(1 << 16) or []) >= 6)
    hw = emu.hw()["pokey"]
    check("the title tune (POKEY channels 1, 2)", hw["audc"][1] & 15 and hw["audc"][2] & 15, str(hw["audc"]))
    ok, why = bare_room_ok(emu)
    if ok is None:
        print(f"  (skipped the picture check: {why})")
    else:
        check("the gate room's picture: as the host composes it", ok, why)

    bot = Bot()
    tick = None
    held_now = set()
    pm_seen = ghost_seen = False
    for f in range(30000):
        raw = emu.read_mem("keep_probe", PROBE_SIZE, space="ram")
        p = decode(raw)
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
            if bot.phase == "done":
                break
            if p["state"] == 1 and p["room"] == 3 and not ghost_seen:
                pm = emu.read_mem(0xB400, 1024, space="ram")
                pm_seen = any(pm[0:256]) and any(pm[512:768])     # the explorer, the ghost
                ghost_seen = pm_seen
        emu.run(1)
    emu.release()
    emu.run(10)
    log = emu.log_text(0)
    rooms = re.findall(r"sk: room (\w+) visited", log)
    check("FIRE starts The Gatehouse", "level THE GATEHOUSE" in log and "state=1" in log)
    check("north into the Great Hall", rooms[:2] == ["gate", "hall"], str(rooms))
    check("the relic taken", "pickup R hall" in log and "relics=1" in log)
    deaths = re.findall(r"death A GHOST room=hall lives=(\d)", log)
    check("the ghost takes every life", deaths == ["3", "2", "1", "0"], str(deaths))
    check("game over, then the title", "state=4" in log and log.rfind("state=0") > log.find("state=4"))
    check("the explorer and the ghost are players in the hall", pm_seen)
    over = emu.screenshot(HERE / "build" / "test_play.png")
    perf = [tuple(map(int, m)) for m in re.findall(r"sk: perf steps=50 frames=(\d+) view=(\d+) room=(?:hall)", log)]
    if perf:
        frames = sum(a for a, _ in perf) / len(perf)
        print(f"    the Great Hall: {50 * 50 / frames:.1f} logic steps a second "
              f"({frames / 50:.2f} frames a step, the picture {sum(b for _, b in perf) / len(perf) / 50:.2f})")
        check("the logic keeps up: >= 9 steps a second in the hall", 50 * 50 / frames >= 9, f"{frames / 50:.2f}")
    else:
        check("perf lines in the hall", False)
    if failures:
        print("\n".join(log.splitlines()[-30:]))

def walk_keep(keep, name, start, route, target):
    """choose a keep on the title, walk a route from its first room into the next"""
    with Emulator() as emu:
        emu.load(PROG)
        emu.run_until("sk: ready", 1500)
        emu.run(30)
        for _ in range(keep):
            emu.press("DOWN", hold_frames=6, release_frames=10)
        emu.run_until(f"sk: drew room={start}", 600)
        emu.run(20)
        emu.press("FIRE", hold_frames=6, release_frames=4)
        ok = emu.run_until(f"sk: level {name}", 300)
        check(f"{name}: chosen on the title and started", ok)
        tick, keys, wp, held_now = None, 0, 0, set()
        for f in range(4000):
            p = decode(emu.read_mem("keep_probe", PROBE_SIZE, space="ram"))
            if p["tick"] != tick and p["state"] == 1 and wp < len(route):
                tick = p["tick"]
                tx, tz = route[wp]
                dx, dz = tx - p["px"], tz - p["pz"]
                if math.hypot(dx, dz) < 0.15 and wp < len(route) - 1:
                    wp += 1
                    tx, tz = route[wp]
                    dx, dz = tx - p["px"], tz - p["pz"]
                keys = keys_towards(dx, dz, keys)
                want = {k for b, k in KEYS if keys & b}
                if want != held_now:
                    emu.release()
                    if want:
                        emu.hold("+".join(sorted(want)))
                    held_now = want
            emu.run(1)
            if f"sk: room {target} visited" in emu.log_text(0):
                break
        emu.release()
        log = emu.log_text(0)
        check(f"{name}: walked from the {start} into the {target}", f"sk: room {target} visited" in log)
        emu.run_until(f"sk: drew room={target}", 300)
        emu.run(40)
        img = emu.screenshot(HERE / "build" / f"test_{target}.png")
        check(f"{name}: the {target} drawn", len(img.getcolors(1 << 16) or []) >= 6)


walk_keep(1, "THE CRYPT", "stairs", [(4.0, 3.0), (4.0, 9.0)], "catacomb")
walk_keep(2, "THE TOWER", "foyer", [(3.5, 4.0), (4.0, 9.0)], "stairwell")

with Emulator(machine="800xl") as emu:
    emu.load(PROG)
    check("64K (800XL): says it needs a 130XE", emu.run_until("sk: no 130xe", 900))

print(f"{'FAIL' if failures else 'PASS'}: spectral_keep")
sys.exit(1 if failures else 0)
