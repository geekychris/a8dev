/* a8gfx.h: display lists and bitmap drawing at the fixed $A000 screen area. */
#include <string.h>
#include "a8gfx.h"

#define BITMAP   ((uint8_t *)(A8_SCREEN_ADDR + 0x10))    /* $A010: 4K boundary between lines 101/102 */
#define TEXTROWS ((uint8_t *)0xBE10)                     /* after a full 7680-byte bitmap */
#define DLIST    ((uint8_t *)0xBF00)                     /* 256 bytes, within one 1K block */

uint8_t gfx_mode;
uint8_t *gfx_screen;
uint16_t gfx_width;
uint8_t gfx_height;
uint8_t gfx_bpl = 40;
uint8_t *gfx_text;
uint8_t gfx_text_rows;
uint8_t *gfx_dlist = DLIST;
uint8_t *gfx_line_addr[192];

static uint8_t scan_lines(uint8_t mode)
{
    switch (mode) {
    case GFX_TEXT: case GFX_TEXT4: return 8;
    case GFX_160x96: return 2;
    }
    return 1;
}

static void wait_vbl(void)
{
    uint8_t t = RTCLOK2;
    while (RTCLOK2 == t)
        ;
}

void gfx_init(uint8_t mode, uint8_t text_rows)
{
    uint8_t *d = DLIST, *a;
    uint8_t lines, i, sl;
    uint16_t addr;

    if (text_rows > 6) text_rows = 6;
    sl = scan_lines(mode);
    lines = (uint8_t)((192 - 8 * text_rows) / sl);
    gfx_mode = mode;
    gfx_height = lines;
    gfx_width = mode == GFX_320 ? 320 : (mode == GFX_TEXT || mode == GFX_TEXT4) ? 40 : 160;
    gfx_screen = BITMAP;
    gfx_text_rows = text_rows;
    gfx_text = text_rows ? TEXTROWS : 0;

    SDMCTL = SDMCTL & 0x1C;          /* screen off while the list changes (keep PM DMA bits) */
    wait_vbl();

    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    a = BITMAP;
    for (i = 0; i < lines; ++i) {
        addr = (uint16_t)a;
        if (i == 0 || (addr & 0x0FFF) == 0) {
            *d++ = 0x40 | mode;
            *d++ = (uint8_t)addr;
            *d++ = (uint8_t)(addr >> 8);
        } else {
            *d++ = mode;
        }
        if (i < 192) gfx_line_addr[i] = a;
        a += 40;
    }
    for (i = 0; i < text_rows; ++i) {
        if (i == 0) {
            *d++ = 0x42;
            *d++ = (uint8_t)(uint16_t)TEXTROWS;
            *d++ = (uint8_t)((uint16_t)TEXTROWS >> 8);
        } else {
            *d++ = 0x02;
        }
    }
    *d++ = 0x41;
    *d++ = (uint8_t)(uint16_t)DLIST;
    *d = (uint8_t)((uint16_t)DLIST >> 8);

    gfx_clear();
    if (mode == GFX_TEXT || mode == GFX_320 || text_rows) {
        COLOR1 = 0x0E;
        COLOR2 = A8_COLOR(9, 4);
    }
    if (mode == GFX_TEXT4 || mode == GFX_160 || mode == GFX_160x96) {
        COLOR0 = A8_COLOR(2, 8);
        COLOR1 = A8_COLOR(12, 10);
        COLOR2 = A8_COLOR(9, 6);
    }
    COLOR4 = 0;
    CHBAS = 0xE0;
    SDLSTL = (uint16_t)DLIST;
    wait_vbl();
    SDMCTL = (SDMCTL & 0x1C) | 0x22;  /* normal playfield + display list DMA */
    wait_vbl();
}

void gfx_off(void) { SDMCTL &= 0x1C; }
void gfx_on(void) { SDMCTL = (SDMCTL & 0x1C) | 0x22; }

void gfx_clear(void)
{
    memset(BITMAP, 0, (uint16_t)0xBF00 - (uint16_t)BITMAP);
}

static const uint8_t mask1[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
static const uint8_t mask2[4] = { 0xC0, 0x30, 0x0C, 0x03 };
static const uint8_t fill2[4] = { 0x00, 0x55, 0xAA, 0xFF };

void gfx_plot(uint16_t x, uint8_t y, uint8_t c)
{
    uint8_t *p = gfx_line_addr[y];
    uint8_t m;
    if (gfx_mode == GFX_320) {
        p += (uint8_t)(x >> 3);
        m = mask1[(uint8_t)x & 7];
        if (c) *p |= m; else *p &= (uint8_t)~m;
    } else {
        p += (uint8_t)(x >> 2);
        m = mask2[(uint8_t)x & 3];
        *p = (*p & (uint8_t)~m) | (fill2[c & 3] & m);
    }
}

uint8_t gfx_point(uint16_t x, uint8_t y)
{
    uint8_t *p = gfx_line_addr[y];
    if (gfx_mode == GFX_320) return (p[(uint8_t)(x >> 3)] & mask1[(uint8_t)x & 7]) ? 1 : 0;
    return (uint8_t)((p[(uint8_t)(x >> 2)] >> ((3 - ((uint8_t)x & 3)) << 1)) & 3);
}

void gfx_hline(uint16_t x0, uint16_t x1, uint8_t y, uint8_t c)
{
    uint16_t x;
    if (x0 > x1) { x = x0; x0 = x1; x1 = x; }
    for (x = x0; x <= x1; ++x) gfx_plot(x, y, c);
}

void gfx_rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    uint8_t j;
    if (!w || !h) return;
    for (j = 0; j < h; ++j) gfx_hline(x, x + w - 1, (uint8_t)(y + j), c);
}

void gfx_fill_bytes(uint8_t bx, uint8_t y, uint8_t w, uint8_t h, uint8_t v)
{
    uint8_t j;
    for (j = 0; j < h; ++j) memset(gfx_line_addr[(uint8_t)(y + j)] + bx, v, w);
}

void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)
{
    int16_t dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, e2;
    int16_t w = (int16_t)gfx_width, h = gfx_height;
    if (dx < 0) { dx = -dx; sx = -1; }
    if (dy < 0) { dy = -dy; sy = -1; }
    err = dx - dy;
    for (;;) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) gfx_plot((uint16_t)x0, (uint8_t)y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}
