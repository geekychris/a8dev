/*
 * bt.h - Boing Twist for the Atari 130XE: the memory map and shared parts.
 *
 *   $0600-$062F   bank select code (only while the XEX loads)
 *   $2000-$8FFF   the program (LOWCODE/LOWDATA first, below $4000)
 *   $9000-$97FF   C stack;  $9800-$9FFF players and missiles
 *   $A000-$A03F   a row of zeros (rows with no ball show it)
 *   $A040-$AA3F   the ball's picture: 64 rows of 40 bytes, ANTIC mode D
 *   $AA60-$AC5F   the picture, packed (64 x 32 texels, 2 bits each)
 *   $AC60-$AE3F   the DLI's colours per row: background, PF0-2, shadow
 *   $B000-$B5FF   the flip's texture (16 rows of 96)
 *   $B800-$BBFF   the font (the OS ROM's, copied: the ROM is off)
 *   $BC00-$BD3F   the display list;  $BD40-$BD8F two text rows
 *   $BDA0-$BF9F   a twisting band's rows (A copy, 64 bytes a row)
 *
 *   (the OS ROM is switched off: src/os.s)
 *   $C000-$CFFF   the game's state (HIBSS: the puzzle, the move queue)
 *   $D800-$E3FF   the ball's texture: 32 rows of 96 bytes (64 texels and
 *                 the first 32 again, so the turn can be added in Y),
 *                 colour << 6
 *   $E400-$FFF9   the rules' code (bt_core.c), copied from bank 2
 *
 * Banks (gfx/bank0-3.bin, from tools/gen_data.py, loaded by the XEX):
 *   0 the play view's code, 1 the flip view's, 2 the title's and the
 *   rules' code (copied up at start-up),
 *   3 the flip's table and the 17 pictures.
 */
#ifndef BT_H
#define BT_H
#include <stdint.h>
#include "gfx_data.h"

#define TEX_A      ((uint8_t *)TEX_A_ADDR)
#define ZERO_ROW   ((uint8_t *)0xA000)
#define BALL_BUF   ((uint8_t *)BUF_ADDR)
#define PIC_BUF    ((uint8_t *)0xAA60)
#define TAB_BK     ((uint8_t *)0xAC60)
#define TAB_C0     (TAB_BK + 96)
#define TAB_C1     (TAB_BK + 192)
#define TAB_C2     (TAB_BK + 288)
#define TAB_SH     (TAB_BK + 384)
#define DLIST      ((uint8_t *)0xBC00)
#define TEXT_ROWS  ((uint8_t *)0xBD40)
#define SAVED_BAND ((uint8_t *)0xBDA0)

/* render.s */
extern void disp_start(void);
extern void disp_stop(void);
extern void __fastcall__ ball_draw(uint8_t bank);
extern uint8_t __fastcall__ bank_ok(uint8_t bank);
extern void flip_build(void);
extern void os_off(void);
extern void band_rotate(void);
extern uint8_t ball_turn, hscrol_next, rot_v0, rot_n, rot_shift;

#endif
