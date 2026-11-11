/* a3d_line.c - clipped lines and points (edges and outlines: a3d_edge.c). */
#include "a3d_int.h"

/* Cohen-Sutherland to 0..159 x 0..a3d_h-1 */
static uint8_t outcode(int16_t x, int16_t y)
{
    uint8_t c = 0;
    if (x < 0) c = 1; else if (x > 159) c = 2;
    if (y < 0) c |= 4; else if (y >= (int16_t)a3d_h) c |= 8;
    return c;
}

void a3d_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)
{
    uint8_t c0 = outcode(x0, y0), c1 = outcode(x1, y1), co;
    int16_t x, y, e;
    while (c0 | c1) {
        if (c0 & c1) return;
        co = c0 ? c0 : c1;
        if (co & 12) {
            e = co & 8 ? (int16_t)a3d_h - 1 : 0;
            x = x0 + a3d_muldiv(x1 - x0, e - y0, y1 - y0); y = e;
        } else {
            e = co & 2 ? 159 : 0;
            y = y0 + a3d_muldiv(y1 - y0, e - x0, x1 - x0); x = e;
        }
        if (co == c0) { x0 = x; y0 = y; c0 = outcode(x, y); }
        else { x1 = x; y1 = y; c1 = outcode(x, y); }
    }
    a3d_lx0 = (uint8_t)x0; a3d_ly0 = (uint8_t)y0;
    a3d_lx1 = (uint8_t)x1; a3d_ly1 = (uint8_t)y1;
    a3d_line_(c);
}

void a3d_plot(uint8_t x, uint8_t y, uint8_t c)
{
    a3d_lx0 = a3d_lx1 = x;
    a3d_ly0 = a3d_ly1 = y;
    a3d_line_(c);
}
