# Atari 8-bit development workspace (a8dev)

Homebrew for the Atari 400/800, XL/XE and the 128K 130XE (6502 @ 1.77 MHz
PAL, ANTIC display lists, GTIA player/missile graphics, POKEY sound). C and
asm built with cc65 (and MADS) in Docker; runs in a patched Atari800 (free
AltirraOS ROMs) that we can script deterministically.

## Commands

```sh
./a8 build <project>        # -> projects/<p>/build/<p>.xex + .map + .lbl (symbols) + .dbg
./a8 test <project>         # projects/<p>/test.py if present, else waits for A8:DONE / A8:FAIL
./a8 log <project> --frames 600 [--press SPACE@400]
./a8 shot <project> -o out.png --until "ready" [--press START@100] [--scale 2]
./a8 video <project> -o out.gif|.mp4 --until "ready" --frames 150
./a8 log <project> --press RETURN@200 --hold LEFT@250 --release ALL@300 --type "hi@320"
./a8 record <project> -o r.json --press START@200   # input recorder + per-frame hashes
./a8 replay r.json --runs 2 # re-run it twice: every frame hash must match
./a8 run <project>          # window for the human (free-running; registered session)
./a8 serve <project>        # headless paused session (prints ports)
./a8 ctl <cmd> key=value    # remote-control the newest session (./a8 ctl help)
./a8 new <name>             # from sdk/template
./a8 selftest               # build + test everything (do this before saying "done")
./a8 doctor                 # environment problems
./a8 devbench               # web UI + REST (/api) + MCP over HTTP (/mcp) on :3680
```

Builds run in Docker (image `a8-dev-toolchain:N`, cc65 + MADS, pinned in
scripts/lib.sh); `A8_NATIVE=1` builds with a cc65 on PATH instead (2.18
works).

## MCP tools (server "a8", .mcp.json)

Inner loop: `build` -> `emu_boot(project)` -> `emu_run_until("<your log line>")` ->
`emu_look` / `emu_press` / `emu_log`. `emu_boot(p)` = private headless session
(paused: frames advance only on run/press/continue); `emu_boot(p, window=True)` =
user-visible, free-running; `emu_attach()` = join `./a8 run X`. Also `emu_run(frames)`,
`emu_hold`/`emu_release`, `emu_type`, `emu_sequence`, `emu_joystick(dirs, port)`,
`emu_pause`/`emu_resume`, `emu_load`, `emu_machine`, `emu_save_state`/`emu_load_state`,
`emu_screenshot(path)`. Restart: `emu_reset(hard)`, `emu_reload`, `emu_power_cycle`,
`emu_reboot`. Screens: `emu_look`, `emu_record_video(path.gif|.mp4)`, `emu_screen_hash`.
Recording: `emu_record_input("start"|"stop"|"save", path, from_reset)` + `emu_replay(path)`.

Window keys (the user): arrows = joystick, Option/Left Alt = fire, F4 START, F3 SELECT,
F2 OPTION, F5 RESET, F9 quit (emulator/patches/0002).

Keys: `A`-`Z` `0`-`9` `SPACE` `RETURN` `ESC` `TAB` `DELETE`, `SHIFT+A`, `CTRL+A`;
console `START` `SELECT` `OPTION`; joystick 0 `UP` `DOWN` `LEFT` `RIGHT` `FIRE`
(`UP+FIRE`).

Debugging: `emu_break("main")` (C names: `main` or `_main`), `emu_continue(max_frames)`,
`emu_watch("global", 2, "w")`, `emu_stepi`, `emu_step_over`, `emu_regs`, `emu_set_reg`,
`emu_disasm`, `emu_symbols`, `emu_read_mem("sym")`, `emu_write_mem`, `emu_find_mem`,
`emu_backtrace`, `emu_read_mem(offset, bank=N)` (130XE banks 0..3, mapped or not),
`emu_memory_map` (PORTB). `emu_cmd("help")` lists every session command.
Details: docs/mcp.md, docs/emulator-harness.md.

- Frames are 1/50 s of guest time (PAL 130XE by default).
- `emu_run_until` only matches output written after the last `emu_log` call, the last
  load/reset, or its own previous match.
- Make programs observable: log state changes with `a8_log("tag: key=%u\n", v)`.

## Rules learned the hard way

- **Harness log channel**: one char per store to `$D1F0` (`A8_LOG_ADDR`), a status
  code to `$D1F1` (`A8_SIGNAL_ADDR`), defined only in sdk/include/a8log.h. It's the
  PBI page, ignored by stock machines (not `$D6xx`: Covox). Release builds
  (`RELEASE=1`, `A8_RELEASE`, `NDEBUG`, `A8_NO_LOG`) compile every log call away, so
  shipped games never touch it. Never write these addresses directly.

- **Memory map is fixed** (sdk/cfg/a8.cfg): program `$2000-$8FFF`, C stack
  `$9000-$97FF`, PMG `$9800`, screen `$A000-$BFFF`. `a8_crt_init` moves cc65's
  stack (it starts at MEMTOP, inside our screen) and switches BASIC off on XL/XE.
- **No printf/puts/conio after `gfx_init`**: the OS screen editor's memory is
  inside our screen area. Use `text_print_at` and `a8_log`.
- **130XE banks**: while a bank is mapped, `$4000-$7FFF` of main memory is
  invisible. Code that runs then, and the buffers it copies, must be outside the
  window: `LOWCODE`/`LOWDATA`/`LOWBSS` (project.mk checks they end below `$4000`).
  A big program's BSS lands in the window: put bank buffers in LOWBSS
  (`#pragma bss-name (push, "LOWBSS")`).
- **ANTIC can't cross a 4K boundary** inside a line of screen memory; bitmaps start
  at `$A010` so the boundary falls between lines 101 and 102 and the display list
  reloads the address there. Display lists must not cross a 1K boundary.
- **cc65 versions**: the pinned cc65 (2.19 git) calls the C stack pointer `c_sp`
  and wants `INIT` as bss; 2.18 says `sp` and `INIT` rw. crt.s and project.mk
  handle both (`ca65 .version` is `$0213` for 2.19 git but `$0320` for 2.18).
- cc65 printf: `%d/%u/%x` are 16-bit; use `%ld/%lu` with `long`. `a8_log` output is
  cut at 127 characters.
- Write the OS shadows (COLOR0..4 `$2C4`, SDMCTL `$22F`, PCOLR0 `$2C0`, GPRIOR
  `$26F`, CHBAS `$2F4`), not the chip registers: the OS VBI copies the shadows
  every frame. HPOSPn, SIZEPn and POKEY have no shadows.
- POKEY reads one key at a time (plus SHIFT/CTRL); joystick and console keys are
  separate, so `UP+FIRE+START` works but two letters at once don't.
- `-g` is always on: it only adds symbols to the .lbl/.dbg, not code.
- cc65 C costs ~100 cycles a byte in simple loops; inner loops go in asm (see
  projects/banktest/src/page.s) or use memcpy/memset/memcmp. Avoid 16-bit maths per byte.
