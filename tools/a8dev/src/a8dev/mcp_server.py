"""MCP server: build, run, inspect and debug Atari 8-bit (400/800/XL/XE/130XE) programs.

Registered for Claude Code in the repo's .mcp.json (stdio), and served over
HTTP by DevBench at /mcp. A *session* is a separate `a8dev serve` process (one
patched Atari800 + control port), so several clients can share one emulator.

  emu_boot(project)               private headless session, paused (you step it)
  emu_boot(project, window=True)  ... with a window the user can watch (free-running)
  emu_attach()                    drive a session someone else started (./a8 run X)
  arcade_play(game)               the game menu on an ATR disk image (./a8 arcade)
"""
from __future__ import annotations

import base64
import json
import subprocess

from mcp.server.mcpserver import Image, MCPServer

from .control import ProtocolError, SessionClient, list_sessions
from .emu import repo_root
from .session import resolve_program

ROOT = repo_root()

server = MCPServer(
    name="a8",
    instructions=(
        "Build, run, inspect and debug Atari 8-bit programs (400/800, XL/XE, 128K 130XE; cc65 C "
        "and ca65/MADS asm) in a patched Atari800 with the free AltirraOS. Loop: build(project) -> "
        "emu_boot(project) -> emu_run_until('<log line>') -> emu_look / emu_press / emu_log. "
        "Sessions start paused: frames (1/50 s, PAL) advance only when you run/press/continue, "
        "deterministically and much faster than real time. Guest code logs with "
        "a8_log(\"tag: key=%u\\n\", v) (sdk/include/a8log.h). Debugging: emu_break('main'), "
        "emu_continue, emu_regs, emu_disasm, emu_watch('var'), emu_stepi, emu_read_mem('sym'), "
        "emu_read_mem(offset, bank=N) for 130XE banks. Keys: A-Z 0-9 SPACE RETURN ESC TAB DELETE, "
        "SHIFT+x CTRL+x; console START SELECT OPTION; joystick UP DOWN LEFT RIGHT FIRE (port 0). "
        "emu_cmd('help') lists every session command; emu_cmd('raw', {'line': 'help'}) reaches the emulator."
    ),
)

_session: SessionClient | None = None


def _j(obj) -> str:
    return json.dumps(obj, indent=1)


def _s() -> SessionClient:
    global _session
    if _session is not None and not _session.alive:
        _session = None
    if _session is None:
        ss = list_sessions()
        if ss:
            _session = SessionClient(ss[-1]["control_port"])
            _session.info = ss[-1]
    if _session is None:
        raise ValueError("no emulator session; call emu_boot(project) or emu_attach() first")
    return _session


def _call(cmd: str, **args):
    try:
        return _s().call(cmd, **{k: v for k, v in args.items() if v is not None})
    except ProtocolError as ex:
        raise ValueError(str(ex)) from None


def _img(res) -> Image:
    return Image(data=base64.b64decode(res["png_b64"]), format="png")


# ================================================================== build


@server.tool()
def list_projects() -> str:
    """List projects (projects/<name>) and their build output (.xex)."""
    out = []
    for d in sorted((ROOT / "projects").iterdir()):
        mk = d / "Makefile"
        if mk.exists():
            built = sorted(str(p.relative_to(ROOT)) for ext in ("*.xex", "*.atr") for p in (d / "build").glob(ext))
            out.append({"project": d.name, "built": built, "test": (d / "test.py").exists()})
    return _j(out)


@server.tool()
def build(project: str, clean: bool = False) -> str:
    """Compile projects/<project> with cc65 (in Docker) -> build/<name>.xex + .map + .lbl.
    Returns compiler diagnostics on failure."""
    proj = ROOT / "projects" / project
    if not (proj / "Makefile").exists():
        raise ValueError(f"no project at {proj}")
    cmd = [str(ROOT / "bin" / "a8-make"), "-C", str(proj)]
    if clean:
        subprocess.run(cmd + ["clean"], capture_output=True, text=True)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    out = (r.stdout + r.stderr).splitlines()
    keep = [l for l in out if not l.startswith(("cl65 ", "ca65 ", "ar65 ", "mkdir ", "rm -f"))]
    status = "OK" if r.returncode == 0 else f"FAILED (exit {r.returncode})"
    return f"build {project}: {status}\n" + "\n".join(keep[-80:])


