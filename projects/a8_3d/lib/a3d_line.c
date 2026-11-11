/* a3d_line.c - clipped lines, edges and outlines. */
#include <string.h>
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

void a3d_edge(uint8_t a, uint8_t b, uint8_t c)
{
    if (a3d_vclip[a]) {
        if (a3d_vclip[b]) return;
        a3d_near(b, a);
        a = A3D_TMP;
    } else if (a3d_vclip[b]) {
        a3d_near(a, b);
        b = A3D_TMP;
    }
    a3d_line(a3d_sx[a], a3d_sy[a], a3d_sx[b], a3d_sy[b], c);
}

uint8_t a3d_face_wire(uint8_t n, const uint8_t *idx, uint8_t c)
{
    static int16_t wx[A3D_MAXP], wy[A3D_MAXP];
    uint8_t i, r;
    a3d_fn = n;
    r = a3d_gather_(idx);
    if (r == 2) {
        n = a3d_gather_near(n, idx);
        r = n >= 3 && a3d_front_();
    }
    if (!r) return 0;
    memcpy(wx, a3d_px, n * 2);
    memcpy(wy, a3d_py, n * 2);
    for (i = 0; i < n; ++i)
        a3d_line(wx[i], wy[i], wx[i + 1 == n ? 0 : i + 1], wy[i + 1 == n ? 0 : i + 1], c);
    return 1;
}
