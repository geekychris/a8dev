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
- [ ] Patched Atari800 + emu.py harness client (a8-emu).
- [ ] hello build → test → shot and MCP end to end; banktest passes.
- [ ] selftest suites (features, remote/MCP, template).
- [ ] Arcade (ATR menu).
