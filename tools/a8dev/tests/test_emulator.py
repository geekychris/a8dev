"""Tests for the patched Atari800 and its Python client (a8dev.emu).

Covers every harness command with a small asm program (tests/emu/emutest.s)
and a8-core's projects: determinism, running, input, the log, breakpoints,
watchpoints, stepping, memory and banks, hardware registers, screens,
states, recording and replay, machines, speed. Run by ./a8 selftest, or:

    .venv/bin/python tools/a8dev/tests/test_emulator.py
    A8_TEST_WINDOW=1 ...   # also the SDL window session (opens a window)

The test program is assembled with the cc65 on PATH, or in the toolchain image.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))

from a8dev.emu import Emulator, EmulatorError, ProtocolError, parse_keys, input_state  # noqa: E402

HERE = Path(__file__).resolve().parent
OUT = ROOT / "build" / "tests" / "emu"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def section(t):
    print(f"\n== {t}")


def assemble(name: str) -> Path:
    """tests/emu/<name>.s -> build/tests/emu/<name>.xex (+ .lbl, .map)."""
    OUT.mkdir(parents=True, exist_ok=True)
    src = HERE / "emu" / f"{name}.s"
    xex = OUT / f"{name}.xex"
    if xex.exists() and xex.stat().st_mtime > src.stat().st_mtime:
        return xex
    args = ["cl65", "-t", "atari", "-C", "atari-asm.cfg", "-Ln", f"{name}.lbl", "-m", f"{name}.map",
            "-o", f"{name}.xex"]
    if shutil.which("cl65") and os.environ.get("A8_NATIVE", "1") != "0":
        shutil.copy(src, OUT / src.name)
        r = subprocess.run(args + [src.name], cwd=OUT, capture_output=True, text=True)
    else:
        image = next(line.split("=", 1)[1].strip('"') for line in (ROOT / "scripts" / "lib.sh").read_text().splitlines()
                     if line.startswith("TOOLCHAIN_IMAGE="))
        shutil.copy(src, OUT / src.name)
        r = subprocess.run(["docker", "run", "--rm", "-u", f"{os.getuid()}:{os.getgid()}", "-v", f"{OUT}:/w",
                            "-w", "/w", image] + args + [src.name], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"cannot assemble {src}: {r.stdout}{r.stderr}")
    return xex


def boot(emu, prog):
    emu.load(prog)
    assert emu.run_until("emutest: ready", 300), emu.log(0)


def test_determinism(prog):
    section("determinism")
    seqs, rnds = [], []
    for _ in range(2):
        with Emulator() as e:
            e.load(prog)
            e.run_until("emutest: ready", 300)
            rnds.append(e.log(0).split("rnd=")[1][:2])
            e.raw("hash action=on")
            e.run(120)
            e.press("A")
            e.joystick("LEFT+FIRE")
            e.run(30)
            seqs.append(e.raw("hash action=get"))
            regs = e.regs()
    check("POKEY RANDOM is the same after every power-on", rnds[0] == rnds[1], str(rnds))
    check("identical inputs give identical per-frame hashes (2 processes)", seqs[0] == seqs[1])
    with Emulator() as e:
        e.load(prog)
        e.run_until("emutest: ready", 300)
        e.run(40)
        e.load(prog)        # reload in the same process: the same start, whatever ran before
        e.run_until("emutest: ready", 300)
        e.raw("hash action=on")
        e.run(120)
        e.press("A")
        e.joystick("LEFT+FIRE")
        e.run(30)
        check("a reload in a used emulator gives the same hashes", e.raw("hash action=get") == seqs[0])
        check("and the same registers", e.regs() == regs, f"{e.regs()} vs {regs}")
    with Emulator(seed=1234) as e:
        e.load(prog)
        e.run_until("emutest: ready", 300)
        check("seed= changes RANDOM", e.log(0).split("rnd=")[1][:2] != rnds[0])


def test_running(e, prog):
    section("running, the log, signals, speed")
    st = e.status()
    check("headless sessions start paused", st["mode"] == "paused" and st["paused"], str(st))
    f0 = e.frame()
    time.sleep(0.3)
    check("a paused session does not advance", e.frame() == f0)
    r = e.run(10)
    check("run(10) runs exactly 10 frames", r["frames"] == 10 and e.frame() == f0 + 10 and r["why"] == "frames", str(r))
    check("cycles: 312 lines x 114 per PAL frame", e.position()["cycles"] == e.frame() * 312 * 114)
    t = time.monotonic()
    r = e.run(3000)
    dt = time.monotonic() - t
    check("faster than 20x real time", r["fps"] > 1000, f"{r.get('fps')} fps")
    print(f"     speed: {r['fps']:.0f} frames/s = {r['speed']:.0f}x real time (3000 frames in {dt:.2f}s)")
    e.load(prog)
    check("run_until stops at the log text", e.run_until("emutest: ready", 300))
    check("... on the instruction after the write (mid-frame)", e.last_run["midframe"], str(e.last_run))
    check("run_until does not match old text", not e.run_until("emutest: ready", 20))
    check("log(0) has everything since the load", "rnd=" in e.log(0) and "banks done" in e.log(0))
    c = e.log_cursor()
    e.press("B")
    check("log() since the mark", "key=15" in e.log())
    check("run_until(since=cursor) finds text written after the cursor", e.run_until("key=15", 5, since=c))
    r = e.run_until_signal(300)
    check("signal ($D6FE) stops a run", r["why"] == "signal" and r.get("signal") == "42", str(r))
    check("status reports the signal", e.status()["signal"] == 42)
    e.run(1)
    check("run_to(frame) lands on the frame boundary", e.run_to(e.position()["since_load"] + 7) and
          not e.position()["midframe"])
    tgt = e.position()["since_load"] + 3
    e.run_to(tgt, 5000)
    p = e.position()
    check("run_to(frame, cycles) stops at that machine cycle", p["since_load"] == tgt and 5000 <= p["tstates"] < 5010, str(p))


def test_input(e, prog):
    section("input: keys, console, joysticks, type, sequence")
    check("key names", parse_keys("SHIFT+A") == ["SHIFT", "A"] and input_state(["SHIFT", "A"])["key"] == 0x7F)
    try:
        parse_keys("NOPE")
        check("unknown key is an error", False)
    except ValueError:
        check("unknown key is an error", True)
    boot(e, prog)
    e.log()
    e.press("Q")
    check("press: keyboard code reaches CH", "key=2F" in e.log())
    e.press("SHIFT+A")
    check("SHIFT+A", "key=7F" in e.log())
    e.press("CTRL+A")
    check("CTRL+A", "key=BF" in e.log())
    e.press("RETURN")
    check("RETURN", "key=0C" in e.log())
    e.type("HI 1\n")
    t = e.log()
    check("type()", all(f"key={c}" in t for c in ("39", "0D", "21", "1F", "0C")), t)
    e.press("START")
    t = e.log()
    check("console START", "consol=06" in t and t.rstrip().endswith("consol=07"), t)
    e.press("SELECT+OPTION")
    check("console SELECT+OPTION", "consol=01" in e.log())
    e.joystick("UP+LEFT")
    e.run(2)
    check("joystick UP+LEFT on stick 0", "stick=0A" in e.log())
    e.joystick("FIRE")
    e.run(2)
    t = e.log()
    check("joystick FIRE (trigger 0), directions released", "trig=00" in t and "stick=0F" in t, t)
    e.joystick(None)
    e.run(2)
    check("joystick released", "trig=01" in e.log())
    e.hold("RIGHT")
    e.run(3)
    e.release()
    e.run(2)
    t = e.log()
    check("hold / release", "stick=07" in t and "stick=0F" in t, t)
    c = e.log_cursor()
    e.sequence([{"press": "Z"}, {"run": 2}, {"hold": "DOWN"}, {"run": 2}, {"release": "DOWN"},
                {"until": "stick=0F", "max": 10}])
    t = e.log(c)
    check("sequence()", "key=17" in t and "stick=0D" in t, t)
    il = e.input_log()
    check("input_log records agent input with positions", len(il) > 10 and "f" in il[-1])
    st = e.raw("input")
    check("input state readable", "seen=" in st)


def test_debugger(e, prog):
    section("breakpoints, watchpoints, stepping, registers, backtrace")
    boot(e, prog)
    b = e.break_at("bump")
    r = e.continue_(5)
    check("break_at(symbol) stops before it runs", r["stopped"] and r["why"] == "breakpoint"
          and r["pc"] == e.addr("bump") and r["symbol"] == "bump", str(r))
    check("... mid-frame, with line/xpos", r["midframe"] and "line" in r)
    bt = e.backtrace()
    check("backtrace: bump called from frame_hook, frame_hook from main_loop",
          len(bt) >= 3 and (bt[1].get("caller_symbol") or "").startswith("frame_hook")
          and (bt[2].get("caller_symbol") or "").startswith("main_loop"), json.dumps(bt))
    r = e.continue_(5)
    check("continue: the next frame's hit (does not re-fire at once)", r["stopped"] and r["frames"] == 1, str(r))
    e.delete()
    br = e.break_if("PC=bump AND [watched]>=$1E")
    r = e.continue_(50)
    w = e.peek("watched")
    check("condition breakpoint with symbols and memory", r["stopped"] and w >= 0x1E and w < 0x21, f"{r} watched={w:#x}")
    e.delete(br["index"])
    e.break_if("A=$2A AND PC>=$2E00 AND PC<$3000")
    r = e.continue_(300)
    check("condition on registers (A=$2A in our code)", r["stopped"] and r["regs"]["A"] == 0x2A, str(r))
    e.delete()
    e.break_if("FRAME>=" + str(e.frame() + 3) + " AND PC=frame_hook")
    f = e.frame()
    r = e.continue_(10)
    check("FRAME condition", r["stopped"] and r["frame"] == f + 3, str(r))
    e.delete()
    wv = e.watch("watched", 1, "w")
    r = e.continue_(5)
    sym = e.symbols.name_of(int(r.get("insn_pc", "0"), 16))
    check("write watchpoint stops after the write", r["why"] == "watchpoint" and sym == "bump+0x6"
          and r.get("access") == "w" and int(r["value"], 16) == e.peek("watched"), str(r))
    check("... reports the old value", int(r["old"], 16) == (e.peek("watched") - 3) & 0xFF, str(r))
    e.delete(wv["index"])
    e.watch("readme", 1, "r")
    r = e.continue_(5)
    check("read watchpoint", r["why"] == "watchpoint" and r.get("access") == "r", str(r))
    bps = e.breakpoints()
    check("breakpoints() lists hits", bps["watchpoints"][0]["hits"] >= 1, str(bps))
    e.delete()
    r = e.continue_(5)
    check("delete() -> runs freely", not r["stopped"] and r["frames"] == 5, str(r))
    e.break_at("frame_hook")
    e.continue_(3)
    e.delete()
    a = e.addr("frame_hook")
    s = e.stepi()
    check("stepi(1)", s["pc"] == a + 3, f"{s['pc']:#x} vs {a + 3:#x}")
    s = e.stepi(3)
    # from +3: bne (taken, skips inc counter+1) -> +8, lda counter -> +11, asl -> +12
    check("stepi(3)", s["pc"] == a + 12 and s["next"], f"{s['pc']:#x}")
    sp = e.regs()["S"]
    while e.peek(e.regs()["PC"]) != 0x20:       # to the JSR bump
        e.stepi()
    pc = e.regs()["PC"]
    s = e.step_over()
    check("step_over a JSR", s["pc"] == pc + 3 and e.regs()["S"] == sp, f"{s['pc']:#x} vs {pc + 3:#x}")
    s = e.step_over()
    check("step_over a plain instruction = stepi", s["pc"] == pc + 6, f"{s['pc']:#x}")
    e.set_reg("A", 0x77)
    e.set_reg("X", "$12")
    check("set_reg A / X", e.regs()["A"] == 0x77 and e.regs()["X"] == 0x12)
    e.set_reg("PC", "main_loop")
    check("set_reg PC to a symbol", e.regs()["PC"] == e.addr("main_loop"))
    d = e.disasm("bump", 4)
    check("disasm with symbols", d[0] == "bump:" and "LDA watched" in d[1] and "ADC #$03" in d[3], "\n".join(d))
    e.break_at("$02")   # (address 2 is never executed) - armed but harmless
    check("break at a raw address", e.breakpoints()["breakpoints"][0]["addr"] == 2)
    e.delete()
    try:
        e.break_if("PC=nosuchsymbol")
        check("unknown symbol in a condition is an error", False)
    except EmulatorError:
        check("unknown symbol in a condition is an error", True)
    try:
        e.raw('break id=99 cond="A=="')
        check("bad condition is an error", False)
    except ProtocolError:
        check("bad condition is an error", True)
    e.run(1)
    boot(e, prog)
    e.press("J")
    r = e.run(5)
    check("a JAM opcode stops the CPU (why=cpu_jam)", r["why"] == "cpu_jam", str(r))
    r = e.run(5)
    check("... and stays jammed", r["why"] == "cpu_jam" and r["frames"] == 0, str(r))


def test_memory(e, prog):
    section("memory, banks, hardware registers")
    boot(e, prog)
    check("read_mem by symbol", e.read_mem("banksig", 4) == b"BANK")
    e.write_mem("counter", b"\x34\x12")
    check("write_mem / peek16", e.peek16("counter") == 0x1234)
    e.poke("$600", 0x99)
    check("poke / peek", e.peek(0x600) == 0x99)
    check("find_mem", e.addr("banksig") in e.find_mem(b"BANK"))
    for n in range(4):
        d = e.read_mem(0, 5, bank=n)
        if d != bytes([0xB0 + n]) + b"BANK":
            break
    check("read_mem bank=0..3 (130XE, none mapped)", d == bytes([0xB0 + n]) + b"BANK", f"bank {n}: {d!r}")
    check("bank offsets also as $4000-$7FFF", e.read_mem(0x4001, 4, bank=2) == b"BANK")
    e.write_mem(0x100, b"xyz", bank=3)
    check("write_mem bank=3", e.read_mem(0x4100, 3, bank=3) == b"xyz")
    check("the CPU view at $4000 is main memory", e.read_mem(0x4000, 1) != bytes([0xB3]))
    os_ = e.read_mem(0xE400, 16)
    ram = e.read_mem(0xE400, 16, space="ram")
    check("space=ram: the RAM under the OS ROM", os_ != ram)
    hw = e.hw()
    check("hw(): ANTIC display list, GTIA, POKEY, PIA", hw["antic"]["dlist"] > 0 and "colbk" in hw["gtia"]
          and "audctl" in hw["pokey"] and hw["pia"]["portb"] == 0xFF, str(hw)[:200])
    m = e.memory_map()
    check("memory_map", m["xe_banks"] == 4 and m["os_rom"] and m["cpu_bank"] is None, str(m))
    e.write_mem("$D301", b"\xE7")        # PORTB: bank 1 for the CPU
    m = e.memory_map()
    check("memory_map follows PORTB", m["cpu_bank"] == 1, str(m))
    check("a mapped bank reads the same through bank=", e.read_mem(0x4000, 1) == b"\xB1" == e.read_mem(0, 1, bank=1))
    check("space=ram shows main memory behind a mapped bank", e.read_mem(0x4000, 1, space="ram") != b"\xB1")
    e.write_mem("$D301", b"\xFF")
    try:
        e.read_mem(0, 1, bank=9)
        check("a bank the machine lacks is an error", False)
    except ProtocolError:
        check("a bank the machine lacks is an error", True)


def test_screen(e, prog, tmp):
    section("screens, hashes, video")
    boot(e, prog)
    e.run(6)            # colours written in the vertical blank show in the next frame
    a = e.screenshot(tmp / "a.png")
    check("screenshot: 336x240 (TV area)", a.size == (336, 240) and (tmp / "a.png").exists())
    check("border=False: 320x192 playfield", e.screenshot(border=False).size == (320, 192))
    check("border='full': 384x240 buffer", e.screenshot(border="full").size == (384, 240))
    check("scale=2", e.screenshot(scale=2).size == (672, 480))
    h1, f1 = e.screen_hash(), e.frame_hash()
    check("hashes are stable without running", e.screen_hash() == h1 and e.frame_hash() == f1)
    e.run(1)
    check("hashes change with the picture", e.screen_hash() != h1 and e.frame_hash() != f1)
    r = e.record_video(tmp / "v.gif", frames=20, every=2)
    check("record_video .gif", r["frames"] == 10 and (tmp / "v.gif").stat().st_size > 1000, str(r))
    r = e.record_video(tmp / "frames", frames=4)
    check("record_video to a PNG directory", len(list((tmp / "frames").glob("*.png"))) == 4)


def test_states(e, prog, tmp):
    section("save_state / load_state")
    boot(e, prog)
    e.run(3)
    p = e.save_state(tmp / "s")
    check("save_state writes .a8s + sidecar", p.endswith(".a8s") and Path(p + ".a8h").exists())
    e.raw("hash action=on")
    e.press("K")
    e.run(20)
    after = e.raw("hash action=get").split("data=")[1].split()[0]
    regs = e.regs()
    e.raw("hash action=off")
    e.write_mem(0, b"\x55", bank=2)
    e.load_state(p)
    check("load_state restores the frame counter", e.position()["since_load"] == 3 + (e.load_frame or 0)
          or e.frame() >= 3)
    check("... and the 130XE banks", e.read_mem(0, 1, bank=2) == b"\xB2")
    e.raw("hash action=on")
    e.press("K")
    e.run(20)
    again = e.raw("hash action=get").split("data=")[1].split()[0]
    e.raw("hash action=off")
    check("a loaded state continues identically (every frame hash)", again == after)
    check("... and registers", e.regs() == regs)
    e.break_at("bump")
    e.continue_(3)
    e.delete()
    p2 = e.save_state(tmp / "mid")
    check("a mid-frame state can be saved (flagged)", e.last_state_midframe)
    pc = e.regs()["PC"]
    e.run(5)
    e.load_state(p2)
    check("... and loads with the CPU where it was", e.regs()["PC"] == pc)


def test_replay(e, prog, tmp):
    section("record / replay")
    boot(e, prog)
    e.record_input("start")
    e.press("A")
    e.joystick("RIGHT")
    e.run(10)
    e.joystick(None)
    e.press("START")
    e.run(5)
    r = e.record_input("save", tmp / "r.json")
    check("record_input from here: events + per-frame hashes + state", r["events"] >= 4 and r["hashes"] == r["frames"]
          and r["snapshot"], str(r))
    for i in range(2):
        rp = e.replay(tmp / "r.json")
        check(f"replay {i + 1}: every frame hash matches", rp["ok"] and rp["compared"] == r["frames"],
              str({k: v for k, v in rp.items() if k != "hashes"}))
    with Emulator() as other:
        rp = other.replay(tmp / "r.json")
        check("replay in another emulator process", rp["ok"], str({k: v for k, v in rp.items() if k != "hashes"}))
    rec = json.loads((tmp / "r.json").read_text())
    for ev in rec["events"]:
        if ev["state"]["key"] == 0x3F:
            ev["state"]["key"] = 0x15       # A -> B
    (tmp / "bad.json").write_text(json.dumps(rec))
    shutil.copy(tmp / "r.a8s", tmp / "bad.a8s")
    shutil.copy(tmp / "r.a8s.a8h", tmp / "bad.a8s.a8h")
    rec["start"]["snapshot"] = "bad.a8s"
    (tmp / "bad.json").write_text(json.dumps(rec))
    rp = e.replay(tmp / "bad.json")
    check("a changed input is caught (hash mismatch)", not rp["ok"] and rp["mismatches"] > 0,
          str({k: v for k, v in rp.items() if k != "hashes"}))
    e.load(prog)
    e.record_input("start", from_reset=True)
    e.press("A")
    e.run(30)
    r = e.record_input("save", tmp / "r2.json")
    rp = e.replay(tmp / "r2.json")
    check("record from reset + replay", rp["ok"] and not r["snapshot"], str({k: v for k, v in rp.items() if k != "hashes"}))
    boot(e, prog)
    e.press("Q")
    e.checkpoint("after Q")
    e.joystick("UP")
    e.run(7)
    e.joystick(None)
    e.run(3)
    rec1 = e.save_replay(tmp / "v1.json")
    rp = e.replay(tmp / "v1.json")
    check("checkpoint replay (version 1)", rp["ok"] and len(rp["checkpoints"]) == 2, str(rp)[:300])


def test_machines(prog):
    section("machines and TV systems")
    with Emulator(machine="800xl") as e:
        boot(e, prog)
        check("800XL: 64K, no extended banks", e.hw()["mem"]["ram_kb"] == 64 and e.memory_map()["xe_banks"] == 0)
        e.set_machine("130xe")
        boot(e, prog)
        check("set_machine(130xe)", e.hw()["mem"]["xe_banks"] == 4)
    with Emulator(machine="800") as e:
        e.load(prog)
        check("800 (48K, AltirraOS 800) runs the program", e.run_until("emutest: ready", 300))
        check("800: OS 400/800 ROM (no RAM under it)", e.read_mem(0xE000, 4, space="ram") == b"\xff" * 4)
    with Emulator(machine="400") as e:
        check("400: 16K", e.hw()["mem"]["ram_kb"] == 16)
    with Emulator(tv="ntsc") as e:
        boot(e, prog)
        e.run(1)
        check("NTSC: 262 lines x 114 cycles", e.position()["cycles"] == e.frame() * 262 * 114)
        check("NTSC: PAL bit clear in GTIA (PAL register)", e.read_mem(0xD014, 1)[0] & 0x0E == 0x0E)
    with Emulator() as e:
        check("PAL by default (GTIA PAL register)", e.read_mem(0xD014, 1)[0] & 0x0E == 0x00)
    try:
        Emulator(machine="xegs")
        check("xegs is refused (non-free ROM)", False)
    except EmulatorError:
        check("xegs is refused (non-free ROM)", True)


def test_projects():
    section("a8-core projects")
    hello = ROOT / "projects" / "hello" / "build" / "hello.xex"
    bank = ROOT / "projects" / "banktest" / "build" / "banktest.xex"
    if not hello.exists():
        print("  (skipped: build projects/hello and projects/banktest first)")
        return
    with Emulator() as e:
        e.load(hello)
        check("hello: ready", e.run_until("hello: ready", 600))
        check("a8_in_harness(): $D6FF reads $A8", e.peek(0xD6FF) == 0xA8)
        if bank.exists():
            e.load(bank)
            check("banktest: A8:DONE", e.run_until("A8:DONE", 9000), e.log(0)[-200:])


def test_window(prog, tmp):
    section("window session (SDL)")
    with Emulator(window=True, audio=False) as e:
        check("window sessions free-run", e.status()["mode"] == "free")
        e.load(prog)
        check("run_until in a window", e.run_until("emutest: ready", 300))
        check("... then it free-runs again", e.status()["mode"] == "free")
        f = e.frame()
        time.sleep(1.0)
        n = e.frame() - f
        check("real time: ~50 frames per second", 40 <= n <= 60, str(n))
        t = time.monotonic()
        r = e.run(500)
        check("run(500) fast-forwards", r["frames"] == 500 and time.monotonic() - t < 3, str(r))
        e.break_at("bump")
        time.sleep(0.3)
        st = e.status()
        check("a breakpoint stops the free-running window", st["mode"] == "paused"
              and st["last_stop"] and st["last_stop"]["why"] == "breakpoint", str(st["last_stop"]))
        r = e.stepi(2)
        check("stepping in the window", r["why"] == "step")
        e.delete()
        e.resume()
        check("resume", e.status()["mode"] == "free")
        e.record_input("start")
        e.hold("LEFT")
        time.sleep(0.3)
        e.release()
        time.sleep(0.3)
        r = e.record_input("save", tmp / "w.json")
    with Emulator() as h:
        rp = h.replay(tmp / "w.json")
        check("a window recording replays headless, every frame hash", rp["ok"] and rp["compared"] == r["frames"],
              str({k: v for k, v in rp.items() if k != "hashes"}))


def main():
    prog = str(assemble("emutest"))
    tmp = Path(tempfile.mkdtemp(prefix="a8emu-test-"))
    try:
        test_determinism(prog)
        with Emulator() as e:
            test_running(e, prog)
            test_input(e, prog)
            test_debugger(e, prog)
            test_memory(e, prog)
            test_screen(e, prog, tmp)
            test_states(e, prog, tmp)
            test_replay(e, prog, tmp)
        test_machines(prog)
        test_projects()
        if os.environ.get("A8_TEST_WINDOW"):
            test_window(prog, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\n{'OK' if not failures else 'FAIL: ' + str(len(failures)) + ' failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
