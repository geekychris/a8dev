"""Tank Duel: a scripted game (./a8 test tank_duel).

Boots to the title (the autopilot fighting behind it, the radar, the tune
of the effects); UP chooses round 2 and back; FIRE starts round 1. The
joystick drives (UP: the tank moves ahead, LEFT turns it) and FIRE shoots.
SELECT hands over to the autopilot: kills, hits taken, round 1 cleared,
round 2 begins. Then the armour is set to 1 and the score to at least 1000
(memory) so the next hit ends the game with a score for the table: game over, the initials ACE in the high scores, back to the
title. Checks the picture has content and the frame rate. Then the same
XEX on a 64K 800XL (the rules' code under the OS ROM: no 130XE needed).
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "tank_duel.xex"
failures = []
TK0 = 12                       # G.tk[0] in TDGame (td.h): type, alive, hp, x, z, v, yaw
SCORE = 481                    # G.score (int32)


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def tank0(emu):
    b = emu.read_mem("G", TK0 + 11, space="ram")
    hp = struct.unpack_from("<b", b, TK0 + 2)[0]
    x, z, v, yaw = struct.unpack_from("<hhhH", b, TK0 + 3)
    return hp, x, z, yaw


def play(machine):
    with Emulator(machine=machine) as emu:
        emu.load(PROG)
        ok = emu.run_until("td: ready", 1500)
        check(f"{machine}: boots to the title", ok)
        if machine != "130xe":
            emu.run(400)
            check(f"{machine}: the demo fights", "td: fps10=" in emu.log_text(0))
            return
        emu.run(200)
        img = emu.screenshot(HERE / "build" / "test_title.png")
        check("the title shows the arena", len(img.getcolors(1 << 16) or []) >= 5)
        emu.press("UP", hold_frames=4, release_frames=60)
        text = emu.read_mem(0xAF80, 40, space="ram")
        check("UP: start at round 2", text[33] == 0x12, text[30:36].hex())
        emu.press("DOWN", hold_frames=4, release_frames=60)
        emu.press("FIRE", hold_frames=4)
        check("FIRE starts round 1", emu.run_until("td: start round=1", 200))
        emu.run(50)
        hp, x0, z0, yaw0 = tank0(emu)
        emu.hold("UP")
        emu.run(150)
        emu.release()
        hp, x1, z1, yaw1 = tank0(emu)
        check("UP drives ahead", z1 > z0 + 16 * 20, f"z {z0} -> {z1}")
        emu.hold("LEFT")
        emu.run(60)
        emu.release()
        hp, x2, z2, yaw2 = tank0(emu)
        check("LEFT turns", yaw2 != yaw1, f"yaw {yaw1} -> {yaw2}")
        emu.press("FIRE", hold_frames=6)
        emu.run(30)
        img = emu.screenshot(HERE / "build" / "test_play.png")
        check("the arena has content", len(img.getcolors(1 << 16) or []) >= 6)
        emu.press("SELECT", hold_frames=4)                 # the autopilot
        ok = emu.run_until("td: clear round=1", 12000)
        check("the autopilot clears round 1", ok)
        log = emu.log_text(0)
        check("kills (100 a scout)", "td: kill SCOUT score=100" in log)
        check("round 2 begins", emu.run_until("td: round 2", 800))
        emu.press("SELECT", hold_frames=4)                 # (back to the stick: stand still)
        emu.run(20)
        # the armour to 1: the next hit ends it; the score to at least 1000,
        # so it makes the table (its lowest is 800) however the fight went
        g = emu.addr("G")
        score = struct.unpack("<i", emu.read_mem(g + SCORE, 4, space="ram"))[0]
        check("the score is read back", 0 < score < 100000, str(score))
        emu.write_mem(g + SCORE, struct.pack("<i", max(score, 1000)), space="ram")
        emu.write_mem(g + TK0 + 2, bytes([1]), space="ram")
        ok = emu.run_until("td: dead", 6000)
        check("a hit with 1 armour left: game over", ok)
        log = emu.log_text(0)
        check("hits taken on the way (td: hurt)", "td: hurt hp=" in log)
        if emu.run_until("td: initials rank=", 400):
            emu.run(20)
            emu.type("ACE")
            check("ACE goes in the high scores", emu.run_until("name=ACE", 300))
        else:
            check("a score for the table", False, "no initials")
        check("back to the title", emu.run_until("td: title", 400))
        fps = [int(m) for m in re.findall(r"td: fps10=(\d+)", emu.log_text(0))]
        if fps:
            print(f"    frame rate: {min(fps) / 10:.1f}-{max(fps) / 10:.1f} fps, {sum(fps) / len(fps) / 10:.1f} on average")
        check("frame rate logged", bool(fps))
        if failures:
            print("\n".join(emu.log(0).splitlines()[-25:]))


play("130xe")
play("800xl")
print(f"{'FAIL' if failures else 'PASS'}: tank_duel")
sys.exit(1 if failures else 0)
