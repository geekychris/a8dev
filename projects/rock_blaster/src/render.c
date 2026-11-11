/*
 * Rock Blaster - the Atari display.
 *
 * Game screen: an ANTIC 6 status line (20 columns, scan lines 16-23), the
 * world as a 160 x 108 ANTIC D bitmap (4 colours, rows of 2 scan lines: one
 * pixel is 2 x 2 world pixels, scan lines 24-239), and an ANTIC 6 message
 * line. The bitmap is at $A010-$B0EF, the display list at $9800 (RAM the
 * player/missile area doesn't use). ANTIC D fetches a row once for its two
 * lines: half the DMA of a 216-line ANTIC E screen, which left the CPU too
 * little time for the rocks.
 *
 * Rocks are XOR outlines (draw.s): large ones in COLPF0, medium and small in
 * COLPF1; bullets and explosion particles are COLPF2 pixels. A rock is
 * redrawn only when its pixel position changes, by erasing and drawing it
 * in one top-to-bottom sweep. When a frame runs short of time the remaining
 * rocks wait for the next one (the bullets and the ship never do). The ship
 * is players 0+1 (16 colour clocks wide), its flame players 2+3, from 64
 * pre-rotated pictures (tools/mkshapes.py).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "render.h"
#include "shapes.h"

#define BITMAP0   ((uint8_t *)0xA010)   /* rows 0-101 */
#define BITMAP1   ((uint8_t *)0xB000)   /* rows 102-107 (the 4K boundary) */
#define ROWS      108
#define DL_GAME   ((uint8_t *)0x9800)
#define DL_TITLE  ((uint8_t *)0x9AE0)   /* 32 bytes */
#define TITLE_MEM ((uint8_t *)0xA010)   /* the title's text, in the bitmap */

extern uint8_t xbyte[224], pm1[224], pm2[224], pm3[224], ylo[256], yhi[256];
extern uint8_t pd_on[64], bd_on[8];
extern void particles_draw(void);
extern void bullets_draw(void);
extern uint8_t plbuf[50];
extern const uint8_t *pl_verts;
extern void __fastcall__ pl_poly(uint8_t n);

/* ATASCII -> ANTIC screen code (port: not the SDK's text module, which
   brings the bitmap library with it: memory is tight here) */
static uint8_t scode(char c)
{
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return c;
}

static uint8_t hud_mem[40];             /* two ANTIC 6 lines */
static uint8_t ship_pic[50];            /* the ship's picture now */

extern uint8_t d_on[MAX_ROCKS];         /* draw.s: the rocks on the screen */
extern void rocks_sync(void);

static uint8_t *line_addr(uint8_t y)
{
    if (y < 102) return BITMAP0 + y * 40;
    return BITMAP1 + (y - 102) * 40;
}

static void build_tables(void)
{
    uint8_t i, x;
    uint8_t *a;
    for (i = 0; i < 224; ++i) {
        x = i < 32 ? i + 128 : (i - 32 >= 160 ? i - 192 : i - 32);   /* (i - 32) mod 160 */
        xbyte[i] = x >> 2;
        pm1[i] = 0x40 >> ((x & 3) << 1);
        pm2[i] = 0x80 >> ((x & 3) << 1);
        pm3[i] = 0xC0 >> ((x & 3) << 1);
    }
    i = 0;
    do {
        x = (uint8_t)((i + ROWS * 2 - 16) % ROWS);                  /* (i - 16) mod 108 */
        a = line_addr(x);
        ylo[i] = (uint8_t)(uint16_t)a;
        yhi[i] = (uint16_t)a >> 8;
    } while (++i);
}

static void build_game_dl(void)
{
    uint8_t *p = DL_GAME, i;
    *p++ = 0x70;
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)hud_mem; *p++ = (uint16_t)hud_mem >> 8;
    *p++ = 0x4D; *p++ = 0x10; *p++ = 0xA0;
    for (i = 1; i < 102; ++i) *p++ = 0x0D;
    *p++ = 0x4D; *p++ = 0x00; *p++ = 0xB0;      /* the 4K boundary */
    for (i = 103; i < ROWS; ++i) *p++ = 0x0D;
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)(hud_mem + 20); *p++ = (uint16_t)(hud_mem + 20) >> 8;
    *p++ = 0x41; *p++ = 0x00; *p = 0x98;
}

