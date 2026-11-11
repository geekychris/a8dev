# Our Atari800 patches

Upstream: [atari800/atari800](https://github.com/atari800/atari800) (GPL-2.0),
pinned in `scripts/lib.sh` (`ATARI800_COMMIT`, tag `ATARI800_7_2_1`).
`scripts/build-emulator.sh` fetches that commit into `third_party/atari800`,
clones it to `third_party/atari800-build/src-tree`, copies
`emulator/ext/a8h.{c,h}` into `src/`, applies the patches here, runs
`autoreconf`, and builds twice:

| Binary | Atari800 target | |
|---|---|---|
| `third_party/atari800/build/atari800-a8h` | `--target=libatari800` + `emulator/ext/a8h_headless_main.c` | headless: no SDL, no window, no sound device |
| `third_party/atari800/build/atari800-a8h-sdl` | `--target=default --with-video=sdl2 --with-sound=sdl2` | window (only if SDL2 is installed) |

Both builds compile the same harness (`-DA8H`) and the same emulation core
(`NEW_CYCLE_EXACT` is forced on in the headless build, which libatari800
otherwise leaves off), so a recording made in a window replays headless,
hash for hash. The binaries are installed with a rename, so emulators that
are already running keep the old file.

Configure flags: `--disable-download` (Atari800 7.x can fetch ROMs from the
net; `a8h.h` refuses to build with it), no MP3/readline/NetSIO/R:/PBI
devices/Voicebox/on-screen keyboard/crash menu. Only the built-in AltirraOS
(400/800 and XL/XE) and Altirra BASIC ROMs are ever used: no copyrighted ROMs.

## The patch (`0001-a8h-harness-hooks.patch`, ~120 lines)

Only hook calls, all under `#ifdef A8H`; the logic is in `emulator/ext/a8h.c`.

| File | Hook | Why |
|---|---|---|
| `memory.c` (end) | `#include "a8h.c"` | the harness can read memory.c's static 130XE buffers (`atarixe_memory`, the RAM under the OS/BASIC ROMs) |
| `cpu.c` instruction fetch | `if (A8H_cpu_check) A8H_CpuHook()` (registers synced around it) | breakpoints, conditions, watchpoints, stepi/step-over, run-to-address/cycle, the stop after a log match. Costs nothing while `A8H_cpu_check` is 0 |
| `cpu.c` CIM/JAM opcodes | `A8H_Cim(insn)` | a jammed CPU stops the run (`why=cim`) instead of opening Atari800's monitor/menu |
| `input.c` `INPUT_Frame` | `A8H_InputKeys()`, `A8H_InputPort()`, `A8H_InputTrig()` | harness input merged with the host's (or forced, for replays), and every frame's input recorded, whatever its source |
| `pbi.c` `PBI_D1PutByte` / `PBI_D1GetByte` | `A8H_D1Put()`; reads of `$D1F0`/`$D1F1` | the guest log: a byte written to `$D1F0` is a character, to `$D1F1` a signal. In the harness `$D1F0` reads `$A8` and `$D1F1` `$D8` (`a8_in_harness()`) |
| `atari.c` `Atari800_Initialise` | skip `SYSROM_FindInDir` and `CFG_WriteConfig` when a8h is on | no scanning directories for ROM files, never writing `~/.atari800.cfg` |
| `sio.c` | `SIO_A8H_PowerOn()` | Atari800's coldstart keeps the serial state and the "Overmind" sector-1 delay (3200 scanlines): a reboot after a XEX ran was ~10 frames slower than the first boot |
| `libatari800/libatari800.h` | `STATESAV_MAX_SIZE` 210000 → 1 MB | 130XE states are 270 KB: libatari800 silently dropped the extended banks |
| `sdl/main.c` | `A8H_Init/Start`, `A8H_LOOP_TOP()` (setjmp + `A8H_FrameStart`), `A8H_FrameEnd()`, an idle callback | the window build's main loop |

The headless main loop is `A8H_HeadlessMain()` in a8h.c (libatari800 has no
`main`).

## How a8h works

Everything runs on the emulator's main thread: no locks, nothing AppKit
dislikes. The control socket (TCP, 127.0.0.1, port chosen by the OS and
printed as `A8H port=N pid=P`) is polled at the top of every frame.

- **Paused** (headless default): the main loop blocks on the socket at the
  frame boundary until a `run`.
- **Free** (window default, or `resume`): frames run in real time
  (Atari800's own sync; headless paces itself) and commands are served
  between frames. A client's `run` takes over in turbo, then real time
  resumes.
- **Stopped mid-frame**: a breakpoint, watchpoint, step, log match or
  position stop enters a nested command loop *inside the CPU hook*, at the
  instruction boundary (the way Atari800's monitor does it). `run` returns
  from it and the CPU carries on. Commands that need a frame boundary
  (`load`, `reset`, `machine`, `loadstate`) `longjmp` to the main loop top,
  abandoning the partial frame, and run there.

Power-on is exact: before any frame runs, a8h saves the power-on state of
every machine × TV system to a temp dir; `load`/`reset`/`machine` restore it,
then reset what a state doesn't hold (SIO, POKEY RANDOM seed, R-Time 8 off,
frame/cycle counters). Two emulators, or one emulator before and after
running other programs, boot a program identically.

## Commands (protocol)

`cmd key=value key="quoted value" ...` → `OK key=value ...` or `ERR message`,
one line each. Numbers accept `123`, `$7F`, `0x7F`, `7Fh`, `%0101`.
`help` lists them. The Python client (`a8dev.emu`) is the supported way in;
docs/emulator-harness.md describes the API on top.

| Command | Arguments → reply |
|---|---|
| `status` | `mode=paused/free/running machine tv frame cycles midframe [line xpos] pc a x y s p log signals signal signal_frame fps hashing recording breakpoints watchpoints xe_banks cpu_bank turbo pid version loaded="" last_stop="…"` |
| `run` | `frames=N` (max frames; required ≥ 1), `steps=N`, `over=1`, `to=ADDR`, `at=CYCLE` (cycles since load), `until=HEX since=OFF`, `until_signal=1`, `nobreak=1` → `why=frames/frame_limit/break/watch/step/to/at/log/signal/cim ran=N frame cycles midframe [line xpos] pc a x y s p [id cond] [addr access insn_pc old value] [match] [signal] log fps speed` |
| `pause` / `resume [turbo=1]` | mode |
| `reset [hard=0]` | power-on (drives emptied) / the RESET key |
| `machine [name=] [tv=pal/ntsc] [basic=0/1]` | power-on as `400 800 600xl 800xl 1200xl 65xe 130xe 320xe` |
| `load path= [basic=0/1] [readonly=1]` | power-on + boot a XEX/ATR/CAR/BAS (Atari800's AFILE) → `type=` |
| `mount path= drive=1..8 [readonly=1]` | insert a disk without rebooting |
| `regs` / `setreg name= value=` | `pc a x y s p flags=NV-BDIZC` + position |
| `peek addr= len= [space=cpu/ram] [bank=N]` | `data=HEX`. cpu: what the CPU sees (I/O read without side effects); ram: RAM under the OS/BASIC ROMs and behind a mapped bank; bank: 130XE bank N (offset 0..$3FFF or $4000..$7FFF), mapped or not |
| `poke addr= data=HEX [space] [bank]` | cpu space: RAM, I/O registers (with their effect), or the ROM image |
| `find data=HEX [start end max space bank]` | `addrs=a,b,… count=` |
| `hw` | `antic.* gtia.* pokey.* pia.* mem.*` (write registers as last written, POKEY RANDOM counter, memory config) |
| `break id= [pc=] [cond=EXPR] [once=1]` | condition language below |
| `watch id= addr= [len=] [kind=r/w/rw]` | CPU data accesses through the operand (not DMA, not the stack): the run stops *after* the instruction, reporting `insn_pc`, `old` and `value` |
| `delete id=` / `delete all=1` / `breaks` | `list="id:break:pc:cond:hits;id:watch:lo-hi:kind:hits"` |
| `log [since= max=]` / `log_clear` | `base end since next data=HEX` (a 4 MB ring with absolute offsets) |
| `screen` / `palette` / `screen_hash` | the 384×240 frame buffer (palette indices, `vx1 vy1 vx2 vy2` = the visible area), the 256 RGB entries, FNV-1a of the buffer |
| `hash action=on/off/clear/get/status [since=]` | per-frame hashes: `first=F data=hhhhhhhh…` (entry i = frame count F+i) |
| `rec action=start/stop/get/status` | input changes per frame: `F:key:shift:consol:port0:port1:trig,…` |
| `input [key= shift= consol= port0= port1= trig=]` / `input raw=1 …` / `raw=0` | the harness layer (merged with the host's input) / forced input (replays) |
| `savestate path=` / `loadstate path=` | Atari800 state + `<path>.a8h` (frame counter, machine, program); `midframe=1` when taken mid-frame |
| `quit` | |

Condition expressions: registers `A X Y S P PC`, flags `N V B D I Z C`,
`FRAME LINE XPOS BANK`; numbers; `[addr]`/`PEEK(addr)`, `DPEEK(addr)`;
`= == != <> < > <= >=`, `+ - & | ^`, `AND OR NOT && || !`, parentheses. A
breakpoint is checked before each instruction; resuming from a stop does not
re-check the instruction it stopped on.

## Speed

Headless on an M-series Mac: ~27,000–34,000 frames/s idle (≈550–690× real
time), a few thousand frames/s with breakpoints (checked per instruction).

## Updating Atari800

1. Change `ATARI800_COMMIT` in `scripts/lib.sh`.
2. `scripts/build-emulator.sh`; if the patch no longer applies, fix it in
   `third_party/atari800-build/src-tree` and regenerate it there with
   `git diff -- src > ../../../emulator/patches/0001-a8h-harness-hooks.patch`.
3. `./a8 selftest` (`A8_TEST_WINDOW=1 .venv/bin/python tools/a8dev/tests/test_emulator.py` for the window path).

## 0002-sdl-keyboard-joystick-arrows.patch (window build only)

Atari800's keyboard joystick 0 defaults to the keypad (8 5 4 6) + Right Ctrl;
Mac laptops have neither. The patch makes it the arrow keys + Left Alt
(Option) for fire and keeps the keypad + Right Ctrl as second bindings
(`kbd_stick0_alt`). Those keys don't reach the Atari keyboard while the
keyboard joystick is on. Checked with real macOS key events in a window:
arrows move and Option fires in hello, keypad 4/6 move, Right Ctrl fires.
