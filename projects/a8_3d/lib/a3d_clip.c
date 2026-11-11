/* a3d_clip.c - clipping the rare cases the asm hands over: polygons that
   cross the top or bottom of the 3D area, faces through the near plane. */
#include <string.h>
#include "a3d_int.h"

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

void a3d_near(uint8_t a, uint8_t b)
{
    int16_t za = a3d_vz[a], d = a3d_vz[b] - za, t = A3D_NEAR - za;
    a3d_vx[A3D_TMP] = a3d_vx[a] + a3d_muldiv(a3d_vx[b] - a3d_vx[a], t, d);
    a3d_vy[A3D_TMP] = a3d_vy[a] + a3d_muldiv(a3d_vy[b] - a3d_vy[a], t, d);
    a3d_vz[A3D_TMP] = A3D_NEAR;
    a3d_project(A3D_TMP, 1);
}

uint8_t a3d_gather_near(uint8_t n, const uint8_t *idx)
{
    uint8_t i, a, b, m = 0;
    for (i = 0; i < n && m < A3D_MAXP - 1; ++i) {
        a = idx[i];
        b = idx[i + 1 == n ? 0 : i + 1];
        if (!a3d_vclip[a]) {
            a3d_px[m] = a3d_sx[a]; a3d_py[m++] = a3d_sy[a];
            if (!a3d_vclip[b]) continue;
            a3d_near(a, b);
        } else if (!a3d_vclip[b]) {
            a3d_near(b, a);
        } else continue;
        a3d_px[m] = a3d_sx[A3D_TMP]; a3d_py[m++] = a3d_sy[A3D_TMP];
    }
    return m;
}

uint8_t a3d_face(uint8_t n, const uint8_t *idx)
{
    uint8_t r;
    a3d_fn = n;
    r = a3d_face_(idx);
    if (r != 2) return r;
    n = a3d_gather_near(n, idx);
    if (n < 3 || !a3d_front_()) return 0;
    a3d_poly(n);
    return 1;
}
