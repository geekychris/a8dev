# The emulator harness

a8dev runs programs in **Atari800 7.2.1 patched with the a8h harness**
(`emulator/ext/a8h.c`, `emulator/patches/`). The patch gives Atari800 a
control socket and makes it deterministic, so Claude (MCP), tests, the CLI
and DevBench can drive it: run exact frames, wait for log lines, press keys,
set breakpoints, read 130XE banks, take screenshots, record and replay.

```
 MCP (Claude) ──┐   ┌──── a8dev session process ─────┐        ┌───────────────────────┐
 ./a8 ctl ──────┼──▶│ JSON control port ─ Emulator ──┼─ TCP ─▶│ atari800-a8h (headless)│
 DevBench ──────┘   │ registry build/sessions/*.json │  a8h   │ atari800-a8h-sdl (window)
                    └────────────────────────────────┘        └───────────────────────┘
```

`a8dev.emu.Emulator` (`tools/a8dev/src/a8dev/emu.py`) starts the emulator and
drives it; tests use it in-process, sessions wrap it (docs/mcp.md). This
page is the Emulator API. The raw protocol is in emulator/patches/README.md.

## Building

`scripts/build-emulator.sh` (run by setup) fetches the pinned Atari800,
applies the patch and builds:

- `third_party/atari800/build/atari800-a8h`: headless (libatari800, no SDL).
- `third_party/atari800/build/atari800-a8h-sdl`: the window (needs SDL2).

Needs a C compiler, make, autoconf, automake, zlib, and SDL2 for the window:

| | |
|---|---|
| macOS | `xcode-select --install; brew install autoconf automake sdl2` |
| Debian/Ubuntu | `sudo apt install build-essential autoconf automake zlib1g-dev libsdl2-dev` |
| Windows | WSL with the Debian packages (recommended), or MSYS2: `pacman -S base-devel autoconf automake mingw-w64-x86_64-{gcc,SDL2,zlib}` and run the script from the MinGW shell |

Only the built-in AltirraOS and Altirra BASIC ROMs are used. Nothing is
downloaded, no directory is scanned for ROM files, and `~/.atari800.cfg` is
neither read nor written (each emulator gets an empty config).

## Sessions

```python
from a8dev.emu import Emulator
with Emulator(machine="130xe") as emu:          # headless, paused
    emu.load("projects/hello/build/hello.xex")
    assert emu.run_until("hello: ready", 600)
    emu.press("SPACE")
    emu.screenshot("shot.png")
```

`Emulator(machine="130xe", window=False, paused=None, tv="pal", zoom=None,
audio=None, seed=0, extra_args=(), log_path=None, basic=False)`

