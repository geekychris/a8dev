# Getting started

## Install

macOS (Apple Silicon or Intel), Linux (x86_64 or arm64), Windows via WSL2.

You need git, make, a C compiler (to build the emulator), Python 3.10+
(or uv) and a Docker engine (Docker Desktop, Rancher Desktop, OrbStack,
colima or docker-ce). SDL2 gives the emulator a window (`brew install sdl2`
/ `sudo apt install libsdl2-dev`); headless runs work without it.

```sh
git clone https://github.com/geekychris/a8dev.git && cd a8dev
./a8 doctor         # what's missing, a fix for each, and one install command for all of it
./a8 setup          # toolchain image, Atari800 (patched), .venv, then selftest
```

`./a8 doctor` knows Homebrew, apt (Debian/Ubuntu), dnf (Fedora), pacman
(Arch) and zypper (openSUSE). Typical fresh machines:

| Host | System packages |
|---|---|
| macOS | `xcode-select --install; brew install autoconf automake sdl2` + a Docker engine |
| Debian/Ubuntu | `sudo apt-get install -y git build-essential autoconf automake zlib1g-dev python3 python3-venv libsdl2-dev` + docker-ce (`sudo usermod -aG docker $USER`) |
| Fedora | `sudo dnf install -y git make gcc autoconf automake zlib-devel python3 SDL2-devel` + docker |
| Windows | `wsl --install -d Ubuntu`, then the Ubuntu line inside WSL; Docker Desktop with WSL integration for this distro (or docker-ce in WSL). The window needs WSLg (Windows 11). |

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

## Playing in the window

`./a8 run <project>` and `./a8 arcade` open Atari800 in a window:

| Key | Atari |
|---|---|
| arrow keys | joystick 1 (keypad 8 / 5 / 4 / 6 also work) |
| Option (Mac) / Left Alt | fire (Right Ctrl also works) |
| F4 / F3 / F2 | START / SELECT / OPTION |
| F5 | RESET (the arcade: back to the menu) |
| F1 | Atari800's menu (a second keyboard joystick, W A S D + Left Ctrl, can be enabled there) |
| F9 | quit |
| other keys | the Atari keyboard |

The arrow keys are the joystick, so they aren't the Atari's cursor keys in a
window; programs read the joystick (the SDK's `input_read()` does both).
Holding Option and pressing a letter is an Atari800 shortcut (e.g.
Option+F fullscreen), not fire + that key.

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
