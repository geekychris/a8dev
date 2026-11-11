# Harness, sessions and MCP

```
Claude Code ──stdio──> a8dev-mcp ─┐
browser ─────HTTP────> devbench ──┼──JSON lines──> session process ──harness protocol──> patched Atari800
./a8 ctl / scripts ───────────────┘   (a8dev serve)      (one Emulator)
```

- **Emulator** (`a8dev.emu.Emulator`, a8-emu's): starts a patched Atari800
  headless or in a window and drives it over its harness protocol
  (docs/emulator-harness.md): frames, the guest log, input, screenshots,
  memory, debugger, snapshots, recording.
- **Session** (`./a8 serve`, `./a8 run`, `emu_boot`): one process owning one
  Emulator, with a JSON-lines control port (`{"id":1,"cmd":"press","args":{"keys":"SPACE"}}`)
  and a registry file in `build/sessions/`. Many clients can share it; commands
  run one at a time. `./a8 sessions` lists them; `./a8 ctl help` lists commands.
- **MCP server** (`.venv/bin/a8dev-mcp`, server `a8`): the tools below, on the
  newest session or the one you `emu_boot`/`emu_attach`.
- **DevBench** (`./a8 devbench`, :3680): web UI + REST (`/api/...`) + the same
  MCP over streamable HTTP at `/mcp`.

## Tools

| Group | Tools |
|---|---|
| projects | `list_projects`, `build(project, clean)`, `new_project(name)`, `run_tests(project)` |
| sessions | `emu_boot(project, window, machine)`, `emu_attach(pid)`, `emu_sessions`, `emu_close`, `emu_status` |
| machine | `emu_load`, `emu_reset(hard)`, `emu_reload`, `emu_power_cycle`, `emu_reboot`, `emu_machine(130xe/800xl/65xe/600xl/1200xl/800/400/320xe)` |
| time | `emu_run(frames)`, `emu_run_until(text, max_frames)`, `emu_run_to(frame, cycle)`, `emu_position`, `emu_pause`, `emu_resume(turbo)` |
| log | `emu_log(all)` |
| screen | `emu_look(border, scale)`, `emu_screenshot(path)`, `emu_screen_hash`, `emu_record_video(path)`, `emu_record_gif` |
| input | `emu_press(keys)`, `emu_hold`, `emu_release`, `emu_type(text)`, `emu_joystick(dirs, port)`, `emu_sequence(steps)` |
| replay | `emu_record_input(action, path, from_reset)`, `emu_replay(path)`, `emu_input_log`, `emu_checkpoint`, `emu_save_replay` |
| debugger | `emu_break(addr, condition)`, `emu_watch(addr, length, kind)`, `emu_delete`, `emu_continue(max_frames)`, `emu_stepi`, `emu_step_over`, `emu_regs`, `emu_set_reg`, `emu_disasm`, `emu_backtrace`, `emu_symbols` |
| memory | `emu_read_mem(addr, length, bank)`, `emu_write_mem(addr, hex, bank)`, `emu_find_mem`, `emu_memory_map` |
| state | `emu_save_state(name)`, `emu_load_state(name)` |
| arcade | `arcade_games`, `arcade_picture`, `arcade_build`, `arcade_play(game)` |
| anything | `emu_cmd(cmd, args)` (`emu_cmd("help")`) |

Addresses take numbers, `$hex`, `0x..`, symbols from the `.lbl`/`.map`
(`main` or `_main`, `buf+16`) and register names. `bank=N` (0..3) reads a
130XE extended bank whether or not it is mapped.

## Writing tests

A project's `test.py` drives an `Emulator` directly (see
sdk/template/test.py and projects/banktest/test.py):

```python
with Emulator() as emu:
    emu.load(PROG)
    assert emu.run_until("game: ready", 500)
    emu.press("START")
    assert emu.run_until("game: level 1", 100)
    img = emu.screenshot("build/test.png")
```

Without a test.py, `./a8 test` passes when the program logs `A8:DONE`
(`a8_done()`) and fails on `A8:FAIL` (`a8_fail("why")`) or a timeout.
