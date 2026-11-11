"""Session process: one Emulator + a JSON-lines control port + a registry entry.

    a8dev serve projects/hello/build/hello.xex [--window] [--mode running]

Clients (./a8 ctl, the MCP server, DevBench, scripts) send one JSON object per
line and get one back:

    {"id": 1, "cmd": "press", "args": {"keys": "SPACE"}}
    {"id": 1, "ok": true, "result": {...}}

Commands run one at a time (a lock), so any number of clients can share a
session. The emulator's own control port (emu_port in the registry) is the
patched Atari800's harness protocol (emu_cmd("raw", line="help")).
"""
from __future__ import annotations

import base64
import io
import json
import os
import signal
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path

from .emu import LOADABLE, Emulator, EmulatorError, repo_root
try:
    from .emu import ProtocolError as ZrcpError   # the emulator link's error
except ImportError:  # pragma: no cover
    class ZrcpError(RuntimeError):
        pass

SESSIONS_DIR = repo_root() / "build" / "sessions"


def resolve_program(target: str | None) -> str | None:
    """Project name or file path -> loadable file (newest .xex/.atr in build/)."""
    if not target:
        return None
    p = Path(target)
    if p.suffix.lower() in LOADABLE:
        if not p.exists():
            raise FileNotFoundError(f"{p} not found")
        return str(p.resolve())
    d = repo_root() / "projects" / target / "build"
    cands = [c for ext in (".xex", ".atr", ".car") for c in d.glob(f"*{ext}")]
    if not cands:
        raise FileNotFoundError(f"nothing built for project {target!r} (./a8 build {target})")
    return str(max(cands, key=lambda c: c.stat().st_mtime))


