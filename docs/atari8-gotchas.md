# Atari 8-bit gotchas

Things that cost time, so they don't again.

## Toolchain

- **cc65 2.19 git vs 2.18**: the C stack pointer is `c_sp` in newer cc65 and
  `sp` in 2.18 (2.19 git still accepts `sp` with a deprecation warning), and
  `atari.cfg`'s `INIT` segment changed from `rw` to `bss, define = yes`
  (exehdr.s needs `__INIT_LOAD__`). `ca65`'s `.version` changed encoding:
  2.18 reports `$0320`, 2.19 git `$0213`. sdk/src/crt.s and project.mk handle both.
- `cl65` has no `--dbgfile`: pass it to the linker (`-Wl --dbgfile,file.dbg`).
- cc65 has no 64-bit integers: fix16's fmul/fdiv use 16 x 16 multiplies
  (fix16_model.h from spectrum_dev).
- Large numeric constants in tables need an `L` suffix to avoid a warning per
  entry ("Integer constant implies signed long").
- A function and a global can't share a name (cc65: "Redefinition ... as
  different kind of symbol"); `gfx_text` (the text rows pointer) vs
  `gfx_print` (draw text).
- `ld65` map files list segments twice (per module and in "Segment list");
  parse only lines whose start column is 6 hex digits.

## Memory

- cc65's `atari` crt0 starts the C stack at MEMTOP, which with BASIC off is
  inside `$A000-$BFFF`; a8_crt_init moves it to `$9000` before `main`.
- With BASIC enabled (XL/XE default), `$A000-$BFFF` is ROM. a8_crt_init sets
  PORTB bit 1 (BASIC off). On a 400/800 PORTB is the joystick 3/4 port set as
  input, so the same write is harmless.
- 130XE: PORTB bits 2-3 = bank, bit 4 = 0 → the CPU sees the bank at
  `$4000-$7FFF`, bit 5 = 0 → ANTIC does. Keep bit 5 = 1 unless the display
  comes from a bank. A program bigger than 8K has code/BSS in the window:
  bank helpers and their buffers live in LOWCODE/LOWDATA/LOWBSS.

## Display

- ANTIC's memory scan counter can't cross a 4K boundary within a mode line;
  a 7680-byte bitmap needs a second LMS. Start it at `$A010` (40 * 102 = 4080)
  so line 102 begins exactly at `$B000`.
- A display list must not cross a 1K boundary (we put it at `$BF00`).
- Change the display list with screen DMA off for a frame (gfx_init does),
  otherwise ANTIC can read half a list.
- Write shadows (COLOR0-4, SDMCTL, SDLSTL, PCOLR0-3, GPRIOR, CHBAS); the OS
  VBI copies them to the chips each frame and overwrites direct writes.
- Mode E/D pixel value 0 is COLOR4 (background), 1..3 = COLOR0..COLOR2.

## Input

- POKEY scans one key (plus SHIFT/CTRL/BREAK). `CH` ($2FC) latches the last
  key code until cleared; `SKSTAT` bit 2 = 0 while a key is held, `KBCODE` =
  which. Console keys (CONSOL) and joysticks (PIA, via STICK0/STRIG0
  shadows) are separate hardware, so they combine freely.

## Speed

- cc65 C is slow for byte loops: a pattern computed per byte with 16-bit
  arithmetic (`(i * 7 + b * 31 + (i >> 8)) & 0xFF`) took banktest 5,200 frames
  (100 s) for 64K. A table plus one add per byte in C was ~100 cycles a byte;
  an indexed asm loop (`lda tab,y / adc / sta buf,y / iny / bne`) is ~16. The
  64K fill + verify now takes 300 frames. Keep inner loops in asm, use 8-bit
  indexes, and use memcpy/memcmp/memset (asm in cc65's library).
- ANTIC's screen DMA takes ~30% of the CPU with a text screen; `gfx_off()`
  around a long computation gets it back.
- A DLI that stores to `WSYNC` stops the CPU until the end of the scan line
  (~100 cycles). One on every ANTIC D row took ~30% of a frame (a8-big):
  set the DLI bit only on the rows where a colour changes.
- The SDK's `sound_tick` was C with struct-pointer access: ~1,900 cycles a
  frame even when silent, 16-23% of boing_twist's CPU. It's asm now (SDK 1.2).

## More memory

- `LOWRAM` ($0C00-$1FFF, 5K, SDK 1.2): for programs loaded without DOS.
- The 14K under the OS ROM ($C000-$CFFF, $D800-$FFFF): switch the ROM off
  (PORTB bit 0 = 0) and supply your own NMI/IRQ handlers, copying the
  shadows to the chips in your VBI. Game-local example:
  projects/boing_twist/src/os.s (a8-big). The SDK's frame timer, input and
  sound read OS shadows/RTCLOK, so keep those updated in your handler.
- The 130XE banks (a8xe.h): 64K of data.
