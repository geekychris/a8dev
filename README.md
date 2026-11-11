# a8dev

End-to-end development of Atari 8-bit programs (400/800, XL/XE, 128K 130XE)
on macOS, Linux and Windows (WSL2): C and assembly built with
[cc65](https://cc65.github.io/) (and MADS) in Docker, run in a patched
[Atari800](https://atari800.github.io/) that scripts, agents and
[Claude Code](https://claude.com/claude-code) can drive deterministically
(MCP server, web DevBench, REST).

In the spirit of the sibling projects for the ZX Spectrum, the Amiga, the
Atari ST and the 3DO.

```sh
./a8 setup                 # toolchain image, emulator, Python env (idempotent)
./a8 new mygame            # a project from sdk/template
./a8 build hello           # -> projects/hello/build/hello.xex (+ .map, .lbl)
./a8 test hello            # headless, deterministic
./a8 shot hello -o hello.png
./a8 run hello             # in a window
./a8 devbench              # web UI + REST + MCP over HTTP on :3680
```

- `sdk/` – the C library and headers for games (frame timer, display lists,
  player/missile graphics, POKEY sound and music, input, fixed point,
  130XE banks, the harness log). See [docs/sdk.md](docs/sdk.md).
- `docs/` – getting started, SDK reference, the harness and MCP tools,
  Atari 8-bit gotchas.
- `projects/` – programs and games.

The Atari OS used is AltirraOS, the free replacement built into Atari800; no
copyrighted ROMs are needed or downloaded.

MIT licensed (see LICENSE). Atari800 is GPL-2.0; it is fetched into
`third_party/` and patched at build time, not distributed here.