def _hexdump(addr: int, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{(addr + i) & 0xFFFF:04X}  {chunk.hex(' '):<47}  {asc}")
    return "\n".join(lines)


class Session:
    def __init__(self, emu: Emulator, info: dict):
        self.emu = emu
        self.info = info
        self.lock = threading.RLock()
        self.quit = threading.Event()
        self.registry: Path | None = None

    # each command: (function, help)
    def commands(self):
        e = self.emu
        return {
            "help": (lambda: {k: v[1] for k, v in sorted(self.commands().items())}, "this list"),
            "status": (lambda: {**e.status(), "session": self.info}, "machine, frame, pc, program, ports"),
            "load": (lambda path, machine=None: e.load(resolve_program(path), machine),
                     "load(path|project, machine?): power on + run a .xex (or boot .atr/.car, load .a8s)"),
            "reset": (lambda hard=True: (e.reset(hard), e.status())[1],
                      "reset(hard=true): hard reset (RAM cleared) or soft reset (CPU only)"),
            "reload": (lambda: e.reload(), "reset + load the same program again"),
            "power_cycle": (lambda: e.power_cycle(), "fresh emulator process, same machine, no program"),
            "reboot": (lambda: e.reboot(), "kill + restart the emulator and reload the program (wedged session)"),
            "position": (lambda: e.position(), "frame, frames since load, T-state"),
            "run_to": (lambda frame, tstates=0: e.run_to(frame, tstates),
                       "run_to(frame, tstates=0): to an exact position (frames since load)"),
            "input_log": (lambda: {"events": e.input_log()}, "input changes since load, with positions"),
            "checkpoint": (lambda label="": e.checkpoint(label), "checkpoint(label): record the screen hash here"),
            "save_replay": (lambda path: {"path": path, **{k: v for k, v in e.save_replay(self._rel(path)).items() if k != "events"},
                                          "events": len(e.input_log())},
                            "save_replay(path): input log + checkpoints since load -> JSON"),
            "replay": (lambda path, verify=True: e.replay(self._rel(path), verify),
                       "replay(path, verify=true): restart from the recording's reset/snapshot, re-apply "
                       "inputs, compare per-frame screen hashes"),
            "record_input": (lambda action="start", path=None, from_reset=False:
                             e.record_input(action, self._rel(path) if path else None, from_reset),
                             "record_input(action=start|stop|save|status, path?, from_reset=false): "
                             "record agent + window input and per-frame hashes"),
            "screen_hash": (lambda layer="screen": {"md5": e.screen_hash(layer)}, "screen_hash(layer=screen)"),
            "record_video": (lambda path, frames=100, every=1, scale=1, layer="screen":
                             e.record_video(self._rel(path), frames, every, scale, layer),
                             "record_video(path .gif|.mp4|dir, frames=100, every=1, scale=1)"),
            "memory_map": (lambda: e.memory_map(), "PORTB: OS ROM, BASIC, self test, 130XE bank mapped for the CPU / ANTIC"),
            "machine": (lambda name=None: (e.set_machine(name), e.status())[1] if name else {"machine": e.machine},
                        "machine(name): 130xe 800xl 65xe 600xl 1200xl 800 400 320xe"),
            "pause": (lambda: (e.pause(), e.status())[1], "stop free-running"),
            "resume": (lambda turbo=False: (e.resume(turbo), e.status())[1], "resume(turbo=false): free-run"),
            "run": (lambda frames=1: e.run(frames), "run(frames): advance N frames (stops at breakpoints)"),
            "run_until": (lambda text, max_frames=3000, since=None: {
                "found": e.run_until(text, max_frames, since), **e.last_run},
                "run_until(text, max_frames=3000, since?): run until the guest log contains text"),
            "log": (lambda since=None, all=False: {
                "text": "\n".join(e.debug_log) if all else e.log(since), "cursor": e.log_cursor()},
                "log(since?, all=false): guest log since the last log call"),
            "log_cursor": (lambda: {"cursor": e.log_cursor()}, "current end of the log"),
            "press": (lambda keys, hold_frames=3, release_frames=3: e.press(keys, hold_frames, release_frames),
                      "press(keys, hold_frames=3, release_frames=3): e.g. 'SPACE', 'Q+A', 'UP+FIRE'"),
            "hold": (lambda keys: (e.hold(keys), {"held": e._held})[1], "hold(keys): press and keep held"),
            "release": (lambda keys=None: (e.release(keys), {"held": e._held})[1], "release(keys?): release (all)"),
            "type": (lambda text, hold_frames=3, gap_frames=3: e.type(text, hold_frames, gap_frames),
                     "type(text): key by key; \\n = ENTER"),
            "joystick": (lambda dirs=None, port=0: {"joystick": e.joystick(dirs, port)},
                         "joystick(dirs?, port=0): set stick 0 to e.g. 'UP+FIRE', '' = centre"),
            "sequence": (lambda steps: e.sequence(steps),
                         "sequence(steps): [{press,hold}, {run}, {until,max}, {type}, {hold}, {release}]"),
            "screenshot": (self._screenshot,
                           "screenshot(path?, border=true, scale=1): PNG (png_b64)"),
            "regs": (lambda: {**e.regs(), "pc_symbol": e.symbols.name_of(e.regs()["PC"])}, "CPU registers"),
            "set_reg": (lambda reg, value: e.set_reg(reg, value), "set_reg(reg, value)"),
            "read_mem": (self._read_mem,
                         "read_mem(addr, length=64, bank?): hex + dump; addr may be a symbol; "
                         "bank = 130XE extended bank 0..3 (addr = offset 0..$3FFF or $4000-$7FFF)"),
            "write_mem": (self._write_mem, "write_mem(addr, hex, bank?)"),
            "find_mem": (self._find_mem, "find_mem(hex|text, start=0, end=65536)"),
            "disasm": (lambda addr="pc", count=12: {"listing": "\n".join(e.disasm(addr, count))},
                       "disasm(addr=pc, count=12)"),
            "symbols": (lambda pattern="", limit=200: e.symbols.search(pattern, limit),
                        "symbols(pattern): from the program's .map"),
            "addr2sym": (lambda addr: {"addr": e.addr(addr), "symbol": e.symbols.name_of(e.addr(addr))},
                         "addr2sym(addr)"),
            "break": (lambda addr=None, condition=None: e.break_if(condition) if condition else e.break_at(addr),
                      "break(addr|condition): e.g. 'main', '$8000', condition='A=5 AND PC=32768'"),
            "watch": (lambda addr, length=1, kind="w": e.watch(addr, length, kind),
                      "watch(addr, length=1, kind=r|w|rw)"),
            "delete": (lambda index=None, addr=None: (e.delete(index, addr), e.breakpoints())[1],
                       "delete(index?|addr?): no args = everything"),
            "breakpoints": (lambda: e.breakpoints(), "list breakpoints and watchpoints"),
            "continue": (lambda max_frames=500: e.continue_(max_frames),
                         "continue(max_frames=500): run until a breakpoint/watchpoint"),
            "stepi": (lambda count=1: e.stepi(count), "stepi(count=1)"),
            "step_over": (lambda: e.step_over(), "step over CALL/RST"),
            "backtrace": (lambda items=8: e.backtrace(items), "backtrace(items=8): stack words + symbols"),
            "save_state": (lambda path: {"path": e.save_state(self._state_path(path))},
                           "save_state(path|name): .zsf (default), .z80, .sna"),
            "load_state": (lambda path: e.load_state(self._state_path(path)), "load_state(path|name)"),
            "raw": (lambda line, timeout=30: {"output": e.raw(line, timeout)},
                    "raw(line): a raw harness-protocol command to the patched Atari800 (raw('help'))"),
            "arcade_play": (self._arcade_play,
                            "arcade_play(game): in a session on the arcade SD image, reset into the menu, "
                            "choose the game (project, title or short name) and run to its title"),
            "quit": (self._quit, "stop the session and the emulator"),
        }

    def _arcade_play(self, game):
        from . import arcade
        e = self.emu
        gs = arcade.manifest()["games"]
        try:
            g = arcade.find_game(gs, game)
        except arcade.ArcadeError as ex:
            raise EmulatorError(str(ex)) from None
        if not (e.program or "").endswith(".atr"):
            raise EmulatorError("not an arcade session: start one on build/arcade/arcade.atr (./a8 arcade)")
        e.reload()
        started = arcade.menu_choose(e, g, len(gs))
        return {"started": started, "game": g["title"], **e.status()}

    def _rel(self, p) -> str:
        """Relative paths are relative to the repo root (sessions have no useful cwd)."""
        return str(p if Path(p).is_absolute() else repo_root() / p)

    def _state_path(self, p) -> str:
        p = Path(p)
        return str(p if p.is_absolute() or p.parent != Path(".") else repo_root() / "build" / "states" / p)

    def _screenshot(self, path=None, border=True, scale=1, layer="screen"):
        path = self._rel(path) if path else None
        img = self.emu.screenshot(path, border, scale)
        buf = io.BytesIO()
        img.save(buf, "PNG")
        return {"png_b64": base64.b64encode(buf.getvalue()).decode(), "width": img.width,
                "height": img.height, "path": path, "colors": len(img.getcolors(1 << 16) or [])}

    def _read_mem(self, addr, length=64, bank=None):
        a = self.emu.addr(addr) if bank is None or not isinstance(addr, int) else addr
        data = self.emu.read_mem(a, int(length), bank=bank)
        return {"addr": a, "bank": bank, "hex": data.hex(), "dump": _hexdump(a, data)}

    def _write_mem(self, addr, hex, bank=None):
        data = bytes.fromhex(hex.replace(" ", ""))
        a = self.emu.addr(addr) if bank is None or not isinstance(addr, int) else addr
        self.emu.write_mem(a, data, bank=bank)
        return {"addr": a, "bank": bank, "written": len(data)}

    def _find_mem(self, hex=None, text=None, start=0, end=0x10000):
        needle = bytes.fromhex(hex.replace(" ", "")) if hex else text.encode("latin-1")
        return {"matches": [{"addr": a, "symbol": self.emu.symbols.name_of(a)}
                            for a in self.emu.find_mem(needle, self.emu.addr(start), int(end))]}

    def _quit(self):
        self.quit.set()
        return {"bye": True}

    def call(self, cmd: str, args: dict):
        table = self.commands()
        if cmd not in table:
            raise ValueError(f"unknown command {cmd!r} (try help)")
        if cmd == "reboot" and not self.lock.acquire(timeout=3):
            # another command is stuck inside the emulator: kill it so that
            # command fails and lets go of the lock, then restart
            try:
                self.emu.proc.kill()
            except OSError:
                pass
            self.lock.acquire()
        elif cmd != "reboot":
            self.lock.acquire()
        try:
            try:
                return table[cmd][0](**(args or {}))
            finally:
                # keep the registry current (the emulator may restart, e.g. reboot)
                now = {"emu_port": self.emu.port, "machine": self.emu.machine,
                       "emulator_pid": self.emu.proc.pid, "program": self.emu.program}
                if any(self.info.get(k) != v for k, v in now.items()):
                    self.info.update(now)
                    if self.registry:
                        self.registry.write_text(json.dumps(self.info))
        finally:
            self.lock.release()


class _Handler(socketserver.StreamRequestHandler):
    def handle(self):
        sess: Session = self.server.session
        for raw in self.rfile:
            try:
                req = json.loads(raw)
            except json.JSONDecodeError as ex:
                self._send({"ok": False, "error": f"bad json: {ex}"})
                continue
            rid = req.get("id")
            try:
                res = sess.call(req.get("cmd", ""), req.get("args") or {})
                self._send({"id": rid, "ok": True, "result": res})
            except (EmulatorError, ZrcpError, ValueError, TypeError, KeyError, FileNotFoundError) as ex:
                self._send({"id": rid, "ok": False, "error": f"{type(ex).__name__}: {ex}"})
            if sess.quit.is_set():
                return

    def _send(self, obj):
        self.wfile.write((json.dumps(obj) + "\n").encode())
        self.wfile.flush()


class _Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(program: str | None = None, machine: str | None = None, window: bool = False,
          mode: str | None = None, port: int = 0, zoom: int | None = None, quiet: bool = False):
    paused = (mode or ("running" if window else "paused")) == "paused"
    SESSIONS_DIR.mkdir(parents=True, exist_ok=True)
    for old in sorted(SESSIONS_DIR.glob("atari800-*.log"), key=lambda p: p.stat().st_mtime)[:-20]:
        old.unlink(missing_ok=True)   # keep the 20 newest emulator logs
    kw = {"machine": machine} if machine else {}
    emu = Emulator(window=window, paused=True, zoom=zoom,
                   log_path=SESSIONS_DIR / f"atari800-{os.getpid()}.log", **kw)
    if program:
        emu.load(program)
    if not paused:
        emu.resume()
    srv = _Server(("127.0.0.1", port), _Handler)
    info = {"pid": os.getpid(), "control_port": srv.server_address[1], "emu_port": emu.port,
            "program": program, "machine": emu.machine, "window": window,
            "mode": "paused" if paused else "running", "started": time.time(),
            "emulator_pid": emu.proc.pid}
    sess = Session(emu, info)
    srv.session = sess
    reg = SESSIONS_DIR / f"{os.getpid()}.json"
    reg.write_text(json.dumps(info))
    sess.registry = reg
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("A8DEV-SESSION " + json.dumps(info), flush=True)
    if not quiet:
        print(f"session pid {info['pid']}: control :{info['control_port']}  emulator :{info['emu_port']}  "
              f"({'window' if window else 'headless'}, {info['mode']})", file=sys.stderr, flush=True)

    def _stop(*_):
        sess.quit.set()
    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    try:
        # exit when told to, or when the emulator goes away (window closed)
        while not sess.quit.wait(0.5):
            # (under the lock: reboot restarts the emulator inside a command)
            if sess.lock.acquire(blocking=False):
                gone = not emu.alive
                sess.lock.release()
                if gone:
                    break
    finally:
        reg.unlink(missing_ok=True)
        srv.shutdown()
        with sess.lock:
            emu.close()
