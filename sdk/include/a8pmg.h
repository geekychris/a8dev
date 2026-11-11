/*
 * a8pmg.h - player/missile graphics (hardware sprites).
 *
 * Four players (8 pixels wide, up to 256 lines tall, one colour each) and
 * four missiles (2 pixels). pmg_init() uses the fixed 2K area at $9800
 * (single-line resolution: one byte per scan line) and turns PM DMA on.
 * Vertical position = where the shape is in the player's memory; horizontal
 * = HPOSPn. Visible area: x 48..207 (normal playfield), y 32..223 on
 * scan lines (a GRAPHICS 0/8 screen starts at scan line 32).
 */
#ifndef A8PMG_H
#define A8PMG_H
#include <stdint.h>
#include "a8hw.h"

#define PMG_X0   48       /* left edge of the 160-pixel (color clock) playfield */
#define PMG_Y0   32       /* first visible line of a 192-line screen */

extern void pmg_init(void);                /* clear PM memory, PMBASE, DMA + GRACTL on */
extern void pmg_off(void);
extern uint8_t *pmg_player_mem(uint8_t n); /* 256 bytes for player n */
extern uint8_t *pmg_missile_mem(void);     /* missile n = bits 2n..2n+1 */
/* draw `h` bytes of `shape` at line y (clears the old shape of the same height) */
extern void pmg_set(uint8_t n, uint8_t y, const uint8_t *shape, uint8_t h);
extern void pmg_clear(uint8_t n);
#define pmg_x(n, x)       (*(volatile uint8_t *)(0xD000 + (n)) = (x))
#define pmg_missile_x(n, x) (*(volatile uint8_t *)(0xD004 + (n)) = (x))
#define pmg_color(n, c)   (*(volatile uint8_t *)(0x2C0 + (n)) = (c))
#define pmg_size(n, s)    (*(volatile uint8_t *)(0xD008 + (n)) = (s))   /* 0 normal, 1 double, 3 quad */
#define pmg_priority(p)   (GPRIOR = (p))   /* 1 = players over playfield, 4 = playfield over players */
/* collisions since the last pmg_hitclr(): bits = playfields / players hit */
#define pmg_hit_pf(n)     (*(volatile uint8_t *)(0xD004 + (n)) & 0x0F)
#define pmg_hit_pl(n)     (*(volatile uint8_t *)(0xD00C + (n)) & 0x0F)
#define pmg_missile_hit_pf(n) (*(volatile uint8_t *)(0xD000 + (n)) & 0x0F)
#define pmg_missile_hit_pl(n) (*(volatile uint8_t *)(0xD008 + (n)) & 0x0F)
#define pmg_hitclr()      (HITCLR = 0)

#endif
