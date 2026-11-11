"""a8_3d: the a3d renderer's checks, cycle budgets and demo.

The program checks its maths and pixels itself (check: ... ok=1), times
each part (bench: ... cycles=N) and then runs the cube demo. Here we check
every check passed, every timing is inside its budget (so a slow-down
shows), the demo's frame rate, the joystick and FIRE, that the picture
has the cubes in it, and that it runs on a 64K 800XL too.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "a8_3d.xex"
failures = []

# cycle budgets (wall time, DMA included), about 15% above what they measure
BUDGET = {
    "clear96": 30000, "clear40rows": 15500, "fill40x30": 15000,
    "poly_tri60x40": 20000, "poly_clipped": 82000, "line140": 19000,
    "line_steep88": 19500, "xform8": 19000, "mat_ypr": 12500,
    "mat_mul": 6500, "cube_faces": 49000, "prism_box": 39000, "prism_nofill": 11700,
}
CHECKS = ["mul8s", "mulf", "muldiv", "fill_rect", "backface", "clip", "clip_skip", "clear_dirty", "near", "prism_draw", "prism_near", "prism_turned", "prism_top", "prism_pyr_wall"]


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("bench runs", emu.run_until("bench: done", 3000))
    log = emu.log(0)
    for c in CHECKS:
        check(f"check {c}", f"check: {c} ok=1" in log)
    check("xform + projection exact", "check: xform v=40,-40,256 s=92,56 clip=0" in log,
          re.search(r"check: xform.*", log).group(0) if "check: xform" in log else "")
    for name, limit in BUDGET.items():
        m = re.search(rf"bench: {name} cycles=(\d+)", log)
        cyc = int(m.group(1)) if m else -1
        print(f"       {name:14s} {cyc:6d} cycles (budget {limit})")
        check(f"bench {name}", 0 < cyc <= limit, f"{cyc} > {limit}")

    check("demo ready", emu.run_until("a8_3d: ready", 300))
    check("fps reported", emu.run_until("a8_3d: fps10=", 1000))
    emu.run(2)
    fps = [int(v) for v in re.findall(r"fps10=(\d+)", emu.log(0))]
    print(f"       demo fps x10: {fps}")
    check("demo >= 6.5 fps (3 shaded cubes)", fps and min(fps) >= 65, str(fps))

    img = emu.screenshot(HERE / "build" / "test.png")
    check("cubes drawn (5+ colours)", len(img.getcolors(1 << 16) or []) >= 5)
    h0 = emu.screen_hash()
    emu.hold("RIGHT")
    emu.run(60)
    emu.release()
    emu.run(30)
    check("joystick turns the camera", emu.screen_hash() != h0)
    emu.log()
    emu.press("FIRE", hold_frames=15)
    check("FIRE: wireframe", emu.run_until("a8_3d: wire=1", 60))
    emu.run(40)
    emu.screenshot(HERE / "build" / "test_wire.png")
    if failures:
        print("\n".join(emu.debug_log[-15:]))

with Emulator(machine="800xl") as emu:              # 64K: no banks needed
    emu.load(PROG)
    check("800XL: runs the demo", emu.run_until("a8_3d: ready", 3000))
    check("800XL: checks pass", "ok=0" not in emu.log(0))

print(f"{'FAIL' if failures else 'PASS'}: a8_3d")
sys.exit(1 if failures else 0)
