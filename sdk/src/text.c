/* a8gfx.h: text in character screens, text rows and bitmaps. */
#include "a8gfx.h"

uint8_t a8_screen_code(char ch)
{
    uint8_t c = (uint8_t)ch, inv = c & 0x80;
    c &= 0x7F;
    if (c < 0x20) c += 0x40;
    else if (c < 0x60) c -= 0x20;
    return c | inv;
}

static void put(uint8_t *p, const char *s)
{
    while (*s) *p++ = a8_screen_code(*s++);
}

void text_print_at(uint8_t x, uint8_t y, const char *s) { put(gfx_screen + (uint16_t)y * 40 + x, s); }
void text_row_print(uint8_t x, uint8_t row, const char *s) { if (gfx_text) put(gfx_text + (uint16_t)row * 40 + x, s); }

static char *num(uint16_t v, uint8_t width)
{
    static char buf[6];
    uint8_t i = 5;
    buf[5] = 0;
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i && (uint8_t)(5 - i) < width) buf[--i] = ' ';
    return buf + i;
}

void text_num_at(uint8_t x, uint8_t y, uint16_t v, uint8_t width) { text_print_at(x, y, num(v, width)); }
void text_row_num(uint8_t x, uint8_t row, uint16_t v, uint8_t width) { text_row_print(x, row, num(v, width)); }

void gfx_print(uint8_t bx, uint8_t y, const char *s, uint8_t c)
{
    static uint8_t ex[16];
    uint8_t i, k, b;
    const uint8_t *f;
    uint8_t pat = (uint8_t)((c & 3) * 0x55);
    if (gfx_mode != GFX_320)
        for (k = 0; k < 16; ++k) {
            b = 0;
            if (k & 8) b |= 0xC0;
            if (k & 4) b |= 0x30;
            if (k & 2) b |= 0x0C;
            if (k & 1) b |= 0x03;
            ex[k] = b & pat;
        }
    while (*s) {
        f = (const uint8_t *)(0xE000 + ((uint16_t)(a8_screen_code(*s++) & 0x7F) << 3));
        for (i = 0; i < 8; ++i) {
            uint8_t *p = gfx_line_addr[(uint8_t)(y + i)] + bx;
            if (gfx_mode == GFX_320) *p = f[i];
            else { p[0] = ex[f[i] >> 4]; p[1] = ex[f[i] & 15]; }
        }
        bx += gfx_mode == GFX_320 ? 1 : 2;
    }
}
