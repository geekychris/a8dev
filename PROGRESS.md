# Progress

Phase 1: infrastructure (emulator harness, toolchain, CLI + MCP, SDK,
docs). Porting the Spectrum games is phase 2.

## Phase 1

- [x] Repo, MIT licence, private GitHub repo geekychris/a8dev.
- [x] Toolchain image a8-dev-toolchain:1: cc65 (commit eebf54a, 2.19 git) and
      MADS 2.1.9 built from source on debian:bookworm-slim, native arm64/amd64.
      `bin/a8-make` runs make in it; `A8_NATIVE=1` (or no Docker) uses a cc65 on
      PATH (2.18 checked).
- [x] SDK: a8.cfg memory map + crt init; log ($D1F0 chars, $D1F1 status; compiled out with RELEASE=1), frame timer, input,
      rnd, POKEY sound/sfx/music, display lists for ANTIC 2/4/D/E/F + text
      rows, plot/line/rect/text, PMG, 130XE banks, fix8, fix16/vec3 from
      spectrum_dev; project.mk with the bank-window check.
- [x] projects/hello (text + PMG + sound + log), projects/banktest (130XE),
      sdk/template.
- [x] `./a8` CLI and the a8dev Python package (session, control, CLI, MCP
      server, DevBench :3680) ported from spectrum_dev's zxdev.
- [x] Patched Atari800 7.2.1 + emu.py harness client (a8-emu, PR #8); window
      keyboard joystick = arrows + Option (patch 0002), SDL2 window fix (0003).
- [x] hello build → test → shot and MCP end to end; banktest (300 frames for 64K).
- [x] selftest: projects + test_emulator (109) + test_remote (56) + test_template (7).
- [x] Arcade: DOS-less ATR boot loader, menu with pictures, RESET back to the menu.
- [x] Fresh machines: ./a8 doctor (brew/apt/dnf/pacman/zypper, WSL2); full
      doctor → install → setup → selftest green in a fresh debian:bookworm
      container (arm64, no Docker, apt's cc65 2.18). Fixed the emulator's
      Linux/GCC build (configure's -Werror vs a ULONG redefinition).

## Phase 2: game ports (a8-arcade, a8-adv, a8-3d, a8-big)
