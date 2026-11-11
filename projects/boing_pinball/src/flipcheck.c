/* flipcheck.c - (make FLIPCHECK=1) physasm.s's flip_fast checked against
   the C it follows: pb_phys.c's hit_flipper. Runs in main memory, called
   through banks.s (bank 0 has no room for both). */
#ifdef FLIPCHECK
#include "pbz.h"
#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))
#define SLOW      V_MM(60)
static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : (int16_t)v; }
static void unit(int16_t dx, int16_t dy, int16_t d, int16_t *nx, int16_t *ny)
{
    *nx = div_q14(dx, d);
    *ny = div_q14(dy, d);
}
static void place(Ball *b, int16_t x, int16_t y)
{
    b->x = x; b->y = y; b->xf = b->yf = 128;
}

int16_t c_flip_ref(Ball *b, const Flipper *f)
{
    int16_t tx, ty, ex, ey, t, qx, qy, rad, dx, dy, rr, d, nx, ny, cx, cy, vcx, vcy, rvx, rvy, vn;
    int32_t num, den;
    tx = f->tx; ty = f->ty;              /* flip_fast, run first, brought the tip up to date */
    ex = tx - f->d.px; ey = ty - f->d.py;
    num = mul16(b->x - f->d.px, ex) + mul16(b->y - f->d.py, ey);
    den = mul16(f->d.len, f->d.len);
    /* t = clamp(num / den, 0, 1), Q14 (port: no 64-bit division; maths.s's
       32 / 16 division) */
    t = num <= 0 ? 0 : num >= den ? 16384 : (int16_t)udiv32_16((uint32_t)num << 8, (uint16_t)(den >> 6));
    qx = f->d.px + mulq14(t, ex);
    qy = f->d.py + mulq14(t, ey);
    rad = f->d.r1 + mulq14(f->d.r2 - f->d.r1, t);
    dx = b->x - qx; dy = b->y - qy;
    rr = PB_R + rad;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) {                        /* dead centre: out on the side the flipper moves towards */
        nx = -div_q14(ey, f->d.len); ny = div_q14(ex, f->d.len);
        if (f->w < 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    cx = qx + mulq14(nx, rad);
    cy = qy + mulq14(ny, rad);
    /* the contact point's velocity: W/256 rad/s x P/16 mm = mm/s, x4 for V */
    vcx = -(int16_t)(mul16(f->w, cy - f->d.py) >> 10);
    vcy = (int16_t)(mul16(f->w, cx - f->d.px) >> 10);
    place(b, qx + mulq14(nx, rr), qy + mulq14(ny, rr));
    rvx = sat16((int32_t)b->vx - vcx); rvy = sat16((int32_t)b->vy - vcy);
    vn = (int16_t)((mul16(rvx, nx) + mul16(rvy, ny)) >> 14);
    if (vn < 0) {
        int16_t e = ABS16(f->w) > 256 ? 141 : 77;          /* 0.55 : 0.3 */
        int8_t slow = -vn < SLOW;
        int16_t ee = slow ? 0 : e, k = slow ? 256 : 248, en = mulq8(ee, vn);   /* 0.97 */
        int16_t sx = rvx - mulq14(vn, nx), sy = rvy - mulq14(vn, ny);
        b->vx = sat16((int32_t)vcx + mulq8(sx, k) - mulq14(en, nx));
        b->vy = sat16((int32_t)vcy + mulq8(sy, k) - mulq14(en, ny));
        return -vn;
    }
    return 0;
}

#endif
