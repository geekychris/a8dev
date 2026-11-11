/* a3d_text.c - the text rows under the 3D area (ANTIC 2, ROM font). */
#include "a3d_int.h"

void a3d_print(uint8_t x, uint8_t row, const char *s)
{
    uint8_t *p = A3D_TEXT + row * 40 + x, c, inv;
    while ((c = (uint8_t)*s++) != 0) {
        inv = c & 0x80;
        c &= 0x7F;
        if (c < 0x20) c += 0x40;
        else if (c < 0x60) c -= 0x20;
        *p++ = c | inv;
    }
}

void a3d_num(uint8_t x, uint8_t row, uint16_t v, uint8_t width)
{
    uint8_t *p = A3D_TEXT + row * 40 + x + width;
    do {
        *--p = (uint8_t)(0x10 + v % 10);
        v /= 10;
    } while (--width && v);
    while (width--) *--p = 0;
}
