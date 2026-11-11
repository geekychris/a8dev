"""Drive the patched Atari800 (the a8h harness, emulator/ext/a8h.c).

    from a8dev.emu import Emulator
    with Emulator(machine="130xe") as emu:          # headless, paused, deterministic
        emu.load("projects/hello/build/hello.xex")
        assert emu.run_until("hello: ready", 600)
        emu.press("SPACE")
        emu.break_at("_main"); print(emu.continue_(50))
        emu.screenshot("shot.png")

The emulator speaks a line protocol on a TCP port (127.0.0.1, chosen by the
emulator and printed on stdout): `cmd key=value ...` -> `OK key=value ...` or
`ERR message`. Emulator.raw("help") lists the commands; docs/emulator-harness.md
describes them.

Headless sessions are paused: frames advance only on run/run_until/press/
continue/stepi, as fast as the host allows, and identically every time.
Window sessions free-run in real time; run/run_until fast-forward them
(the window keeps showing frames, the user's input keeps working) and then
let them go again. A breakpoint stops a free-running window too.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path

from .symbols import Symbols
from . import cpu6502

# ---------------------------------------------------------------- machines

MACHINES = {
    "130xe": "130XE, 128K (4 extended 16K banks at $4000 via PORTB)",
    "800xl": "800XL, 64K",
    "65xe": "65XE, 64K",
    "600xl": "600XL, 16K",
    "1200xl": "1200XL, 64K (no BASIC)",
    "800": "800, 48K (OS-B era machine, AltirraOS 800)",
    "400": "400, 16K",
    "320xe": "320K Compy-Shop XE (16 extended banks)",
}
MACHINE_ALIASES = {"xe": "130xe", "xl": "800xl", "130": "130xe", "800x": "800xl", "a800": "800",
                   "atari800": "800", "atari400": "400", "65": "65xe"}
LOADABLE = (".xex", ".com", ".exe", ".obx", ".atr", ".xfd", ".atx", ".dcm", ".pro", ".car", ".rom",
            ".bin", ".bas", ".lst", ".cas", ".a8s")
STATE_EXT = ".a8s"


def repo_root() -> Path:
    env = os.environ.get("A8_ROOT")
    if env:
        return Path(env)
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / "a8").exists() and (p / "emulator").is_dir():
            return p
    return here.parents[4]


def emulator_binary(window: bool = False) -> Path:
    env = os.environ.get("A8_EMULATOR_WINDOW" if window else "A8_EMULATOR")
    if env:
        return Path(env)
    return repo_root() / "third_party" / "atari800" / "build" / ("atari800-a8h-sdl" if window else "atari800-a8h")


def machine_id(name: str) -> str:
    n = str(name).strip().lower().replace(" ", "")
    n = MACHINE_ALIASES.get(n, n)
    if n not in MACHINES:
        raise EmulatorError(f"unknown machine {name!r}: {', '.join(MACHINES)} "
                            "(xegs is not available: its built-in game ROM is not free)")
    return n


class EmulatorError(RuntimeError):
    pass


class ProtocolError(EmulatorError):
    """The emulator answered ERR (or the link broke)."""


# ---------------------------------------------------------------- the line protocol

_KV = re.compile(r'(\w[\w.]*)=("(?:[^"\\]|\\.)*"|\S*)')


def parse_reply(line: str) -> dict:
    """'OK a=1 b="x y"' -> {'a': '1', 'b': 'x y'} (values stay strings)."""
    out = {}
    for k, v in _KV.findall(line):
        if v.startswith('"'):
            v = re.sub(r'\\(.)', lambda m: "\n" if m.group(1) == "n" else m.group(1), v[1:-1])
        out[k] = v
    return out


def _q(v) -> str:
    """Quote an argument value for the protocol."""
    s = str(v)
    if s == "" or any(c in s for c in ' "\\\n'):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
    return s


class Link:
    def __init__(self, port: int, timeout: float = 8.0):
        t0 = time.monotonic()
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError as ex:
                if time.monotonic() - t0 > timeout:
                    raise ProtocolError(f"cannot connect to the emulator on :{port}: {ex}") from None
                time.sleep(0.05)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    def cmd(self, line: str, timeout: float = 60.0) -> str:
        self.sock.settimeout(timeout)
        try:
            self.sock.sendall(line.encode("latin-1") + b"\n")
            while b"\n" not in self.buf:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    raise ProtocolError("the emulator closed the connection")
                self.buf += chunk
        except socket.timeout:
            raise ProtocolError(f"no answer to {line.split()[0]!r} within {timeout:.0f}s") from None
        except OSError as ex:
            raise ProtocolError(f"emulator link: {ex}") from None
        reply, self.buf = self.buf.split(b"\n", 1)
        text = reply.decode("latin-1")
        if text.startswith("ERR"):
            raise ProtocolError(text[4:])
        return text

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- keys

# POKEY keyboard codes (Atari800 akey.h): unshifted; | 0x40 shift, | 0x80 control
KEYCODES = {
    **{c: v for c, v in {
        "A": 0x3F, "B": 0x15, "C": 0x12, "D": 0x3A, "E": 0x2A, "F": 0x38, "G": 0x3D, "H": 0x39,
        "I": 0x0D, "J": 0x01, "K": 0x05, "L": 0x00, "M": 0x25, "N": 0x23, "O": 0x08, "P": 0x0A,
        "Q": 0x2F, "R": 0x28, "S": 0x3E, "T": 0x2D, "U": 0x0B, "V": 0x10, "W": 0x2E, "X": 0x16,
        "Y": 0x2B, "Z": 0x17,
        "0": 0x32, "1": 0x1F, "2": 0x1E, "3": 0x1A, "4": 0x18, "5": 0x1D, "6": 0x1B, "7": 0x33,
        "8": 0x35, "9": 0x30}.items()},
    "SPACE": 0x21, "RETURN": 0x0C, "ENTER": 0x0C, "ESC": 0x1C, "ESCAPE": 0x1C, "TAB": 0x2C,
    "DELETE": 0x34, "BACKSPACE": 0x34, "HELP": 0x11, "CAPS": 0x3C, "INVERSE": 0x27, "ATARI": 0x27,
    "F1": 0x03, "F2": 0x04, "F3": 0x13, "F4": 0x14,
    "COMMA": 0x20, "PERIOD": 0x22, "FULLSTOP": 0x22, "SLASH": 0x26, "SEMICOLON": 0x02,
    "MINUS": 0x0E, "EQUAL": 0x0F, "PLUS": 0x06, "ASTERISK": 0x07, "LESS": 0x36, "GREATER": 0x37,
    "CURSOR_UP": 0x8E, "CURSOR_DOWN": 0x8F, "CURSOR_LEFT": 0x86, "CURSOR_RIGHT": 0x87,
    "CLEAR": 0x76, "INSERT": 0xB7,
    "BREAK": -5,
}
CONSOLE = {"START": 1, "SELECT": 2, "OPTION": 4}
STICK = {"UP": 1, "DOWN": 2, "LEFT": 4, "RIGHT": 8}
MODIFIERS = ("SHIFT", "CTRL")

# characters for type(): (code incl. 0x40 shift bit)
CHARCODES = {" ": 0x21, "\n": 0x0C, "!": 0x5F, '"': 0x5E, "#": 0x5A, "$": 0x58, "%": 0x5D,
             "&": 0x5B, "'": 0x73, "@": 0x75, "(": 0x70, ")": 0x72, "<": 0x36, ">": 0x37,
             "=": 0x0F, "?": 0x66, "-": 0x0E, "+": 0x06, "*": 0x07, "/": 0x26, ":": 0x42,
             ";": 0x02, ",": 0x20, ".": 0x22, "_": 0x4E, "[": 0x60, "]": 0x62, "^": 0x47,
             "\\": 0x46, "|": 0x4F, "\t": 0x2C, "\b": 0x34}


def parse_keys(keys) -> list[str]:
    """'SHIFT+A' / 'UP+FIRE' / ['Q', 'SPACE'] -> normalised names."""
    if keys is None:
        return []
    if isinstance(keys, str):
        parts = [p for p in re.split(r"\s*\+\s*|\s*,\s*", keys.strip()) if p]
    else:
        parts = [str(k) for k in keys]
    out = []
    for p in parts:
        n = p.strip().upper()
        n = {"CONTROL": "CTRL", "FIRE0": "FIRE", "TRIG": "FIRE", "BUTTON": "FIRE", "ENTER": "RETURN"}.get(n, n)
        base = n[:-1] if n[-1:] in "0123" and n[:-1] in ("UP", "DOWN", "LEFT", "RIGHT", "FIRE") else n
        if not (n in KEYCODES or n in CONSOLE or n in MODIFIERS or base in STICK or base == "FIRE"):
            raise ValueError(f"unknown key {p!r} (A-Z 0-9 SPACE RETURN ESC TAB DELETE HELP BREAK SHIFT CTRL "
                             f"START SELECT OPTION UP DOWN LEFT RIGHT FIRE, UP1.. for stick 1, "
                             f"CURSOR_UP.., COMMA PERIOD SLASH SEMICOLON MINUS EQUAL PLUS ASTERISK LESS GREATER)")
        out.append(n)
    return out


def input_state(names: list[str]) -> dict:
    """Held key names -> the harness input registers (key, shift, consol, port0, port1, trig)."""
    key, shift, ctrl, consol, trig = -1, 0, 0, 0, 0
    sticks = [0, 0, 0, 0]
    for n in names:
        if n == "SHIFT":
            shift = 1
        elif n == "CTRL":
            ctrl = 1
        elif n in CONSOLE:
            consol |= CONSOLE[n]
        elif n in KEYCODES:
            key = KEYCODES[n]
        else:
            num = int(n[-1]) if n[-1] in "0123" else 0
            base = n[:-1] if n[-1] in "0123" else n
            if base == "FIRE":
                trig |= 1 << num
            else:
                sticks[num] |= STICK[base]
    if key >= 0:
        if shift:
            key |= 0x40
        if ctrl:
            key |= 0x80
    nib = [0xF & ~s for s in sticks]
    return {"key": key, "shift": shift, "consol": consol, "port0": nib[0] | nib[1] << 4,
            "port1": nib[2] | nib[3] << 4, "trig": trig}


def chord_for_char(ch: str):
    """A character for type(): a key code (with the shift bit) or None."""
    if ch.isalpha() and ch.isascii():
        return KEYCODES[ch.upper()]
    if ch.isdigit():
        return KEYCODES[ch]
    return CHARCODES.get(ch)


# ---------------------------------------------------------------- the emulator

class Emulator:
    def __init__(self, machine: str = "130xe", window: bool = False, paused: bool | None = None,
                 tv: str = "pal", zoom: int | None = None, audio: bool | None = None, seed: int = 0,
                 extra_args=(), log_path=None, port: int | None = None, basic: bool = False):
        self.machine = machine_id(machine)
        self.tv = tv.lower()
        if self.tv not in ("pal", "ntsc"):
            raise EmulatorError("tv: pal or ntsc")
        self.window = window
        self.paused = (not window) if paused is None else paused
        self.basic = basic
        self.program: str | None = None
        self.load_frame = 0
        self.symbols = Symbols()
        self.mark = 0
        self._logtext = bytearray()
        self._logbase = 0
        self._held: list[str] = []
        self._inputs: list[dict] = []
        self._checkpoints: list[dict] = []
        self._seq = 0
        self.last_stop: dict | None = None
        self.last_run: dict = {}
        self._bps: dict[int, dict] = {}
        self._watches: dict[int, dict] = {}
        self._next_id = 1
        self._recording: dict | None = None
        self._tmp = Path(tempfile.mkdtemp(prefix="a8dev-"))
        self.log_path = Path(log_path) if log_path else self._tmp / "atari800.log"
        self.link: Link | None = None
        self.port = 0
        self.proc = None
        self._opts = {"zoom": zoom, "audio": audio, "seed": seed, "extra": list(extra_args)}
        self._start()

    # ---------------------------------------------------- process

    def _args(self) -> list[str]:
        exe = emulator_binary(self.window)
        if not exe.exists():
            raise EmulatorError(f"emulator not built: {exe} (run scripts/build-emulator.sh)")
        cfg = self._tmp / "atari800.cfg"
        cfg.write_text("")               # never the user's ~/.atari800.cfg
        o = self._opts
        args = [str(exe), "-config", str(cfg), "-a8h-port", "0", "-a8h-machine", self.machine,
                "-a8h-seed", str(int(o["seed"])), "-" + self.tv]
        args.append("-a8h-paused" if self.paused else "-a8h-free")
        if self.window:
            z = int(o["zoom"] or 2)
            args += ["-windowed", "-win-width", str(336 * z), "-win-height", str(240 * z)]
            if o["audio"] is False:
                args.append("-nosound")
        else:
            args.append("-nosound")
        return args + o["extra"]

    def _start(self):
        args = self._args()
        out = open(self.log_path, "ab")
        start = out.tell()
        self.proc = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                     start_new_session=True, env={**os.environ, "A8H_PARENT_WATCH": "1"})
        out.close()
        t0 = time.monotonic()
        port = None
        while time.monotonic() - t0 < 15:
            text = self.log_path.read_bytes()[start:].decode("latin-1", "replace")
            m = re.search(r"A8H port=(\d+) pid=(\d+)", text)
            if m:
                port = int(m.group(1))
                if int(m.group(2)) != self.proc.pid:
                    self._kill()
                    raise EmulatorError("the emulator reported another pid")
                break
            if self.proc.poll() is not None:
                break
            time.sleep(0.02)
        if port is None:
            tail = self.log_path.read_bytes()[start:][-1500:].decode("latin-1", "replace")
            code = self.proc.poll()
            self._kill()
            raise EmulatorError(f"the emulator did not start (exit code {code}). Log:\n{tail}")
        self.port = port
        self.link = Link(port)
        st = self._kv("status")
        if st.get("pid") != str(self.proc.pid):
            self.link.close()
            self.link = None
            self._kill()
            raise EmulatorError(f"port {port} answered for pid {st.get('pid')}, not ours ({self.proc.pid})")
        self.mark = int(st["log"])
        if self.basic:
            self._cmd(f"machine basic=1")

    def _kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass

    def _stop_process(self):
        if self.link is not None:
            try:
                self.link.sock.sendall(b"quit\n")
            except OSError:
                pass
            self.link.close()
            self.link = None
        if self.proc is None:
            return
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    @property
    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def close(self):
        self._stop_process()
        shutil.rmtree(self._tmp, ignore_errors=True)

    def _cmd(self, line: str, timeout: float = 60.0) -> str:
        if self.link is None or not self.alive:
            raise EmulatorError(f"emulator has exited (log: {self.log_path})")
        return self.link.cmd(line, timeout=timeout)

    def _kv(self, line: str, timeout: float = 60.0) -> dict:
        return parse_reply(self._cmd(line, timeout))

    def raw(self, line: str, timeout: float = 30.0) -> str:
        """A raw harness-protocol command (raw('help') lists them)."""
        return self._cmd(line, timeout=timeout)

    @contextmanager
    def held(self):
        """Pause a free-running session for the duration (deterministic steps)."""
        if self.paused:
            yield
            return
        self._cmd("pause")
        try:
            yield
        finally:
            self._cmd("resume")

    def pause(self):
        """Stop free-running (frames then only advance on run/press/continue)."""
        self.paused = True
        self._cmd("pause")

    def resume(self, turbo: bool = False):
        """Free-run in real time (turbo=True: as fast as possible)."""
        self.paused = False
        self._cmd(f"resume turbo={1 if turbo else 0}")

    # ---------------------------------------------------- programs

    def load(self, path, machine: str | None = None) -> dict:
        """Power on (RAM cleared, registers fixed, RANDOM seeded) and run a program:
        .xex/.com (loaded directly), .atr/.xfd/.atx (booted from D1:), .car/.rom,
        .bas (Altirra BASIC), or a state (.a8s). Symbols come from the .lbl/.map
        beside it."""
        p = Path(path).resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        if p.suffix.lower() == STATE_EXT:
            return self.load_state(p)
        if machine:
            self.set_machine(machine)
        basic = 1 if (self.basic or p.suffix.lower() in (".bas", ".lst")) else 0
        self._cmd(f"load path={_q(p)} basic={basic}")
        self.program = str(p)
        self.symbols = Symbols.load(p)
        self._after_reset()
        return self.status()

    def _after_reset(self):
        self.load_frame = 0
        self._inputs = []
        self._checkpoints = []
        self._held = []
        self._sync_log()
        self.mark = self._logbase + len(self._logtext)
        self._push_input(log=False)
        # breakpoints survive a load (re-armed in the emulator: ids are ours)

    def set_machine(self, name: str) -> dict:
        """Switch machine (power-on; the program is not reloaded)."""
        self.machine = machine_id(name)
        self._cmd(f"machine name={self.machine} tv={self.tv}")
        self.program = None
        self.symbols = Symbols()
        self._after_reset()
        return self.status()

    def set_tv(self, tv: str) -> dict:
        self.tv = tv.lower()
        self._cmd(f"machine tv={self.tv}")
        self.program = None
        self._after_reset()
        return self.status()

    def reset(self, hard: bool = True):
        """hard: power-on (RAM cleared, drives emptied, program gone); soft: the RESET key."""
        self._cmd(f"reset hard={1 if hard else 0}")
        if hard:
            self.program = None
            self._after_reset()

    def reload(self) -> dict:
        if not self.program:
            raise EmulatorError("no program loaded")
        return self.load(self.program)

    def power_cycle(self) -> dict:
        """A fresh emulator process with the same machine (no program)."""
        paused = self.paused
        self._stop_process()
        self.paused = True
        self._logtext, self._logbase = bytearray(), 0
        self._bps.clear()
        self._watches.clear()
        self._held = []
        self.program = None
        self.symbols = Symbols()
        self.load_frame = 0
        self._start()
        if not paused:
            self.resume()
        return self.status()

    def reboot(self) -> dict:
        """Recover a wedged emulator: kill it, start a fresh one, reload the program."""
        prog = self.program
        bps, watches = dict(self._bps), dict(self._watches)
        try:
            self.proc.kill()
        except OSError:
            pass
        paused = self.paused
        self.paused = True
        self.power_cycle()
        for b in bps.values():
            self._arm(b)
        for w in watches.values():
            self._arm(w)
        if prog:
            self.load(prog)
        if not paused:
            self.resume()
        return self.status()

    # ---------------------------------------------------- running

    _WHY = {"break": "breakpoint", "watch": "watchpoint", "step": "step", "frames": "frames",
            "frame_limit": "frame_limit", "log": "log_match", "at": "position", "to": "breakpoint",
            "signal": "signal", "cim": "cpu_jam"}

    def _stop_info(self, text: str) -> dict | None:
        """The emulator's last_stop (status) as a dict, without side effects."""
        r = parse_reply(text)
        if not r.get("why") or r["why"] == "none":
            return None
        pc = int(r.get("pc", "0"), 16)
        out = {"why": self._WHY.get(r["why"], r["why"]), "reason": r["why"], "pc": pc,
               "symbol": self.symbols.name_of(pc), "frame": int(r.get("frame", 0)),
               "since_load": int(r.get("frame", 0)) - self.load_frame, "cycles": int(r.get("cycles", 0)),
               "midframe": r.get("midframe") == "1"}
        if "line" in r:
            out["line"], out["xpos"] = int(r["line"]), int(r["xpos"])
        if "id" in r:
            i = int(r["id"])
            out["break"] = (self._bps.get(i) or self._watches.get(i) or {}).get("desc")
        return out

    def _result(self, r: dict) -> dict:
        res = {"reason": r.get("why"), "frames": int(r.get("ran", 0)), "frame": int(r.get("frame", 0)),
               "cycles": int(r.get("cycles", 0)), "pc": int(r.get("pc", "0"), 16),
               "midframe": r.get("midframe") == "1"}
        res["since_load"] = res["frame"] - self.load_frame
        res["why"] = self._WHY.get(res["reason"], res["reason"])
        if "line" in r:
            res["line"], res["xpos"] = int(r["line"]), int(r["xpos"])
        for k in ("id", "cond", "addr", "access", "insn_pc", "value", "old", "match", "signal", "opcode"):
            if k in r:
                res[k] = r[k]
        if "match" in res:
            res["match"] = int(res["match"])
        if "id" in res:
            i = int(res["id"])
            res["id"] = i
            res["break"] = (self._bps.get(i) or self._watches.get(i) or {}).get("desc")
        if r.get("fps"):
            res["fps"] = float(r["fps"])
            res["speed"] = float(r.get("speed", 0))
        res["regs"] = {"A": int(r.get("a", "0"), 16), "X": int(r.get("x", "0"), 16), "Y": int(r.get("y", "0"), 16),
                       "S": int(r.get("s", "0"), 16), "P": int(r.get("p", "0"), 16), "PC": res["pc"]}
        res["symbol"] = self.symbols.name_of(res["pc"])
        self.last_stop = {k: res.get(k) for k in ("why", "reason", "pc", "symbol", "break", "frame",
                                                   "since_load", "cycles", "line", "xpos", "midframe")}
        if res["why"] in ("breakpoint", "watchpoint", "cpu_jam"):
            self.paused = True        # a stop holds a free-running window too
        return res

    def _run(self, frames: int, timeout: float | None = None, **kw) -> dict:
        frames = max(1, int(frames))
        line = f"run frames={frames}" + "".join(f" {k}={_q(v)}" for k, v in kw.items() if v is not None)
        r = parse_reply(self._cmd(line, timeout=timeout or (30 + frames / 20)))
        return self._result(r)

    def run(self, frames: int = 1) -> dict:
        """Advance `frames` frames (stops early at a breakpoint: see ['why']).
        In a free-running window this fast-forwards, then real time resumes."""
        return self._run(frames)

    def run_until(self, text: str, max_frames: int = 3000, since: int | None = None) -> bool:
        """Run until the guest log contains `text` (written after `since`, default:
        after the last log()/reset/load/previous match). Stops on the instruction
        after the one that wrote its last byte. False if max_frames pass first
        (or a breakpoint stops it: see last_run). Text already written after
        `since` (e.g. during a press()) matches without running."""
        since = self.mark if since is None else since
        pat = text.encode("latin-1")
        self._sync_log()
        start = max(0, since - self._logbase)
        i = self._logtext.find(pat, start)
        if i >= 0:
            pos = self.position()
            self.mark = self._logbase + i + len(pat)
            self.last_run = {"reason": "log", "why": "log_match", "frames": 0, "already": True,
                             "match": self._logbase + i, **pos}
            return True
        # a match may straddle what is already there and what comes next
        since = max(since, self._logbase + len(self._logtext) - len(pat) + 1)
        r = self._run(max_frames, until=pat.hex(), since=since)
        self.last_run = r
        if r["reason"] == "log":
            self.mark = r["match"] + len(text.encode("latin-1"))
            return True
        return False

    def run_to(self, since_load: int, tstates: int = 0) -> dict:
        """Run to frame `since_load` (frames since load) and `tstates` machine
        cycles into it (0: the frame boundary). Breakpoints still stop it."""
        pos = self.position()
        target = int(since_load) * self.cycles_per_frame + int(tstates)
        if pos["cycles"] >= target:
            return {"reason": "at", "frames": 0, **pos}
        if tstates == 0 and not pos["midframe"]:
            return self._run(int(since_load) - pos["since_load"])
        return self._run(int(since_load) - pos["since_load"] + 2, at=target)

    @property
    def cycles_per_frame(self) -> int:
        return (312 if self.tv == "pal" else 262) * 114

    def continue_(self, max_frames: int = 500) -> dict:
        """Run until a breakpoint/watchpoint fires (or max_frames pass)."""
        r = self._run(max_frames)
        r["stopped"] = r["why"] in ("breakpoint", "watchpoint", "cpu_jam")
        return r

    def stepi(self, count: int = 1) -> dict:
        """Execute `count` instructions."""
        r = self._run(1 + count // 2000, steps=max(1, int(count)))
        return {**r, "pc_symbol": r["symbol"], "next": self.disasm("pc", 1)}

    def step_over(self) -> dict:
        """One instruction; a JSR runs until it returns."""
        r = self._run(200, over=1)
        return {**r, "pc_symbol": r["symbol"], "next": self.disasm("pc", 1)}

    def run_to_addr(self, addr, max_frames: int = 500) -> dict:
        return self._run(max_frames, to=self.addr(addr))

    def run_until_signal(self, max_frames: int = 3000) -> dict:
        """Run until the program writes a byte to $D6FE (an exit code)."""
        return self._run(max_frames, until_signal=1)

    def position(self) -> dict:
        r = self._kv("status")
        f = int(r["frame"])
        cyc = int(r["cycles"])
        return {"frame": f, "since_load": f - self.load_frame, "cycles": cyc,
                "tstates": cyc - f * self.cycles_per_frame if r.get("midframe") == "1" else 0,
                "midframe": r.get("midframe") == "1"}

    def frame(self) -> int:
        return int(self._kv("status")["frame"])

    def status(self) -> dict:
        r = self._kv("status")
        pc = int(r["pc"], 16)
        mode = r.get("mode")
        if mode == "paused":
            self.paused = True
        return {"machine": self.machine, "tv": self.tv, "mode": mode, "frame": int(r["frame"]),
                "since_load": int(r["frame"]) - self.load_frame, "cycles": int(r["cycles"]),
                "midframe": r.get("midframe") == "1", "pc": pc, "pc_symbol": self.symbols.name_of(pc),
                "paused": self.paused, "window": self.window, "program": self.program,
                "log_end": int(r["log"]), "symbols": len(self.symbols), "emu_port": self.port,
                "breakpoints": len(self._bps), "watchpoints": len(self._watches),
                "signal": int(r["signal"]), "signals": int(r["signals"]), "fps": float(r.get("fps", 0)),
                "inputs_logged": len(self._inputs), "last_stop": self._stop_info(r.get("last_stop", ""))}

    # ---------------------------------------------------- guest log

    def log_end(self) -> int:
        return int(self._kv("status")["log"])

    def log_cursor(self) -> int:
        """Absolute offset of the end of the log (pass as since= to run_until)."""
        return self.log_end()

    def _sync_log(self):
        end = self._logbase + len(self._logtext)
        while True:
            r = self._kv(f"log since={end} max=1000000")
            base, nxt = int(r["base"]), int(r["next"])
            if int(r["since"]) > end:            # the ring moved past what we had
                self._logtext, self._logbase = bytearray(), int(r["since"])
            self._logtext += bytes.fromhex(r["data"])
            end = nxt
            if nxt >= int(r["end"]):
                break
        if len(self._logtext) > 8 << 20:
            cut = len(self._logtext) - (4 << 20)
            del self._logtext[:cut]
            self._logbase += cut

    def log_text(self, since: int | None = None) -> str:
        self._sync_log()
        start = max(0, (self.mark if since is None else since) - self._logbase)
        return self._logtext[start:].decode("latin-1")

    def log(self, since: int | None = None) -> str:
        """Guest log text written since `since` (default: since the last log()
        call / reset / load / match); moves the mark to the end."""
        t = self.log_text(since)
        self.mark = self._logbase + len(self._logtext)
        return t

    @property
    def debug_log(self) -> list[str]:
        self._sync_log()
        return self._logtext.decode("latin-1").splitlines()

    # ---------------------------------------------------- input

    def _push_input(self, log: bool = True, state: dict | None = None):
        st = state or input_state(self._held)
        self._cmd("input " + " ".join(f"{k}={v}" for k, v in st.items()))
        if log:
            pos = self.position()
            self._seq += 1
            self._inputs.append({"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
                                 "keys": list(self._held), "state": st})

    def hold(self, keys):
        """Press and keep holding keys / console keys / joystick ('Q', 'START', 'UP+FIRE')."""
        for k in parse_keys(keys):
            if k not in self._held:
                self._held.append(k)
        self._push_input()

    def release(self, keys=None):
        """Release some (or all) held keys."""
        drop = parse_keys(keys) if keys is not None else None
        self._held = [] if drop is None else [k for k in self._held if k not in drop]
        self._push_input()

    def press(self, keys, hold_frames: int = 3, release_frames: int = 3) -> dict:
        """Press keys together for hold_frames, release, run release_frames.
        Atari800 samples the keyboard, sticks and console keys once per frame."""
        ks = parse_keys(keys)
        saved = list(self._held)
        self._held = saved + [k for k in ks if k not in saved]
        self._push_input()
        r = self.run(hold_frames)
        self._held = saved
        self._push_input()
        if release_frames and r.get("why") not in ("breakpoint", "watchpoint", "cpu_jam"):
            r = self.run(release_frames)
        return r

    def type(self, text: str, hold_frames: int = 3, gap_frames: int = 3) -> dict:
        """Type text key by key. Letters use the plain key (the OS starts in caps
        mode, so they come out upper case), \\n = RETURN."""
        r = {}
        for ch in text:
            code = chord_for_char(ch)
            if code is None:
                raise ValueError(f"cannot type {ch!r}")
            st = input_state(self._held)
            st.update(key=code, shift=1 if code & 0x40 else st["shift"])
            self._push_input(state=st)
            r = self.run(hold_frames)
            self._push_input()
            if gap_frames:
                r = self.run(gap_frames)
            if r.get("why") in ("breakpoint", "watchpoint", "cpu_jam"):
                break
        return r

    def joystick(self, dirs=None, port: int = 0) -> str:
        """Set stick `port` (0-3) to e.g. 'UP+FIRE' ('' or None = centre, released)."""
        sfx = "" if port == 0 else str(port)
        mine = {f"{d}{sfx}" for d in ("UP", "DOWN", "LEFT", "RIGHT", "FIRE")}
        self._held = [k for k in self._held if k not in mine]
        for d in parse_keys(dirs or ""):
            d = d.rstrip("0123")
            if d not in STICK and d != "FIRE":
                raise ValueError(f"{d!r} is not a joystick direction (UP DOWN LEFT RIGHT FIRE)")
            self._held.append(d + sfx)
        self._push_input()
        return "+".join(k.rstrip("0123") for k in self._held if k in mine) or "centre"

    def console(self, keys=None):
        """Hold console keys ('START', 'SELECT+OPTION'; None = release all three)."""
        self._held = [k for k in self._held if k not in CONSOLE]
        self._held += parse_keys(keys or "")
        self._push_input()

    def sequence(self, steps) -> list:
        """[{"press": "Q", "hold": 3}, {"run": 20}, {"until": "text", "max": 500}, {"hold": "UP"},
        {"release": "UP"}, {"type": "HELLO"}, {"joystick": "LEFT+FIRE", "port": 0}].
        Any step may carry "at": N (frames since load) to run there first."""
        out = []
        for st in steps:
            if "at" in st:
                self.run_to(int(st["at"]), int(st.get("t", 0)))
            if "press" in st:
                out.append(self.press(st["press"], st.get("hold", 3), st.get("release", 3)))
            elif "run" in st:
                out.append(self.run(st["run"]))
            elif "until" in st:
                out.append(self.run_until(st["until"], st.get("max", 3000)))
            elif "type" in st:
                out.append(self.type(st["type"]))
            elif "joystick" in st:
                out.append(self.joystick(st["joystick"], int(st.get("port", 0))))
            elif "hold" in st:
                self.hold(st["hold"])
                out.append(True)
            elif "release" in st:
                self.release(st["release"] or None)
                out.append(True)
            elif "at" in st:
                out.append(self.position())
            else:
                raise ValueError(f"bad step {st!r}")
        return out

    # ---------------------------------------------------- screen

    _palette_cache: list | None = None

    def palette(self) -> bytes:
        if self._palette_cache is None:
            self._palette_cache = bytes.fromhex(self._kv("palette")["data"])
        return self._palette_cache

    def frame_buffer(self) -> tuple[bytes, dict]:
        """The raw 384x240 frame buffer (palette indices) and its geometry."""
        r = self._kv("screen")
        return bytes.fromhex(r["data"]), {k: int(r[k]) for k in ("w", "h", "vx1", "vy1", "vx2", "vy2")}

    def screenshot(self, path=None, border: bool | str = True, scale: int = 1, layer: str = "screen"):
        """The displayed frame as a PIL RGB image.
        border=True: what a TV shows, 336x240 (the playfield's 320x192 at 8,24);
        border=False: the 320x192 playfield only; border="full": Atari800's whole
        384x240 buffer (overscan included)."""
        from PIL import Image
        if layer not in ("screen",):
            raise EmulatorError("layer: screen")
        data, g = self.frame_buffer()
        img = Image.frombytes("P", (g["w"], g["h"]), data)
        img.putpalette(self.palette())
        img = img.convert("RGB")
        if border == "full":
            pass
        elif border:
            img = img.crop((g["vx1"], g["vy1"], g["vx2"], g["vy2"]))
        else:
            img = img.crop((32, 24, 352, 216))
        if scale and scale != 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            img.save(path)
        return img

    def screen_hash(self, layer: str = "screen") -> str:
        """md5 of the visible frame's RGB pixels."""
        return hashlib.md5(self.screenshot(layer=layer).tobytes()).hexdigest()

    def frame_hash(self) -> str:
        """The emulator's FNV-1a hash of the whole frame buffer (what recordings compare)."""
        return self._kv("screen_hash")["hash"]

    def record_video(self, path, frames: int = 100, every: int = 1, scale: int = 1,
                     layer: str = "screen", border=True) -> dict:
        """Record `frames` frames (one picture every `every`) to .gif, .mp4 or a
        directory of PNGs. Paused sessions advance deterministically."""
        p = Path(path)
        shots = []
        t0 = time.monotonic()
        for i in range(0, max(1, frames), max(1, every)):
            if i:
                self.run(every)
            shots.append(self.screenshot(scale=scale, layer=layer, border=border))
        fps = (50 if self.tv == "pal" else 60) / max(1, every)
        if p.suffix.lower() == ".gif":
            p.parent.mkdir(parents=True, exist_ok=True)
            shots[0].save(p, save_all=True, append_images=shots[1:], loop=0,
                          duration=int(round(1000 / fps)), optimize=False)
        elif p.suffix.lower() in (".mp4", ".mov", ".webm"):
            ff = shutil.which("ffmpeg")
            if not ff:
                raise EmulatorError("ffmpeg not found: use a .gif path, or install ffmpeg")
            tmp = self._tmp / "video"
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir()
            for i, im in enumerate(shots):
                im.save(tmp / f"{i:05d}.png")
            p.parent.mkdir(parents=True, exist_ok=True)
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", f"{fps:g}",
                                "-i", str(tmp / "%05d.png"), "-vf",
                                "scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=neighbor",
                                "-pix_fmt", "yuv420p", str(p)], capture_output=True, text=True)
            if r.returncode:
                raise EmulatorError(f"ffmpeg failed: {r.stderr.strip()[-500:]}")
        else:
            p.mkdir(parents=True, exist_ok=True)
            for i, im in enumerate(shots):
                im.save(p / f"{i:05d}.png")
        return {"path": str(p), "frames": len(shots), "fps": fps, "size": list(shots[0].size),
                "seconds": round(time.monotonic() - t0, 2)}

    # ---------------------------------------------------- checkpoint replays (agent input)

    def input_log(self) -> list[dict]:
        return list(self._inputs)

    def checkpoint(self, label: str = "") -> dict:
        pos = self.position()
        self._seq += 1
        cp = {"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
              "label": label or f"cp{len(self._checkpoints) + 1}", "screen": self.frame_hash(),
              "pc": self.regs()["PC"]}
        self._checkpoints.append(cp)
        return cp

    def save_replay(self, path=None) -> dict:
        """Input log + checkpoints + the final position/hash since the last load (JSON)."""
        if not self.program:
            raise EmulatorError("no program loaded: a replay starts from load()")
        end = self.checkpoint("end")
        self._checkpoints.pop()
        rec = {"version": 1, "program": self.program, "machine": self.machine, "tv": self.tv,
               "events": self.input_log(), "checkpoints": list(self._checkpoints), "end": end}
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            Path(path).write_text(json.dumps(rec, indent=1))
        return rec

    def replay(self, rec, verify: bool = True) -> dict:
        """Replay a recording: version 1 (save_replay: agent input + checkpoints) or
        version 2 (record_input: every input source + every frame's hash)."""
        base = None
        if not isinstance(rec, dict):
            base = Path(rec).resolve().parent
            rec = json.loads(Path(rec).read_text())
        if rec.get("tv") and rec["tv"] != self.tv:
            self.tv = rec["tv"]
        if rec.get("version") == 2:
            return self._replay_v2(rec, base, verify)
        if not Path(rec["program"]).exists():
            raise EmulatorError(f"replay program {rec['program']} not found")
        self.delete()
        self.load(rec["program"], rec.get("machine"))
        self._inputs = []
        items = [("event", e) for e in rec["events"]] + [("check", c) for c in rec["checkpoints"]]
        items.append(("check", rec["end"]))
        items.sort(key=lambda it: (it[1]["f"], it[1]["t"], it[1].get("seq", 0)))
        results = []
        with self.held():
            for kind, it in items:
                self.run_to(it["f"], it["t"])
                if kind == "event":
                    self._held = list(it.get("keys", []))
                    self._push_input(state=it["state"])
                elif verify:
                    got = self.frame_hash()
                    results.append({"label": it["label"], "f": it["f"], "t": it["t"],
                                    "expected": it["screen"], "got": got, "ok": got == it["screen"]})
        ok = all(r["ok"] for r in results)
        return {"ok": ok, "checkpoints": results, "events": len(rec["events"]), "position": self.position()}

    # ---------------------------------------------------- input recorder (all sources)

    def record_input(self, action: str = "start", path=None, from_reset: bool = False) -> dict:
        """Record every input change the machine sees (agent-injected AND the user's
        keyboard/joystick in the window), per frame, plus a hash of every frame.

        start: from here (a state is saved to replay from; stopped mid-frame, the
               frame is finished first, breakpoints off), or
               from_reset=True: reload the program and record from the load.
        stop / save (path: JSON + .a8s state beside it) / status."""
        if action == "start":
            if from_reset and not self.program:
                raise EmulatorError("from_reset needs a loaded program")
            with self.held():
                if from_reset:
                    self.reload()
                    snap = None
                else:
                    # input is sampled per frame and states are exact at frame
                    # boundaries: a recording starts at one (finish this frame)
                    if self.position()["midframe"]:
                        self._run(1, nobreak=1)
                    snap = self.save_state(self._tmp / f"rec-start-{time.monotonic_ns()}{STATE_EXT}")
                pos = self.position()
                self._cmd("hash action=clear")
                self._cmd("hash action=on")
                self._cmd("rec action=start")
            self._recording = {"program": self.program, "machine": self.machine, "tv": self.tv,
                               "snapshot": snap, "from_reset": bool(from_reset), "frame": pos["frame"],
                               "since_load": pos["since_load"], "done": False}
            return {"recording": True, **pos}
        rec = self._recording
        if rec is None:
            raise EmulatorError("not recording (record_input start first)")
        if action == "status":
            return {"recording": not rec["done"], **self._kv("rec action=status")}
        if action in ("stop", "save") and not rec["done"]:
            with self.held():
                pos = self.position()
                self._cmd("rec action=stop")
                self._cmd("hash action=off")
                rec["events"] = self._rec_events(rec["frame"])
                rec["hashes"] = self._hashes(rec["frame"])
                rec["end"] = {"f": pos["frame"] - rec["frame"]}
            rec["done"] = True
        if action == "stop":
            return {"recording": False, "events": len(rec["events"]), "frames": rec["end"]["f"]}
        if action == "save":
            if not path:
                raise ValueError("save needs a path")
            p = Path(path).resolve()
            p.parent.mkdir(parents=True, exist_ok=True)
            out = {"version": 2, "program": rec["program"], "machine": rec["machine"], "tv": rec["tv"],
                   "start": {"from_reset": rec["from_reset"], "since_load": rec["since_load"]},
                   "events": rec["events"], "hashes": rec["hashes"], "end": rec["end"]}
            if rec["snapshot"]:
                snap = p.with_suffix(STATE_EXT)
                shutil.copy(rec["snapshot"], snap)
                shutil.copy(rec["snapshot"] + ".a8h", str(snap) + ".a8h")
                out["start"]["snapshot"] = snap.name
            p.write_text(json.dumps(out, indent=None, separators=(",", ":")))
            return {"path": str(p), "events": len(out["events"]), "frames": rec["end"]["f"],
                    "hashes": len(out["hashes"]), "snapshot": out["start"].get("snapshot")}
        raise ValueError(f"unknown action {action!r}: start, stop, save, status")

    def _rec_events(self, base_frame: int) -> list:
        r = self._kv("rec action=get since=0", timeout=60)
        evs = []
        for item in filter(None, r.get("data", "").split(",")):
            f, key, shift, consol, p0, p1, trig = item.split(":")
            evs.append({"f": int(f) - base_frame, "state": {"key": int(key), "shift": int(shift),
                        "consol": int(consol, 16), "port0": int(p0, 16), "port1": int(p1, 16),
                        "trig": int(trig, 16)}})
        return evs

    def _hashes(self, base_frame: int) -> list:
        r = self._kv("hash action=get", timeout=60)
        first, data = int(r["first"]), r.get("data", "")
        return [[first + i - base_frame, data[8 * i:8 * i + 8]] for i in range(len(data) // 8)]

    def _replay_v2(self, rec: dict, base: Path | None, verify: bool) -> dict:
        start = rec["start"]
        self.delete()
        if rec.get("machine") and rec["machine"] != self.machine:
            self.set_machine(rec["machine"])
        if start.get("snapshot"):
            snap = Path(start["snapshot"])
            if not snap.is_absolute() and base is not None:
                snap = base / snap
            self.load_state(snap)
            self.program = rec["program"]
            self.symbols = Symbols.load(rec["program"])
        else:
            self.load(rec["program"], rec.get("machine"))
            if start.get("since_load"):
                self._run(start["since_load"], nobreak=1)
        with self.held():
            f0 = self.position()["frame"]
            self._cmd("hash action=clear")
            self._cmd("hash action=on")
            for ev in rec["events"]:
                n = f0 + ev["f"] - self.position()["frame"]
                if n > 0:
                    self._run(n, nobreak=1)
                st = ev["state"]
                self._cmd("input raw=1 " + " ".join(f"{k}={st[k]}" for k in
                                                     ("key", "shift", "consol", "port0", "port1", "trig")))
            n = f0 + rec["end"]["f"] - self.position()["frame"]
            if n > 0:
                self._run(n, nobreak=1)
            self._cmd("input raw=0")
            self._cmd("hash action=off")
            got = self._hashes(f0)
        res = {"ok": True, "frames": rec["end"]["f"], "events": len(rec["events"]), "hashes": got}
        if verify and rec.get("hashes"):
            want = {f: h for f, h in rec["hashes"]}
            have = {f: h for f, h in got}
            common = sorted(set(want) & set(have))
            bad = [f for f in common if want[f] != have[f]]
            res.update(ok=not bad and len(common) > 0, compared=len(common),
                       first_mismatch=bad[0] if bad else None, mismatches=len(bad))
        return res

    # ---------------------------------------------------- registers / memory

    def addr(self, expr) -> int:
        """Number, $hex/0xhex/NNh, symbol (main or _main), symbol+off, or register."""
        regs = None
        if isinstance(expr, str) and expr.strip().upper() in ("PC", "A", "X", "Y", "S", "SP", "P"):
            regs = self.regs()
            regs["SP"] = 0x100 + regs["S"]
        try:
            return self.symbols.resolve(expr, regs)
        except ValueError as ex:
            raise EmulatorError(str(ex)) from None

    def regs(self) -> dict:
        r = self._kv("regs")
        out = {k.upper(): int(r[k], 16) for k in ("a", "x", "y", "s", "p", "pc")}
        out["FLAGS"] = r["flags"]
        return out

    def set_reg(self, name: str, value) -> dict:
        v = self.addr(value) if isinstance(value, str) else int(value)
        n = name.strip().lower()
        self._cmd(f"setreg name={'s' if n == 'sp' else n} value={v}")
        return self.regs()

    def read_mem(self, addr, length: int = 16, bank: int | None = None, space: str = "cpu") -> bytes:
        """CPU view by default (hardware registers read without side effects);
        space='ram': the RAM under the OS/BASIC ROMs and behind a mapped bank;
        bank=N: 130XE extended bank N (0..3), mapped or not (addr: offset or $4000-$7FFF)."""
        a = self.addr(addr) if bank is None else (self.addr(addr) if isinstance(addr, str) else int(addr))
        sp = f"bank={int(bank)}" if bank is not None else f"space={space}"
        return bytes.fromhex(self._kv(f"peek addr={a} len={int(length)} {sp}")["data"])

    def write_mem(self, addr, data: bytes, bank: int | None = None, space: str = "cpu"):
        a = self.addr(addr) if bank is None else (self.addr(addr) if isinstance(addr, str) else int(addr))
        sp = f"bank={int(bank)}" if bank is not None else f"space={space}"
        data = bytes(data)
        for i in range(0, len(data), 4096):
            self._cmd(f"poke addr={a + i} data={data[i:i + 4096].hex()} {sp}")

    def peek(self, addr) -> int:
        return self.read_mem(addr, 1)[0]

    def peek16(self, addr) -> int:
        b = self.read_mem(addr, 2)
        return b[0] | b[1] << 8

    def poke(self, addr, value: int):
        self.write_mem(addr, bytes([value & 0xFF]))

    def find_mem(self, needle: bytes, start: int = 0, end: int = 0x10000, bank: int | None = None,
                 limit: int = 64) -> list[int]:
        if isinstance(needle, str):
            needle = needle.encode("latin-1")
        sp = f" bank={bank}" if bank is not None else ""
        r = self._kv(f"find data={bytes(needle).hex()} start={int(start)} end={min(int(end), 0x10000) - 1} "
                     f"max={limit}{sp}")
        return [int(a, 16) for a in r.get("addrs", "").split(",") if a]

    def hw(self) -> dict:
        """ANTIC/GTIA/POKEY/PIA registers (the values last written; read-only
        registers such as collisions are not included) and the memory setup."""
        r = self._kv("hw")
        out: dict = {}
        for k, v in r.items():
            chip, _, name = k.partition(".")
            vals = v.split(",")
            try:
                parsed = [int(x, 16) for x in vals] if name not in ("ypos", "random", "ram_kb", "xe_banks",
                                                                   "cpu_bank", "selftest", "basic", "trig") \
                    else [int(x) for x in vals]
            except ValueError:
                parsed = vals
            out.setdefault(chip, {})[name] = parsed[0] if len(parsed) == 1 else parsed
        return out

    def memory_map(self) -> dict:
        """PORTB: OS ROM, BASIC, self test, the 130XE bank mapped for the CPU / ANTIC."""
        h = self.hw()
        portb = h["pia"]["portb"] | h["pia"]["portb_mask"]
        xl = self.machine not in ("800", "400")
        out = {"machine": self.machine, "ram_kb": h["mem"]["ram_kb"], "xe_banks": h["mem"]["xe_banks"]}
        if xl:
            banks = h["mem"]["xe_banks"]
            out.update(portb=portb, os_rom=bool(portb & 1), basic=not (portb & 2),
                       selftest=bool(h["mem"]["selftest"]),
                       cpu_bank=None if not banks or portb & 0x10 else (portb >> 2) & 3,
                       antic_bank=None if not banks or portb & 0x20 else (portb >> 2) & 3)
        return out

    def disasm(self, addr="pc", count: int = 12) -> list[str]:
        a = self.addr(addr)
        data = self.read_mem(a, min(3 * count + 3, 0x10000 - a) or 1)
        mem = {(a + i) & 0xFFFF: b for i, b in enumerate(data)}

        def rd(x):
            if x not in mem:
                mem.update({(x + i) & 0xFFFF: b for i, b in enumerate(self.read_mem(x, 3))})
            return mem[x]

        def sym(v):
            n = self.symbols.name_of(v)
            return n if n and "+" not in n else None

        out = []
        for _ in range(count):
            name = self.symbols.name_of(a)
            if name and "+" not in name:
                out.append(f"{name}:")
            n, b, text = cpu6502.disasm_one(rd, a, sym)
            out.append(f"  {a:04X}  {' '.join(f'{x:02X}' for x in b):<9} {text}")
            a = (a + n) & 0xFFFF
        return out

    # ---------------------------------------------------- debugger

    def _arm(self, b: dict):
        if b.get("kind"):
            self._cmd(f"watch id={b['index']} addr={b['addr']} len={b['length']} kind={b['kind']}")
        else:
            line = f"break id={b['index']}"
            if b.get("addr") is not None:
                line += f" pc={b['addr']}"
            if b.get("expr"):
                line += f" cond={_q(b['expr'])}"
            self._cmd(line)

    def _new_id(self) -> int:
        i = self._next_id
        self._next_id += 1
        return i

    _COND_WORDS = {"A", "X", "Y", "S", "P", "PC", "N", "V", "B", "D", "I", "Z", "C", "FRAME", "LINE",
                   "XPOS", "BANK", "AND", "OR", "NOT", "PEEK", "DPEEK"}

    def _cond_expr(self, condition: str) -> str:
        """Symbols in a condition -> numbers ('PC=_main AND A=5' -> 'PC=$2090 AND A=5')."""
        def rep(m):
            w = m.group(0)
            if w.upper() in self._COND_WORDS:
                return w
            a = self.symbols.lookup(w)
            if a is None:
                raise EmulatorError(f"unknown symbol {w!r} in condition")
            return f"${a:04X}"
        # $hex / 0x / %bin numbers are left alone by the identifier pattern
        return re.sub(r"(?<![$%\w])[A-Za-z_][\w@.]*", rep, condition)

    def break_at(self, addr) -> dict:
        """Breakpoint at an address/symbol (stops before executing it)."""
        a = self.addr(addr)
        i = self._new_id()
        b = {"index": i, "addr": a, "condition": f"PC=${a:04X}", "desc": f"{addr} (${a:04X})"}
        self._arm(b)
        self._bps[i] = b
        return b

    def break_if(self, condition: str, desc: str | None = None, addr: int | None = None) -> dict:
        """Breakpoint on a condition: 'A=5 AND PC=$2000', 'PC=_main', '[$D40B]>100',
        'DPEEK($58)=$A010', 'FRAME>=200'. Registers A X Y S P PC, flags N V B D I Z C,
        FRAME LINE XPOS BANK; = == != < > <= >= + - & | ^ AND OR NOT; [a]/PEEK(a), DPEEK(a)."""
        i = self._new_id()
        b = {"index": i, "condition": condition, "expr": self._cond_expr(condition),
             "desc": desc or condition, "addr": addr}
        self._arm(b)
        self._bps[i] = b
        return b

    def watch(self, addr, length: int = 1, kind: str = "w") -> dict:
        """Stop after the instruction that reads ('r'), writes ('w') or accesses ('rw')
        [addr, addr+length) (CPU data accesses through the operand; not DMA or the stack)."""
        a = self.addr(addr)
        kind = {"wr": "rw"}.get(kind, kind)
        if kind not in ("r", "w", "rw"):
            raise EmulatorError("kind: r, w or rw")
        i = self._new_id()
        w = {"index": i, "addr": a, "length": int(length), "kind": kind, "desc": f"{addr} (${a:04X}) {kind}"}
        self._arm(w)
        self._watches[i] = w
        return w

    def delete(self, index: int | None = None, addr=None):
        """Remove a breakpoint/watchpoint (by index, or every one at addr), or everything."""
        if index is None and addr is None:
            self._cmd("delete all=1")
            self._bps.clear()
            self._watches.clear()
            return
        ids = []
        if index is not None:
            ids.append(int(index))
        if addr is not None:
            a = self.addr(addr)
            ids += [i for i, b in {**self._bps, **self._watches}.items() if b.get("addr") == a]
        for i in ids:
            if i in self._bps or i in self._watches:
                self._cmd(f"delete id={i}")
                self._bps.pop(i, None)
                self._watches.pop(i, None)

    def breakpoints(self) -> dict:
        hits = {}
        for item in filter(None, self._kv("breaks").get("list", "").split(";")):
            f = item.split(":")
            hits[int(f[0])] = int(f[-1])
        return {"breakpoints": [{**b, "hits": hits.get(i, 0)} for i, b in self._bps.items()],
                "watchpoints": [{**w, "hits": hits.get(i, 0)} for i, w in self._watches.items()]}

    def backtrace(self, items: int = 8) -> list[dict]:
        """Best effort: return addresses on the 6502 stack. A word is a return
        address if the three bytes before its target are a JSR; each entry gives
        the caller (the JSR) and the routine it called, with symbols."""
        r = self.regs()
        s = r["S"]
        stack = self.read_mem(0x100 + s + 1, 0xFF - s) if s < 0xFF else b""
        out = [{"frame": 0, "pc": r["PC"], "symbol": self.symbols.name_of(r["PC"])}]
        i = 0
        while i + 1 < len(stack) and len(out) <= items:
            w = stack[i] | stack[i + 1] << 8
            jsr = (w - 2) & 0xFFFF
            code = self.read_mem(jsr, 3)
            if code[0] == 0x20:
                target = code[1] | code[2] << 8
                out.append({"frame": len(out), "sp": 0x100 + s + 1 + i, "return": (w + 1) & 0xFFFF,
                            "caller": jsr, "caller_symbol": self.symbols.name_of(jsr),
                            "called": target, "called_symbol": self.symbols.name_of(target)})
                i += 2
            else:
                i += 1
        return out

    # ---------------------------------------------------- states

    def save_state(self, path) -> str:
        """An Atari800 state file (.a8s) plus <file>.a8h (the harness's frame counter).
        Exact at a frame boundary. Stopped mid-frame (a breakpoint, stepi) it holds
        the CPU, memory and chips as they are, but Atari800 can't resume ANTIC
        inside a frame: loading it starts a new frame at line 0 (self.last_state_midframe)."""
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(STATE_EXT)
        p = p.resolve()
        p.parent.mkdir(parents=True, exist_ok=True)
        r = self._kv(f"savestate path={_q(p)}")
        self.last_state_midframe = r.get("midframe") == "1"
        if not p.exists():
            raise EmulatorError(f"state not written: {p}")
        return str(p)

    def load_state(self, path) -> dict:
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(STATE_EXT)
        p = p.resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        self._cmd(f"loadstate path={_q(p)}")
        side = Path(str(p) + ".a8h")
        if side.exists():
            info = dict(ln.split("=", 1) for ln in side.read_text().splitlines() if "=" in ln)
            if info.get("machine") in MACHINES:
                self.machine = info["machine"]
            if info.get("loaded"):
                self.program = info["loaded"]
                self.symbols = Symbols.load(self.program)
        self.load_frame = 0
        self._held = []
        self._push_input(log=False)
        return self.status()
