# The a8dev SDK

`sdk/` is a C library for Atari 8-bit games, built with cc65 (target
`atari`: the OS ROM stays in, so one XEX runs on a 48K 400/800 and on
XL/XE/130XE machines). Every project links `build/a8sdk.lib`, so a program
only carries the modules it calls.

| Header | What |
|---|---|
| `a8hw.h` | hardware registers and OS shadows (ANTIC, GTIA, POKEY, PIA, page 2), the memory map, `A8_COLOR(hue, lum)` |
| `a8log.h` | `a8_log(fmt, ...)`, `a8_log_s`, `a8_log_c`, `a8_log_u`, `a8_done()`, `a8_fail(why)`, `a8_in_harness()` |
| `a8game.h` | frame timer, input, random numbers, POKEY sound + music, 8.8 fixed point |
| `a8gfx.h` | display lists for ANTIC 2/4/D/E/F + text rows, colours, plot/line/rect, text |
| `a8pmg.h` | player/missile graphics (sprites), collisions |
| `a8xe.h` | 130XE banked memory (4 x 16K) |
| `fix16.h`, `fixmath.h`, `vec3.h` | 16.16 fixed point and Q14 vectors from spectrum_dev (the amiga3d engine's maths, bit for bit) |

## Memory map (`sdk/cfg/a8.cfg`)

| Address | Use |
|---|---|
| `$0082-$00FF` | cc65 zero page |
| `$2000-$3FFF` | start-up code, then `LOWBSS`, `LOWCODE`, `LOWDATA`: below the 130XE bank window |
| `$2000-$8FFF` | program: code, rodata, data, bss (36K) |
| `$9000-$97FF` | the C stack (2K) |
| `$9800-$9FFF` | player/missile graphics (`PMBASE = $98`, single-line) |
| `$A000-$BFFF` | screen: bitmap/characters from `$A010`, text rows at `$BE10`, display list at `$BF00` |

`a8_crt_init` (sdk/src/crt.s, a constructor that runs before `main`)
moves the C stack to `$9000-$97FF`, lowers MEMTOP, and switches BASIC
off on XL/XE so `$A000-$BFFF` is RAM. On a 400/800, take the BASIC
cartridge out.

The OS screen editor's memory is inside our screen area: don't use
`printf`/`puts`/conio after `gfx_init`; use `text_print_at` and `a8_log`.

## Frame timer

`frame_init()` resets the counters. `frame_wait()` waits for the next
vertical blank (the OS VBI counts `RTCLOK2`), runs `sound_tick()` for each
frame that passed, and returns how many passed (1 = on time).
`frame_count`, `frame_overruns`. `frame_is_pal()`. 50 Hz in the harness
(PAL).

## Graphics

```c
gfx_init(GFX_160, 2);          /* 160x176 4-colour bitmap + 2 rows of text */
gfx_color(0, A8_COLOR(2, 8));  /* pixel value 1 */
gfx_line(0, 0, 159, 175, 3);
text_row_print(0, 0, "SCORE");
text_row_num(6, 0, score, 5);
```

| Mode | ANTIC | Size | Colours |
|---|---|---|---|
| `GFX_TEXT` | 2 | 40x24 characters | COLOR1 luminance on COLOR2 |
| `GFX_TEXT4` | 4 | 40x24 characters (custom font, `CHBAS`) | 4 + inverse |
| `GFX_160x96` | D | 160x96 | 4 (0 = COLOR4, 1..3 = COLOR0..2) |
| `GFX_160` | E | 160x192 | 4 |
| `GFX_320` | F | 320x192 | COLOR1 luminance on COLOR2 |

`text_rows` (0..6) adds 40-column ANTIC 2 rows below the main area; the
main area loses 8 scan lines per row. `gfx_line_addr[y]` is each line's
address (fast drawing). `gfx_print(bx, y, s, c)` draws text with the ROM
font into a bitmap.

## Player/missile graphics

```c
pmg_init();                         /* $9800, DMA on, players over playfield */
pmg_color(0, A8_COLOR(12, 10));
pmg_set(0, y, shape, 8);            /* vertical position = where the shape is */
pmg_x(0, x);                        /* horizontal: HPOSP0 */
if (pmg_hit_pf(0)) ...; pmg_hitclr();
```

The visible playfield is x 48..207 (colour clocks) and y 32..223 (scan
lines) for a 192-line screen (`PMG_X0`, `PMG_Y0`).

## Input

`input_read()` → `IN_LEFT/RIGHT/UP/DOWN/FIRE` (joystick 0, or W/A/S/D,
the arrow keys, SPACE), `IN_START/IN_SELECT/IN_OPTION` (console keys;
RETURN = START). `input_pressed()` = bits that went down since the
previous read. `joy_read(port)`, `console_read()`, `key_get()` /
`key_peek()` (OS key codes, `KEY_*`), `key_ascii(code)`. POKEY scans one
key at a time (plus SHIFT/CTRL).

## Sound (POKEY)

`sound_init()` (AUDCTL 0: 64 kHz clock), `snd_tone(ch, audf, dist, vol)`,
`snd_note(ch, midi, vol)` (MIDI 48..107), `snd_off(ch)`,
`snd_sfx(ch, audf, dist, vol, decay, sweep)` (decays each frame).
Music: `music_play(ch, track, vol)` with `a8_note {note, len}` lists
(`{0,0}` loops), `music_stop()`; ticked by `frame_wait()`.

## 130XE banks

```c
if (xe_bank_count() == 4) {
    xe_bank_write(2, 0x0000, level_data, 4096);
    xe_bank_read(2, 0x0000, buffer, 4096);
}
```

Banks 0..3 appear at `$4000-$7FFF` while selected (PORTB bits 2-3, bit 4 =
CPU, bit 5 = ANTIC). While a bank is mapped, main memory's `$4000-$7FFF` is
gone: the copy routines are in `LOWCODE` and the bounce byte in `LOWDATA`
(below `$4000`; project.mk checks), and buffers you copy to/from must be
outside the window too (`#pragma bss-name (push, "LOWBSS")`, or the
screen/PM area, or globals above `$8000`). See projects/banktest.

## Log

`a8_log("game: score=%u lives=%u\n", score, lives)` — each byte goes to
`$D1F0`, which the patched Atari800 captures (`./a8 log`, `emu_log`,
`emu_run_until`). On real hardware the stores do nothing. `A8_NO_LOG=1`
compiles the calls out. cc65's `%d/%u/%x` are 16-bit; use `%ld/%lu` with
`long`.

## Fixed point

`fix8` (8.8): `FIX(1.5)`, `fix_mul`, `fix_div`, `fix_sin/fix_cos` (256
steps a turn), `fix_atan2`, `isqrt16`. `fix16.h` (16.16, the amiga3d
engine's): `fmul`, `fdiv`, `fsqrt`, `fhypot`, `a3_sin/a3_cos`, `fatan2`...
`vec3.h`: Q14 vectors (`v3_dot`, `v3_cross`, `v3_norm`, `v3_mat`). cc65 has
no 64-bit integers; fmul/fdiv use spectrum_dev's 16 x 16 C model, checked
bit for bit there.

## Build rules (`sdk/project.mk`)

```make
NAME   := mygame
A8_SDK ?= ../../sdk
include $(A8_SDK)/project.mk
```

`src/*.c` and `src/*.s` are compiled (`cl65 -t atari -Oirs -g`), linked
with `sdk/cfg/a8.cfg` into `build/<NAME>.xex`, with `build/<NAME>.map`,
`.lbl` (VICE labels: symbols for the debugger) and `.dbg`. Overridable:
`OPT`, `CFG`, `START`, `EXTRA_CFLAGS`, `EXTRA_ASFLAGS`, `EXTRA_LDFLAGS`,
`EXTRA_OBJS`, `DEBUG=1`, `A8_NO_LOG=1`.
