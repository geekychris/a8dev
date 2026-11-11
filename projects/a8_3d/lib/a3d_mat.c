/* a3d_mat.c - sine table and rotation matrices (Q7). */
#include <string.h>
#include "a3d_int.h"

static int16_t m8(int8_t a, int8_t b)
{
    a3d_ma = a;
    return a3d_mul8s_(b);
}

static int8_t q7(int16_t v)
{
    v = (v + 64) >> 7;
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    return (int8_t)v;
}

void a3d_mat_ident(a3d_mat *out)
{
    memset(out, 0, sizeof *out);
    out->m[0] = out->m[4] = out->m[8] = 127;
}

void a3d_mat_ypr(a3d_mat *out, uint8_t yaw, uint8_t pitch, uint8_t roll)
{
    int8_t cy = a3d_cos(yaw), sy = a3d_sin(yaw);
    int8_t cp = a3d_cos(pitch), sp = a3d_sin(pitch);
    int8_t *m = out->m;
    int8_t y0, y2, cr, sr;
    y0 = q7(m8(sy, sp));
    y2 = q7(m8(cy, sp));
    m[6] = q7(m8(sy, cp)); m[7] = (int8_t)-sp; m[8] = q7(m8(cy, cp));
    if (roll) {
        cr = a3d_cos(roll); sr = a3d_sin(roll);
        m[0] = q7(m8(cr, cy) + m8(sr, y0));
        m[1] = q7(m8(sr, cp));
        m[2] = q7(m8(sr, y2) - m8(cr, sy));
        m[3] = q7(m8(cr, y0) - m8(sr, cy));
        m[4] = q7(m8(cr, cp));
        m[5] = q7(m8(cr, y2) + m8(sr, sy));
    } else {
        m[0] = cy; m[1] = 0; m[2] = (int8_t)-sy;
        m[3] = y0; m[4] = cp; m[5] = y2;
    }
}

void a3d_cam_ypr(a3d_mat *out, uint8_t yaw, uint8_t pitch, uint8_t roll)
{
    int8_t *m = out->m;
    a3d_mat_ypr(out, yaw, pitch, roll);
    m[3] = q7(m8(m[3], (int8_t)a3d_aspect));
    m[4] = q7(m8(m[4], (int8_t)a3d_aspect));
    m[5] = q7(m8(m[5], (int8_t)a3d_aspect));
}

void a3d_mat_mul(a3d_mat *out, const a3d_mat *a, const a3d_mat *b)
{
    a3d_mma = a;
    a3d_mmo = out;
    a3d_mat_mul_(b);
}

void a3d_rot16(const a3d_mat *m, int16_t x, int16_t y, int16_t z, int16_t *out)
{
    a3d_rv[0] = x; a3d_rv[1] = y; a3d_rv[2] = z;
    a3d_rot16_(m);
    memcpy(out, a3d_rout, 6);
}