@server.tool()
def new_project(name: str) -> str:
    """Create projects/<name> from sdk/template."""
    r = subprocess.run([str(ROOT / "a8"), "new", name], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()


@server.tool()
def run_tests(project: str) -> str:
    """Build and run a project's headless test (projects/<p>/test.py, else wait for A8:DONE)."""
    r = subprocess.run([str(ROOT / "a8"), "test", project], capture_output=True, text=True, timeout=1800)
    return ((r.stdout + r.stderr).strip()[-6000:] + f"\n(exit {r.returncode})")


# ================================================================== arcade


@server.tool()
def arcade_games() -> str:
    """The arcade's games (every project with an arcade.json): title, controls,
    description, the menu picture (PNG path), and whether the disk image is built."""
    from . import arcade
    m = arcade.manifest()
    return _j({"image": m.get("image"), "built": bool(m.get("image")), "games": [
        {k: g.get(k) for k in ("project", "title", "controls", "description", "ready",
                               "image_picture", "index")} for g in m["games"]]})


@server.tool()
def arcade_picture(game: str) -> Image:
    """A game's picture as the arcade menu shows it."""
    from . import arcade
    import io
    g = arcade.find_game(arcade.manifest()["games"], game)
    buf = io.BytesIO()
    arcade.picture(g["picture"])[1].resize((320, 240)).save(buf, "PNG")
    return Image(data=buf.getvalue(), format="png")


@server.tool()
def arcade_build() -> str:
    """Build every game + the menu and make the disk image build/arcade/arcade.atr."""
    r = subprocess.run([str(ROOT / "a8"), "arcade", "--image-only"], capture_output=True, text=True)
    return (r.stdout + r.stderr)[-3000:] + ("" if r.returncode == 0 else f"\nexit {r.returncode}")


@server.tool()
def arcade_play(game: str = "", window: bool = True) -> str:
    """Boot the arcade (the menu from the ATR disk image; builds it if missing) in a new
    session, and if `game` is given (project or title) choose it in the menu and run to its
    title. window=True (default) opens a window for the user to play; the emu_* tools drive
    the session too. Reset (emu_reset) returns to the menu."""
    global _session
    from . import arcade
    if not (arcade.OUT / "arcade.atr").exists():
        out = arcade_build()
        if not (arcade.OUT / "arcade.atr").exists():
            raise ValueError(f"arcade build failed: {out}")
    if _session is not None and _session.proc is not None:
        _session.close(stop=True)
    _session = SessionClient.spawn(str(arcade.OUT / "arcade.atr"), window=window)
    if game:
        return _j(_call("arcade_play", game=game))
    return _j(_call("status"))


# ================================================================== sessions


@server.tool()
def emu_boot(project: str = "", window: bool = False, machine: str = "") -> str:
    """Start an emulator session with a project (or a .xex/.atr/.car/.a8s path).
    Headless sessions are paused: they only advance when you run/press. window=True opens
    a window the user can watch and play (free-running). machine: 130xe (default, 128K),
    800xl, 800 (48K 400/800),. Empty project = just the OS."""
    global _session
    if _session is not None and _session.proc is not None:
        _session.close(stop=True)
    prog = resolve_program(project) if project else None
    _session = SessionClient.spawn(prog, window=window, machine=machine or None)
    return _j(_call("status"))


@server.tool()
def emu_attach(pid: int = 0) -> str:
    """Attach to a running session (default: newest), e.g. one started with ./a8 run."""
    global _session
    ss = list_sessions()
    if not ss:
        raise ValueError("no running sessions")
    info = next((s for s in ss if s["pid"] == pid), None) if pid else ss[-1]
    if info is None:
        raise ValueError(f"no session with pid {pid}; running: {[s['pid'] for s in ss]}")
    _session = SessionClient(info["control_port"])
    _session.info = info
    return _j(_call("status"))


@server.tool()
def emu_sessions() -> str:
    """Running sessions (pid, ports, program, window)."""
    return _j(list_sessions())


@server.tool()
def emu_close() -> str:
    """Stop the current session (and its emulator)."""
    global _session
    s = _s()
    s.close(stop=True)
    _session = None
    return "closed"


@server.tool()
def emu_status() -> str:
    """Machine, frame, PC (+symbol), program, paused/running, breakpoints."""
    return _j(_call("status"))


@server.tool()
def emu_load(path: str, machine: str = "") -> str:
    """Reset and load a program (project name or file) into the current session."""
    return _j(_call("load", path=path, machine=machine or None))


@server.tool()
def emu_reset(hard: bool = True) -> str:
    """Reset the machine: hard (RAM cleared, like power on) or soft (RESET key: warm start).
    The program is gone after a hard reset; emu_reload reruns it."""
    return _j(_call("reset", hard=hard))


@server.tool()
def emu_reload() -> str:
    """Reset and load the same program again."""
    return _j(_call("reload"))


@server.tool()
def emu_power_cycle() -> str:
    """Restart the emulator process cold (same machine, no program loaded)."""
    return _j(_call("power_cycle"))


@server.tool()
def emu_reboot() -> str:
    """Recover a wedged session: kill the emulator (even mid-command), start a
    fresh one and reload the program."""
    return _j(_call("reboot"))


@server.tool()
def emu_machine(name: str) -> str:
    """Switch machine: 130xe 800xl 65xe 600xl 1200xl 800 400 320xe (resets)."""
    return _j(_call("machine", name=name))


# ================================================================== running


@server.tool()
def emu_run(frames: int = 50) -> str:
    """Advance N frames (50 = one second of guest time, PAL). Stops early at a breakpoint."""
    return _j(_call("run", frames=frames))


@server.tool()
def emu_run_until(text: str, max_frames: int = 3000) -> str:
    """Run until the guest log contains `text` (only output after the last emu_log /
    emu_run_until match / load counts). Stops on the exact instruction that logged it."""
    return _j(_call("run_until", text=text, max_frames=max_frames))


@server.tool()
def emu_pause() -> str:
    """Stop a free-running (window) session; frames then advance only on request."""
    return _j(_call("pause"))


@server.tool()
def emu_resume(turbo: bool = False) -> str:
    """Let the session free-run in real time (turbo=True: as fast as possible)."""
    return _j(_call("resume", turbo=turbo))


@server.tool()
def emu_position() -> str:
    """Current position: absolute frame, frames since load, CPU cycle within the frame."""
    return _j(_call("position"))


@server.tool()
def emu_run_to(frame: int, tstates: int = 0) -> str:
    """Run to an exact position (frames since load, cycle in the frame): the first instruction
    boundary at or after it. Breakpoints still stop it."""
    return _j(_call("run_to", frame=frame, tstates=tstates))


@server.tool()
def emu_log(all: bool = False) -> str:
    """Guest debug log (a8_log output) since the previous emu_log call; all=True for everything."""
    return _call("log", all=all)["text"] or "(no new output)"


# ================================================================== screen + input


@server.tool()
def emu_look(border: bool = True, scale: int = 2) -> Image:
    """Screenshot of the rendered frame (playfield + players/missiles). border=False
    crops to the playfield; scale enlarges."""
    return _img(_call("screenshot", border=border, scale=scale))


@server.tool()
def emu_screenshot(path: str, border: bool = True, scale: int = 1) -> str:
    """Save a PNG screenshot to a file (native resolution unless scale)."""
    r = _call("screenshot", path=path, border=border, scale=scale)
    return f"saved {path} ({r['width']}x{r['height']}, {r['colors']} colours)"


@server.tool()
def emu_record_video(path: str, frames: int = 100, every: int = 1, scale: int = 1,
                     layer: str = "screen") -> str:
    """Record frames to .gif, .mp4 or a directory of PNGs. MP4 needs ffmpeg on PATH (not
    installed by us; you get an error otherwise). Paused sessions advance `frames` frames
    deterministically while recording."""
    return _j(_call("record_video", path=path, frames=frames, every=every, scale=scale, layer=layer))


@server.tool()
def emu_record_gif(path: str, frames: int = 100, every: int = 2, scale: int = 1) -> str:
    """Record N frames as an animated GIF (every = keep one frame in `every`)."""
    if not path.lower().endswith(".gif"):
        path += ".gif"
    return _j(_call("record_video", path=path, frames=frames, every=every, scale=scale))


@server.tool()
def emu_screen_hash() -> str:
    """md5 of the current frame's pixels: compare runs cheaply."""
    return _call("screen_hash")["md5"]


# ================================================================== replay


@server.tool()
def emu_input_log() -> str:
    """Every input change since the program was loaded, with frame + cycle."""
    return _j(_call("input_log"))


@server.tool()
def emu_checkpoint(label: str = "") -> str:
    """Record the screen hash at this point; a replay verifies it at the same point."""
    return _j(_call("checkpoint", label=label))


@server.tool()
def emu_save_replay(path: str) -> str:
    """Save the input log since load + checkpoints + final screen hash as a replay (JSON)."""
    return _j(_call("save_replay", path=path))


@server.tool()
def emu_record_input(action: str = "start", path: str = "", from_reset: bool = False) -> str:
    """Record every input change - yours AND the user's keyboard/joystick in the window -
    with its frame + cycle, plus a hash of every frame.
    action: start (from now: a snapshot is taken to replay from; from_reset=True reloads
    the program and records from the load), stop, save (path: JSON + .a8s beside it), status."""
    return _j(_call("record_input", action=action, path=path or None, from_reset=from_reset))


@server.tool()
def emu_replay(path: str, verify: bool = True) -> str:
    """Replay a recording (emu_record_input save, or emu_save_replay): restart from its
    reset/snapshot, re-apply the inputs at the same frames/cycles, and check the screen
    hashes (every frame for emu_record_input recordings). Result has ok, compared, mismatches,
    first_mismatch; per-frame hashes are summarised."""
    r = _call("replay", path=path, verify=verify)
    if isinstance(r.get("hashes"), list):
        hs = r.pop("hashes")
        r["hash_count"] = len(hs)
        r["hash_digest"] = __import__("hashlib").md5("".join(h for _, h in hs).encode()).hexdigest()
    return _j(r)


@server.tool()
def emu_press(keys: str, hold_frames: int = 3, release_frames: int = 3) -> str:
    """Press keys together, e.g. 'SPACE', 'Q', 'SHIFT+A', 'CTRL+S', 'START', joystick 'UP+FIRE'.
    Keys: A-Z 0-9 SPACE RETURN ESC TAB DELETE and punctuation (POKEY: one key at a time,
    plus SHIFT/CTRL). Console: START SELECT OPTION (GTIA CONSOL; combine with keys).
    Joystick port 0: UP DOWN LEFT RIGHT FIRE. Also HELP (XL/XE) and BREAK."""
    return _j(_call("press", keys=keys, hold_frames=hold_frames, release_frames=release_frames))


@server.tool()
def emu_hold(keys: str) -> str:
    """Press and keep holding keys/joystick directions (until emu_release)."""
    return _j(_call("hold", keys=keys))


@server.tool()
def emu_release(keys: str = "") -> str:
    """Release held keys (empty = all)."""
    return _j(_call("release", keys=keys or None))


@server.tool()
def emu_type(text: str) -> str:
    """Type text key by key (\\n = ENTER). """
    return _j(_call("type", text=text))


@server.tool()
def emu_joystick(dirs: str = "", port: int = 0) -> str:
    """Set a joystick and keep it so: dirs e.g. 'LEFT', 'UP+FIRE', '' = centred, port 0..3
    (0..1 on XL/XE). For a press of a few frames use emu_press('UP')."""
    return _j(_call("joystick", dirs=dirs, port=port))


@server.tool()
def emu_sequence(steps: list[dict]) -> str:
    """Scripted input: [{"press":"Q","hold":3}, {"run":20}, {"until":"text","max":500},
    {"hold":"RIGHT"}, {"release":"RIGHT"}, {"type":"HELLO"}]. Add "at": N (frames since
    load, optional "t": cycle) to any step for timed press/release scripts."""
    return _j(_call("sequence", steps=steps))


# ================================================================== debugging


@server.tool()
def emu_regs() -> str:
    """6502 registers (A X Y S P + flags, PC) and the symbol at PC."""
    return _j(_call("regs"))


@server.tool()
def emu_set_reg(reg: str, value: str) -> str:
    """Set a register (PC, A, X, Y, S, P). value: number, $hex, or symbol."""
    return _j(_call("set_reg", reg=reg, value=value))


@server.tool()
def emu_read_mem(addr: str, length: int = 64, bank: int = -1, space: str = "cpu") -> str:
    """Hex dump of memory at an address or symbol ('player_x', '$A010', 'buf+16') as the
    CPU sees it now (hardware registers read without side effects). bank=N (0..3) reads
    130XE extended bank N whether mapped or not; addr is then the offset 0..$3FFF (or
    $4000-$7FFF). space="ram": the RAM under the OS/BASIC ROMs and behind a mapped bank."""
    return _call("read_mem", addr=addr, length=length, bank=None if bank < 0 else bank,
                 space=space)["dump"]


@server.tool()
def emu_write_mem(addr: str, hex: str, bank: int = -1, space: str = "cpu") -> str:
    """Write bytes (hex string) at an address/symbol, into a 130XE bank, or (space="ram")
    to the RAM under the ROMs (see emu_read_mem)."""
    return _j(_call("write_mem", addr=addr, hex=hex, bank=None if bank < 0 else bank, space=space))


@server.tool()
def emu_run_until_signal(max_frames: int = 3000) -> str:
    """Run until the program writes a status byte to $D1F1 (a8_signal(v); a8_done() = 0,
    a8_fail() = 1). The result's status has "signal"."""
    return _j(_call("run_until_signal", max_frames=max_frames))


@server.tool()
def emu_run_to_addr(addr: str, max_frames: int = 500) -> str:
    """Run until the PC reaches an address or symbol (a one-shot breakpoint)."""
    return _j(_call("run_to_addr", addr=addr, max_frames=max_frames))


@server.tool()
def emu_console(keys: str = "") -> str:
    """Hold console keys until changed: 'START', 'SELECT+OPTION'; '' releases them.
    (emu_press('START') is a short press.)"""
    return _j(_call("console", keys=keys or None))


@server.tool()
def emu_hw() -> str:
    """Chip registers as last written (ANTIC: DMACTL, DLIST, CHBASE, PMBASE...; GTIA:
    colours, HPOS, PRIOR, GRACTL...; POKEY: AUDF/AUDC/AUDCTL...; PIA: PORTB) and the memory setup."""
    return _j(_call("hw"))


@server.tool()
def emu_memory_map() -> str:
    """PORTB memory control: OS ROM, BASIC, self test, the 130XE bank the CPU/ANTIC see."""
    return _j(_call("memory_map"))


@server.tool()
def emu_find_mem(text: str = "", hex: str = "") -> str:
    """Find a byte string (text or hex) in the 64K address space."""
    return _j(_call("find_mem", text=text or None, hex=hex or None))


@server.tool()
def emu_disasm(addr: str = "pc", count: int = 12) -> str:
    """Disassemble from an address/symbol (default PC), with symbol labels."""
    return _call("disasm", addr=addr, count=count)["listing"]


@server.tool()
def emu_symbols(pattern: str = "") -> str:
    """Symbols from the program's .lbl/.map (regex filter). C names have a leading underscore."""
    return _j(_call("symbols", pattern=pattern))


@server.tool()
def emu_break(addr: str = "", condition: str = "") -> str:
    """Breakpoint at an address/symbol ('main', '$2090'), or on a condition
    ('A=5 AND PC=$2090', see emu_cmd('raw', {'line': 'help'})). Then emu_continue."""
    return _j(_call("break", addr=addr or None, condition=condition or None))


@server.tool()
def emu_watch(addr: str, length: int = 1, kind: str = "w") -> str:
    """Data watchpoint on [addr, addr+length): kind r, w or rw. Then emu_continue."""
    return _j(_call("watch", addr=addr, length=length, kind=kind))


@server.tool()
def emu_delete(index: int = 0, addr: str = "") -> str:
    """Delete breakpoint #index or the watchpoint at addr; no arguments = all."""
    return _j(_call("delete", index=index or None, addr=addr or None))


@server.tool()
def emu_continue(max_frames: int = 500) -> str:
    """Run until a breakpoint/watchpoint fires (result has stopped, pc, symbol, regs)."""
    return _j(_call("continue", max_frames=max_frames))


@server.tool()
def emu_stepi(count: int = 1) -> str:
    """Execute `count` 6502 instructions; returns registers and the next instruction."""
    return _j(_call("stepi", count=count))


@server.tool()
def emu_step_over() -> str:
    """Step over the current instruction (runs a JSR to completion)."""
    return _j(_call("step_over"))


@server.tool()
def emu_backtrace(items: int = 8) -> str:
    """Return addresses on the 6502 stack (page 1 from S), with symbols."""
    return _j(_call("backtrace", items=items))


@server.tool()
def emu_save_state(name: str) -> str:
    """Snapshot the machine (Atari800 .a8s; plain names go to build/states/)."""
    return _j(_call("save_state", path=name))


@server.tool()
def emu_load_state(name: str) -> str:
    """Restore a snapshot saved with emu_save_state (or any .a8s)."""
    return _j(_call("load_state", path=name))


@server.tool()
def emu_cmd(cmd: str, args: dict | None = None) -> str:
    """Any session command (emu_cmd('help') lists them), e.g. emu_cmd('raw', {'line': 'help'})."""
    res = _call(cmd, **(args or {}))
    if isinstance(res, dict) and "png_b64" in res:
        res = {k: v for k, v in res.items() if k != "png_b64"}
    return _j(res)


def main():
    server.run()


if __name__ == "__main__":
    main()
