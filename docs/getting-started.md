# Getting started

## Install

macOS (Apple Silicon or Intel), Linux (x86_64 or arm64), Windows via WSL2.

You need git, make, a C compiler (to build the emulator), Python 3.10+
(or uv) and a Docker engine (Docker Desktop, Rancher Desktop, OrbStack,
colima or docker-ce). SDL2 gives the emulator a window (`brew install sdl2`
/ `sudo apt install libsdl2-dev`); headless runs work without it.

```sh
git clone https://github.com/geekychris/a8dev.git && cd a8dev
./a8 setup          # toolchain image, Atari800 (patched), .venv, then selftest
./a8 doctor         # what's missing, with a fix for each problem
```

No Atari ROMs are needed: Atari800 has AltirraOS (a free OS and BASIC
replacement) built in.

No Docker? Install cc65 (`brew install cc65`, `apt install cc65`) and
build with `A8_NATIVE=1 ./a8 build hello` (also the fallback when Docker
isn't running).

## First program

```sh
./a8 new mygame                 # projects/mygame from sdk/template
./a8 build mygame               # projects/mygame/build/mygame.xex
./a8 test mygame                # its test.py, headless
./a8 shot mygame -o shot.png --until "mygame: ready"
./a8 run mygame                 # play it in a window
```

`src/main.c` has a 50 Hz main loop (`frame_wait()`), a text screen, a
sprite moved with the joystick or W/A/S/D, and `a8_log` lines that the
test waits for. Read docs/sdk.md for the library.

## With Claude Code

Open the repo in `claude`. `.mcp.json` registers the `a8` MCP server
(`.venv/bin/a8dev-mcp`), so Claude can build, boot the emulator headless,
press keys, take screenshots, set breakpoints and read memory. Or run
`./a8 devbench --open` for the web UI, REST API and MCP over HTTP at
`http://127.0.0.1:3680/mcp`.

## Running the XEX elsewhere

The `.xex` is a standard Atari executable: load it from DOS (DOS 2.5,
SpartaDOS, MyDOS…), Altirra, or a stock Atari800 (`atari800 -xe -nobasic
-run build/mygame.xex`). It needs 48K, BASIC off (XL/XE: we switch it off
ourselves; 400/800: remove the cartridge), and for bank switching a 130XE
or compatible.