/* title: the name in ANTIC 7, then 18 ANTIC 6 / 2 lines */
static void build_title_dl(void)
{
    uint8_t *p = DL_TITLE, i;
    uint16_t a = (uint16_t)TITLE_MEM;
    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;
    *p++ = 0x47; *p++ = (uint8_t)a; *p++ = a >> 8;           /* 20 bytes */
    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;
    *p++ = 0x06;                                             /* 20 */
    *p++ = 0x70;
    for (i = 0; i < 14; ++i) *p++ = 0x02;                    /* 14 x 40 */
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_TITLE; *p = (uint16_t)DL_TITLE >> 8;
}

void render_init(void)
{
    build_tables();
    build_game_dl();
    build_title_dl();
    pmg_init();
    GPRIOR = 1;
}

static void show_dl(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x1C;
}

void render_game_screen(void)
{
    SDMCTL = 0;
    frame_wait();
    memset(BITMAP0, 0, ROWS * 40);
    memset(d_on, 0, sizeof(d_on));
    memset(pd_on, 0, sizeof(pd_on));
    memset(bd_on, 0, sizeof(bd_on));
    ship_pic[12] = 0;
    memset(hud_mem, 0, sizeof(hud_mem));
    COLOR4 = A8_COLOR(8, 0);            /* the original's dark blue */
    COLOR0 = A8_COLOR(0, 12);           /* large rocks */
    COLOR1 = A8_COLOR(7, 8);            /* medium and small */
    COLOR2 = A8_COLOR(1, 14);           /* bullets, particles */
    COLOR3 = 0x0E;                      /* text */
    CHBAS = 0xE0;
    pmg_color(0, 0x0E);
    pmg_color(1, 0x0E);
    pmg_color(2, A8_COLOR(2, 10));      /* the flame */
    pmg_color(3, A8_COLOR(2, 10));
    render_hide_ship();
    show_dl(DL_GAME);
}

void render_title_screen(void)
{
    render_hide_ship();
    COLOR4 = A8_COLOR(8, 0);
    COLOR0 = A8_COLOR(0, 12);           /* ANTIC 7/6: colour 0 */
    COLOR1 = A8_COLOR(1, 14);           /* ... colour 1; ANTIC 2: luminance */
    COLOR2 = A8_COLOR(8, 0);            /* ANTIC 2 background */
    COLOR3 = A8_COLOR(7, 10);
    CHBAS = 0xE0;
    pmg_color(0, 0x0E);
    pmg_color(1, 0x0E);
    show_dl(DL_TITLE);
}

void title_clear(void)
{
    memset(TITLE_MEM, 0, 20 + 20 + 14 * 40);
}

/* row 0: the name (ANTIC 7, colour in the top bits), row 1: ANTIC 6,
   rows 2..15: ANTIC 2 */
void title_print(uint8_t row, uint8_t col, const char *s)
{
    uint8_t *d = TITLE_MEM + (row == 0 ? 0 : row == 1 ? 20 : 40 + (row - 2) * 40) + col;
    uint8_t hi = row == 0 ? 0x00 : row == 1 ? 0x40 : 0;
    while (*s) *d++ = scode(*s++) | hi;
}

/* ------------------------------------------------------------ status */

void hud_clear(uint8_t line) { memset(hud_mem + line * 20, 0, 20); }

void hud_print(uint8_t line, uint8_t x, const char *s)
{
    uint8_t *d = hud_mem + line * 20 + x;
    while (*s) *d++ = scode(*s++) | 0xC0;          /* COLPF3 */
}

static const uint16_t p10[4] = { 1000, 100, 10, 1 };

