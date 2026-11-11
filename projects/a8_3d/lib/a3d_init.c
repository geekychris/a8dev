/* a3d_init.c - display lists, double buffering, projection table, timer. */
#include <string.h>
#include "a8game.h"
#include "a3d_int.h"

#define BUF0   0xA010u
#define DL0    ((uint8_t *)(a3d_double ? 0xA800 : 0xAF10))
#define DL1    ((uint8_t *)(a3d_double ? 0xB800 : 0xBF10))

uint8_t a3d_h;
#ifdef A3D_LOWTAB
#pragma bss-name (push, "LOWRAM")
#endif
uint8_t a3d_ylo[96], a3d_yhi[96];     /* line addresses of buffer 0 (buffer 1: high | $10) */
#ifdef A3D_LOWTAB
#pragma bss-name (pop)
#endif
uint8_t a3d_back;
uint8_t a3d_double;                   /* set before a3d_init: every line shown twice */

static void wait_vbl(void)
{
    uint8_t t = RTCLOK2;
    while (RTCLOK2 == t)
        ;
}

/* 24 blank lines, LMS + mode D lines (doubled: an LMS for each of the two
   showings of every line), ANTIC 2 text rows, JVB */
static void build_dl(uint8_t *d, uint8_t hi, uint8_t lines, uint8_t rows)
{
    uint8_t *start = d, i;
    uint16_t a = ((uint16_t)hi << 8) | 0x10;
    d[0] = d[1] = d[2] = 0x70;
    if (a3d_double) {
        d += 3;
        for (i = 0; i < lines; ++i, a += 40) {
            d[0] = d[3] = 0x4D;
            d[1] = d[4] = (uint8_t)a;
            d[2] = d[5] = (uint8_t)(a >> 8);
            d += 6;
        }
    } else {
        d[3] = 0x4D; d[4] = 0x10; d[5] = hi;
        memset(d + 6, 0x0D, lines - 1);
        d += 5 + lines;
    }
    if (rows) {
        d[0] = 0x42; d[1] = 0x80; d[2] = 0xAF;
        memset(d + 3, 0x02, rows - 1);
        d += 2 + rows;
    }
    d[0] = 0x41; d[1] = (uint8_t)(uint16_t)start; d[2] = (uint8_t)((uint16_t)start >> 8);
}

void a3d_init(uint8_t lines, uint8_t text_rows)
{
    uint8_t i;
    uint16_t a = BUF0;

    if (lines > (a3d_double ? 48 : A3D_MAXH)) lines = a3d_double ? 48 : A3D_MAXH;
    if (text_rows > 3) text_rows = 3;
    a3d_h = lines;
    for (i = 0; i < A3D_MAXH; ++i) {
        a3d_ylo[i] = (uint8_t)a;
        a3d_yhi[i] = (uint8_t)(a >> 8);
        a += 40;
    }
    SDMCTL = SDMCTL & 0x1C;
    wait_vbl();
    memset((void *)0xA000, 0, 0x2000);
    build_dl(DL0, 0xA0, lines, text_rows);
    build_dl(DL1, 0xB0, lines, text_rows);
    COLOR0 = A8_COLOR(2, 8);
    COLOR1 = A8_COLOR(12, 10);
    COLOR2 = A8_COLOR(9, 6);
    COLOR4 = 0;
    if (text_rows) {        /* the text rows' colours come from a DLI */
        a3d_dl_flag((uint8_t)(lines - 1), 0x80);
        VDSLST = (uint16_t)a3d_hud_dli;
        NMIEN = 0xC0;
    }
    CHBAS = 0xE0;
    SDLSTL = (uint16_t)DL0;
    a3d_back = 1;
    a3d_bsel = 0x10;
    wait_vbl();
    SDMCTL = (SDMCTL & 0x1C) | 0x22;
    wait_vbl();
    a3d_colour(1);
}

uint8_t *a3d_dlist(uint8_t buf) { return buf ? DL1 : DL0; }

void a3d_dl_flag(uint8_t y, uint8_t flags)
{
    /* line 0 is the LMS at offset 3, line y > 0 at offset 5 + y; doubled:
       the second showing of line y at 6 + 6y */
    uint16_t o;
    uint8_t *d0, *d1;
    if (a3d_double) {
        o = 6 + 6 * (uint16_t)y;
        d0 = (uint8_t *)0xA800;
        d1 = (uint8_t *)0xB800;
    } else {
        o = y ? 5 + y : 3;
        d0 = (uint8_t *)0xAF10;
        d1 = (uint8_t *)0xBF10;
    }
    d0[o] = (d0[o] & 0x7F) | flags;
    d1[o] = (d1[o] & 0x7F) | flags;
}

static uint8_t shown_at;

void a3d_show(void)
{
    /* both lists start at $xF10: only the high byte changes (atomic for the VBI) */
    *(volatile uint8_t *)0x231 = (uint8_t)((uint16_t)(a3d_back ? DL1 : DL0) >> 8);
    shown_at = RTCLOK2;
    a3d_back ^= 1;
    a3d_bsel = a3d_back ? 0x10 : 0x00;
}

/* wait for a vertical blank after the last a3d_show (the new list is then
   on screen and the other buffer free); frame_wait() for the sound and
   the count. (frame_wait alone returns at once when a frame passed while
   drawing, before the switch: the next frame would draw into the buffer
   on screen.) */
uint8_t a3d_sync(void)
{
    while (RTCLOK2 == shown_at)
        ;
    return frame_wait();
}

uint8_t a3d_flip(void)
{
    a3d_show();
    return a3d_sync();
}
