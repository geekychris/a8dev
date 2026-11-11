/* a3d_near.c - faces through the near plane (the asm hands them over). */
#include <string.h>
#include "a3d_int.h"

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