void hud_num(uint8_t line, uint8_t x, uint32_t v, uint8_t digits)
{
    uint8_t *d = hud_mem + line * 20 + x, i, c;
    uint16_t w;
    char buf[7];
    c = '0'; while (v >= 100000UL) { v -= 100000UL; ++c; } buf[0] = c;
    c = '0'; while (v >= 10000UL) { v -= 10000UL; ++c; } buf[1] = c;
    w = (uint16_t)v;
    for (i = 0; i < 4; ++i) { c = '0'; while (w >= p10[i]) { w -= p10[i]; ++c; } buf[2 + i] = c; }
    buf[6] = 0;
    for (i = 6 - digits; i < 6; ++i) *d++ = scode(buf[i]) | 0xC0;
}

/* ------------------------------------------------------------ the ship */

static uint8_t ship_shown_y, ship_shown, flame_shown, ship_shown_f;

#define PL(n) ((uint8_t *)(0x9C00 + ((n) << 8)))

void render_hide_ship(void)
{
    if (ship_shown) {
        memset(PL(0) + ship_shown_y, 0, SHIP_H);
        memset(PL(1) + ship_shown_y, 0, SHIP_H);
    }
    if (flame_shown) {
        memset(PL(2) + ship_shown_y, 0, SHIP_H);
        memset(PL(3) + ship_shown_y, 0, SHIP_H);
    }
    ship_shown = flame_shown = 0;
}

/* the outline for frame f (0..63) into plbuf */
static void outline(const uint8_t *verts, uint8_t n, uint8_t f)
{
    memset(plbuf, 0, 50);
    pl_verts = verts + f * (n * 2);
    pl_poly(n);
}


static void ship_draw(uint8_t visible, uint8_t flame)
{
    uint8_t y = (uint8_t)(SCAN_TOP + ship_py - 12);
    uint8_t x = (uint8_t)(PMG_X0 + (ship_px >> 1) - 8);
    uint8_t f = ship_angle >> 2;

    if ((ship_shown || flame_shown) && y != ship_shown_y) render_hide_ship();
    pmg_x(0, x);
    pmg_x(1, x + 8);
    pmg_x(2, x);
    pmg_x(3, x + 8);
    if (!visible) {
        render_hide_ship();
        return;
    }
    if (f != ship_shown_f || !ship_pic[12]) {
        outline(ship_verts, SHIP_VERTS, f);
        memcpy(ship_pic, plbuf, 50);
        ship_shown_f = f;
        ship_shown = 0;
    }
    if (!ship_shown) {
        memcpy(PL(0) + y, ship_pic, SHIP_H);
        memcpy(PL(1) + y, ship_pic + SHIP_H, SHIP_H);
    }
    if (flame) {
        outline(flame_verts, FLAME_VERTS, f);
        memcpy(PL(2) + y, plbuf, SHIP_H);
        memcpy(PL(3) + y, plbuf + SHIP_H, SHIP_H);
    } else if (flame_shown) {
        memset(PL(2) + y, 0, SHIP_H);
        memset(PL(3) + y, 0, SHIP_H);
    }
    ship_shown = 1;
    flame_shown = flame;
    ship_shown_y = y;
}

/* the title's ship, turning (players 0+1 at double width) */
void title_ship(uint8_t angle)
{
    pmg_size(0, 1);
    pmg_size(1, 1);
    pmg_x(0, PMG_X0 + 80 - 16);
    pmg_x(1, PMG_X0 + 80);
    outline(ship_verts, SHIP_VERTS, angle >> 2);
    memcpy(PL(0) + 60, plbuf, SHIP_H);
    memcpy(PL(1) + 60, plbuf + SHIP_H, SHIP_H);
    ship_shown = 1;
    ship_shown_y = 60;
}

/* ------------------------------------------------------------ the world */

void render_world(void)
{
    bullets_draw();
    particles_draw();
    pmg_size(0, 0);
    pmg_size(1, 0);
    ship_draw(ship_alive && gs_state != STATE_TITLE && !(ship_invuln && (gs_frame & 4)),
              ship_thrust_on && (gs_frame & 2));
    rocks_sync();       /* draw.s */
}