| | |
|---|---|
| headless (default) | **paused**: frames advance only on `run`, `run_until`, `press`, `continue_`, `stepi`…, as fast as the host allows (~550× real time), identically every time |
| `window=True` | an SDL window for the human, **free-running** in real time. `run`/`run_until` fast-forward it (the window keeps showing frames and the user's keyboard/joystick stay live), then real time resumes. A breakpoint, watchpoint or step **holds** the window until `resume()` |
| `pause()` / `resume(turbo=False)` | switch either kind |
| `seed` | POKEY RANDOM after power-on (default 0) |
| `zoom` | window scale (default 2) |

Each emulator is our own child process. `close()` (or the `with` block) quits
it, and it exits by itself if the Python process dies. Nothing ever stops
emulators by name.

## Machines and programs

**Machines:** `130xe` (default, 128K: four 16K banks), `800xl`, `65xe`,
`600xl` (16K), `1200xl`, `800` (48K, AltirraOS 400/800), `400` (16K),
`320xe` (Compy-Shop 320K). `tv="pal"` (default, 312 lines, 49.86 Hz) or
`"ntsc"` (262 lines, 59.92 Hz). XEGS isn't available: its built-in game
ROM isn't free.

| | |
|---|---|
| `load(path, machine=None)` | **power-on**, then run `.xex/.com/.exe/.obx` directly, boot `.atr/.xfd/.atx/.dcm` from D1:, insert `.car/.rom`, load `.bas/.lst` into Altirra BASIC, or restore a state (`.a8s`). Symbols come from the `.lbl`/`.map` beside it |
| `set_machine(name)`, `set_tv("ntsc")` | power-on as another machine |
| `reset(hard=True)` | power-on (program gone); `hard=False`: the RESET key (warm start) |
| `reload()` | power-on and load the same program |
| `power_cycle()` / `reboot()` | a fresh emulator process (reboot also reloads; use it on a wedged emulator) |

**Power-on is exact.** RAM (main, banks and under the ROMs) is zero, the chip
registers are cleared, RANDOM is seeded, and the state is the one the
emulator saved for that machine before it ran any frame. So a program boots
identically in a fresh emulator and in one that ran other programs.

## Running

| | |
|---|---|
| `run(frames=1)` | exactly N frames (a frame = one field: 312 or 262 lines × 114 machine cycles); stops early at a breakpoint |
| `run_until(text, max_frames=3000, since=None)` | until the guest log contains `text` → bool. Only text written after the **mark** counts: the last `log()`, `load`/`reset`, or the previous match (`since=` an absolute offset from `log_cursor()` overrides). Stops on the instruction after the one that wrote its last byte; text already written (say during a `press`) matches without running |
| `run_to(frame, tstates=0)` | to frame N since load, `tstates` machine cycles into it (0: the boundary) |
| `run_to_addr(addr, max_frames)` / `run_until_signal(max_frames)` | to an address / to the next signal |
| `position()` | `frame`, `since_load`, `cycles` (machine cycles since load, DMA included), `tstates` (cycles into the frame), `midframe` |
| `status()` | machine, mode (`paused`/`free`/`running`), position, PC + symbol, log end, signals, `fps`, `last_stop` |

Every run returns a dict: `why` (`frames`, `frame_limit`, `breakpoint`,
`watchpoint`, `step`, `log_match`, `position`, `signal`, `cpu_jam`), `frames`
run, `frame`, `since_load`, `cycles`, `midframe` (+ `line`, `xpos`), `pc`,
`symbol`, `regs`, `fps` and `speed` (× real time), and the breakpoint/watch
details. `last_stop` holds the same for the latest stop, including one that
happened while a window free-ran.

## The guest log and signals

Programs write a character to **`$D1F0`** for each byte of the log (`$0A` or
the ATASCII EOL `$9B` ends a line) and a status byte to **`$D1F1`** (a
*signal*: `a8_done()` sends 0, `a8_fail()` 1). `$D1xx` is the PBI page:
a stock 400/800/XL/XE ignores it unless a PBI device is selected through
`$D1FF`, so the writes are harmless on real hardware. In the harness
`$D1F0` reads `$A8` and `$D1F1` reads `$D8` (`a8_in_harness()`). The SDK
wrappers are in sdk/include/a8log.h; release builds compile them out.

| | |
|---|---|
| `log(since=None)` | text since the mark (moves the mark to the end) |
| `log_text(since)`, `log_cursor()`, `debug_log` | without moving the mark / the end offset / every line so far |
| `status()["signal"]`, `run_until_signal()` | the last signal |

The emulator keeps the last 4 MB of log, addressed by absolute offsets.

## Input

Atari800 reads the keyboard, joysticks, triggers and console keys **once per
frame**, so input takes effect at the next frame and replays are exact at
frame granularity.

| | |
|---|---|
| `press(keys, hold_frames=3, release_frames=3)` | press together, hold, release, wait |
| `hold(keys)` / `release(keys=None)` | open-ended (None = everything) |
| `type(text, hold_frames=3, gap_frames=3)` | key by key; `\n` = RETURN; letters use the plain key (the OS starts in caps), symbols their shifted key |
| `joystick(dirs, port=0)` | stick 0-3: `"UP+LEFT+FIRE"`, `None` = centre |
| `console(keys)` | hold START/SELECT/OPTION |
| `sequence(steps)` | `[{"press": "Q"}, {"run": 20}, {"until": "x", "max": 500}, {"type": "HI"}, {"hold": "UP"}, {"release": "UP"}, {"joystick": "FIRE", "port": 1}]`; any step may carry `"at": N` (frames since load) |

**Key names:** `A`–`Z`, `0`–`9`, `SPACE`, `RETURN`, `ESC`, `TAB`,
`DELETE`, `HELP`, `BREAK`, `CAPS`, `INVERSE`, `CLEAR`, `INSERT`, `F1`–`F4`
(1200XL), `COMMA PERIOD SLASH SEMICOLON MINUS EQUAL PLUS ASTERISK LESS
GREATER`, `CURSOR_UP/DOWN/LEFT/RIGHT`; modifiers `SHIFT`, `CTRL`
(`SHIFT+A`, `CTRL+1`); console `START SELECT OPTION`; joystick `UP DOWN
LEFT RIGHT FIRE` (stick 0), `UP1`… `FIRE3` for sticks 1–3. One normal key
at a time (the Atari keyboard reports one).

In a window, the user's own keys and joystick reach the machine as usual;
the harness's input is merged with them.

## Screen

| | |
|---|---|
| `screenshot(path=None, border=True, scale=1)` | PIL RGB image: `border=True` 336×240 (what a TV shows; the 320×192 playfield at 8,24), `False` the playfield, `"full"` Atari800's 384×240 buffer |
| `screen_hash()` | md5 of the visible pixels |
| `frame_hash()` | the emulator's FNV-1a of the whole frame buffer (what recordings compare) |
| `record_video(path, frames=100, every=1, scale=1, border=True)` | `.gif`, `.mp4` (ffmpeg) or a directory of PNGs; a paused session advances deterministically |
| `frame_buffer()`, `palette()` | the raw buffer (palette indices) and the 256 RGB entries |

Colours written in the vertical blank (the OS copies its shadows there)
show in the *next* frame.

## Memory, banks, hardware

| | |
|---|---|
| `read_mem(addr, length, bank=None, space="cpu")` | the CPU's view (I/O registers read without side effects); `space="ram"`: the RAM under the OS/BASIC ROMs, and main memory behind a mapped bank; `bank=N`: 130XE bank 0–3 (offset 0–`$3FFF` or `$4000`–`$7FFF`), whether mapped or not |
| `write_mem(addr, data, bank=None, space="cpu")` | the same spaces; a cpu-space write to an I/O register takes effect |
| `peek`, `peek16`, `poke`, `find_mem(bytes or text, start, end, bank)` | |
| `memory_map()` | PORTB decoded: OS ROM, BASIC, self test, the bank the CPU and ANTIC see |
| `hw()` | ANTIC (`dmactl`, `dlist`, `chbase`, `nmien`, `ypos`…), GTIA (positions, sizes, colours, `prior`, `gractl`, triggers), POKEY (`audf/audc/audctl`, `kbcode`, `irqen/irqst`, `skctl`, the RANDOM counter), PIA (ports, controls), memory config. Write registers show their last written value |
| `regs()` / `set_reg(name, value)` | `A X Y S P PC` and `FLAGS` (`NV-BDIZC`); values may be symbols |

Addresses anywhere: numbers, `$hex`/`0xhex`/`NNh`, symbols (`main` or
`_main`), `symbol+0x10`, or a register name.

## Debugging

| | |
|---|---|
| `break_at(addr)` | before the instruction at addr runs |
| `break_if(condition)` | `"A=5 AND PC=$2000"`, `"PC=_main AND [counter]>=$10"`, `"DPEEK($58)=$A010"`, `"FRAME>=200"`, `"BANK=2"` |
| `watch(addr, length=1, kind="w")` | `r`, `w`, `rw`: stops **after** the instruction that accessed it, with `insn_pc`, `access`, `old` and `value`. CPU data accesses through the operand (not DMA, not pushes and pulls) |
| `delete(index=None, addr=None)`, `breakpoints()` | everything if no argument; hits counted |
| `continue_(max_frames=500)` | until one fires (`stopped`) |
| `stepi(count=1)`, `step_over()` | `step_over` runs a JSR until it returns |
| `disasm(addr="pc", count=12)` | 6502 + undocumented opcodes, with symbols |
| `backtrace(items=8)` | best effort: words on the stack whose target follows a JSR, giving each caller and the routine it called |

**Conditions:** `A X Y S P PC`, flags `N V B D I Z C`, `FRAME` (since
load), `LINE` (scanline), `XPOS` (cycle in the line), `BANK` (130XE bank the
CPU sees, -1 for none); numbers; `[addr]` or `PEEK(addr)`, `DPEEK(addr)`;
`= == != <> < > <= >=`, `+ - & | ^`, `AND OR NOT && || !`, parentheses.
Symbols become numbers on the Python side.

A breakpoint is checked before every instruction: a few thousand
frames/s instead of ~30,000. Resuming from a stop doesn't re-fire on the
instruction it stopped at. A JAM opcode stops with `why=cpu_jam`, and the
CPU stays jammed (as on real hardware) until a reset or `set_reg("PC", …)`.

## States

`save_state(path)` writes an Atari800 state (`.a8s`, the whole machine,
including 130XE banks) and `<path>.a8h` (frame counter, machine, program).
`load_state(path)` restores both. States are exact at a frame boundary: a
loaded state continues frame for frame as the original did. One saved
mid-frame (stopped at a breakpoint) holds the CPU, memory and chips, but
Atari800 can't resume ANTIC inside a frame: on load the frame restarts at
line 0 (`last_state_midframe` tells you). States from the window and
headless builds are interchangeable.

## Recording and replay

**Input recorder** (`record_input`, MCP `emu_record_input`, `./a8 record`):
the patched core records the input the machine actually sees each frame
(key, SHIFT, console keys, both joystick ports, triggers), from every
source: the harness's input, and the user's keyboard and joystick in a
window. It also hashes every frame.

- `record_input("start")`: from here. A state is saved to replay from (a
  stop mid-frame first finishes the frame, breakpoints off).
  `from_reset=True`: reload the program and record from the load.
- `record_input("save", path)`: JSON (events, per-frame hashes, end) plus
  the `.a8s` beside it. Also `"stop"` and `"status"`.
- `replay(path)` (`./a8 replay r.json --runs 2`): restores the state (or
  reloads), forces each frame's recorded input, hashes every frame and
  compares → `ok`, `compared`, `mismatches`, `first_mismatch`, `hashes`.

A window recording replays headless with every frame hash matching, because
both builds emulate the same machine and the input is applied where the
core reads it.

**Checkpoint replays** (`save_replay` / `replay` of a version-1 file):
the agent's own inputs since `load`, with `checkpoint(label)` frame hashes.

## Determinism

A paused session is a pure function of its inputs: power-on is a fixed
state, RANDOM is seeded (`seed=`), nothing reads the wall clock (R-Time 8
off), and input changes only between frames. Two emulators given the same
program and inputs produce the same frame hashes, registers and log, and so
does one emulator that ran something else first. tools/a8dev/tests/test_emulator.py
checks all of this. A free-running window is paced by the host clock and
so isn't deterministic, but its recording replays exactly.

## Tests

`tools/a8dev/tests/test_emulator.py` (in `./a8 selftest`) runs
`tests/emu/emutest.s`, a small asm program that logs keys, sticks,
triggers, console keys, RANDOM and banks, and covers every command above.
`A8_TEST_WINDOW=1` adds the window session (it opens a window).
