/* a3d_clip.c - clipping the rare cases the asm hands over: polygons that
   cross the top or bottom of the 3D area, faces through the near plane. */
#include <string.h>
#include "a3d_int.h"

#ifdef A3D_LOWTAB
#pragma bss-name ("LOWRAM")
#endif

int16_t a3d_px[A3D_MAXP], a3d_py[A3D_MAXP];

extern int16_t a3d_mda, a3d_mdb;
extern int16_t __fastcall__ a3d_muldiv_(int16_t c);

int16_t a3d_muldiv(int16_t a, int16_t b, int16_t c)
{
    a3d_mda = a;
    a3d_mdb = b;
    return a3d_muldiv_(c);
}

/* clip a3d_px/py (n) to y >= lim (top) or y <= lim (bottom) */
static uint8_t clip_y(uint8_t n, int16_t lim, uint8_t bottom)
{
    static int16_t ox[A3D_MAXP], oy[A3D_MAXP];
    uint8_t i, j, m = 0, ina, inb;
    int16_t ya, yb;
    for (i = 0; i < n; ++i) {
        j = (uint8_t)(i + 1 == n ? 0 : i + 1);
        ya = a3d_py[i]; yb = a3d_py[j];
        ina = (ya <= lim) == bottom || ya == lim;
        inb = (yb <= lim) == bottom || yb == lim;
        if (m >= A3D_MAXP - 1) break;
        if (ina) { ox[m] = a3d_px[i]; oy[m++] = ya; }
        if (ina != inb) {
            ox[m] = a3d_px[i] + a3d_muldiv(a3d_px[j] - a3d_px[i], lim - ya, yb - ya);
            oy[m++] = lim;
        }
    }
    memcpy(a3d_px, ox, m * 2);
    memcpy(a3d_py, oy, m * 2);
    return m;
}

void __fastcall__ a3d_poly_clip(uint8_t n)
{
    n = clip_y(n, 0, 0);
    if (n >= 3) n = clip_y(n, (int16_t)a3d_h, 1);
    if (n >= 3) a3d_fill(n);
}
