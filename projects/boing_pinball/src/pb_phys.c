/*
 * pb_phys.c - Boing Pinball's physics for the Z80: the original's
 * (tools/orig/pb_core.c, "collisions", "the ramps" and "the world"),
 * function for function, in the 16-bit units of pbz.h. The constants are
 * the original's, converted; where the arithmetic differs it says so.
 */
#include <string.h>
#include "pbz.h"

#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))
#define SLOW      V_MM(60)              /* below this a touch doesn't bounce or scrub */
#define PI_A      12868                 /* pi, A */

static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : (int16_t)v; }

/* |(x, y)|, V or P */
static int16_t hypot16(int16_t x, int16_t y)
{
    return (int16_t)isqrt32((uint32_t)(mul16(x, x) + mul16(y, y)));
}

/* ================================================================ collisions */

static void bounce(Ball *b, int16_t nx, int16_t ny, int16_t vn, uint8_t e, uint8_t fric)
{
    int16_t tx = b->vx - mulq14(vn, nx), ty = b->vy - mulq14(vn, ny);
    int8_t slow = -vn < SLOW;
    int16_t ee = slow ? 0 : e, k = slow ? 256 : 256 - fric;
    int16_t en = mulq8(ee, vn);
    b->vx = mulq8(tx, k) - mulq14(en, nx);
    b->vy = mulq8(ty, k) - mulq14(en, ny);
}

static void unit(int16_t dx, int16_t dy, int16_t d, int16_t *nx, int16_t *ny)
{
    *nx = div_q14(dx, d);
    *ny = div_q14(dy, d);
}

static void place(Ball *b, int16_t x, int16_t y)
{
    b->x = x; b->y = y; b->xf = b->yf = 128;
}

/* Circle (the ball) against a capsule segment: resolves the overlap and
 * returns the approach speed (V), or -1. */
extern int16_t __fastcall__ seg_fast(Ball *b, const Seg *s);   /* physasm.s: the interior case below */

static int16_t hit_segment(Ball *b, const Seg *s)
{
    int16_t dx = b->x - s->ax, dy = b->y - s->ay, t, qx, qy, rr = PB_R + s->r, d, nx, ny, vn;
    int32_t tt;
#ifdef SEGCHECK
    {   /* the asm against the C (segcheck.c), on a copy */
        extern int16_t seg_ref(Ball *b, const Seg *s);
        extern void seg_mismatch(const Ball *a, const Ball *c, int16_t ra, int16_t rc);
        static Ball bc;
        int16_t ra, rc;
        memcpy(&bc, b, sizeof bc);
        ra = seg_fast(&bc, s);
        if (ra != (int16_t)0x8000) {
            rc = seg_ref(b, s);
            if (ra != rc || memcmp(&bc, b, sizeof bc)) seg_mismatch(&bc, b, ra, rc);
            return rc;
        }
    }
#endif
    t = seg_fast(b, s);
    if (t != (int16_t)0x8000) return t;
    /* port: on the segment's straight part (0 < t < len) the closest
       point's direction is the segment's normal: seg_fast (physasm.s) takes
       the distance from the cross product and the normal +/-(-uy, ux), with
       no square root and no divisions (the original normalises b - q: the
       same up to rounding). Here: the ends. */
    tt = (mul16(dx, s->ux) + mul16(dy, s->uy)) >> 14;
    t = tt < 0 ? 0 : tt > s->len ? s->len : (int16_t)tt;
    qx = s->ax + mulq14(s->ux, t);
    qy = s->ay + mulq14(s->uy, t);
    dx = b->x - qx; dy = b->y - qy;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) {                        /* dead centre: out on the side it came from */
        nx = -s->uy; ny = s->ux;
        if (mul16(nx, b->vx) + mul16(ny, b->vy) > 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    vn = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
    if ((s->ow_x || s->ow_y) && (nx * s->ow_x + ny * s->ow_y <= 0 || vn >= 0)) return -1;
    place(b, qx + mulq14(nx, rr), qy + mulq14(ny, rr));
    if (vn < 0) bounce(b, nx, ny, vn, s->e, 8);           /* friction 0.03 */
    return vn < 0 ? -vn : 0;
}

static int16_t hit_circle(Ball *b, const Circ *c, int16_t *pnx, int16_t *pny)
{
    int16_t dx = b->x - c->cx, dy = b->y - c->cy, rr = PB_R + c->r, d, nx, ny, vn;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) { nx = 0; ny = 16384; } else unit(dx, dy, d, &nx, &ny);
    place(b, c->cx + mulq14(nx, rr), c->cy + mulq14(ny, rr));
    vn = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
    if (vn < 0) bounce(b, nx, ny, vn, c->e, 5);           /* friction 0.02 */
    *pnx = nx; *pny = ny;
    return vn < 0 ? -vn : 0;
}

void pb_flipper_tip(const Flipper *f, int16_t *x, int16_t *y)
{
    Flipper *m = (Flipper *)f;          /* port: the tip is cached until the flipper moves */
    if (m->tip_ang != f->ang) {
        m->tx = f->d.px + mulq14(f->d.len, cos_q14(f->ang));
        m->ty = f->d.py + mulq14(f->d.len, sin_q14(f->ang));
        m->tip_ang = f->ang;
    }
    *x = m->tx;
    *y = m->ty;
}

/* Ball against a (moving) flipper, a tapered capsule; the surface's
 * velocity at the contact is w x r. */
static int16_t hit_flipper(Ball *b, const Flipper *f)
{
    int16_t tx, ty, ex, ey, t, qx, qy, rad, dx, dy, rr, d, nx, ny, cx, cy, vcx, vcy, rvx, rvy, vn;
    int32_t num, den;
    pb_flipper_tip(f, &tx, &ty);
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


/* Did the move p0 -> p1 cross the sensor line? +1 / -1 (the side crossed
 * to), or 0. */
static int8_t crossed(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const Sensor *s)
{
    int16_t ex = s->bx - s->ax, ey = s->by - s->ay, px, py;
    int32_t d0 = mul16(ex, y0 - s->ay) - mul16(ey, x0 - s->ax);
    int32_t d1 = mul16(ex, y1 - s->ay) - mul16(ey, x1 - s->ax);
    int32_t u, l2;
    if ((d0 > 0) == (d1 > 0) || d0 == d1) return 0;
    /* the crossing point (port: the step is short, so (x1 - x0) * d0 fits) */
    px = x0 + (int16_t)((int32_t)(x1 - x0) * d0 / (d0 - d1));
    py = y0 + (int16_t)((int32_t)(y1 - y0) * d0 / (d0 - d1));
    l2 = mul16(ex, ex) + mul16(ey, ey);
    u = mul16(px - s->ax, ex) + mul16(py - s->ay, ey);   /* 0 <= u / l2 <= 1 */
    if (u < 0 || u > l2) return 0;
    return d1 > 0 ? 1 : -1;
}

/* ================================================================ the world */
/* port: the cold parts (the ramps, adding and serving balls, kicks) are in
   pb_phys2.c, in bank 2 */

static void emit(World *w, int8_t type, int16_t x, int16_t y, int8_t id, int8_t up, int32_t val, int8_t ball)
{
    Event *e;
    if (w->nev >= PB_MAX_EV) return;
    e = &w->ev[w->nev++];
    e->type = type; e->id = id; e->kind = 0; e->up = up;
    e->x = x >> 4; e->y = y >> 4; e->val = val; e->ball = ball;
}

/* the substep: n of them make the 1/25 s step (port: 25 Hz) */
/* port: the substep's constants for n = 0..40: 9000 rad/s^2 and 5000
   rad/s^2 a substep in W (92160/n, 51200/n; capped at 32767: from n = 1 the
   flipper reaches its top speed in one substep either way), W -> A
   (41943/n, Q16; n = 1: 20972, Q15), V -> 1/256 P (20972/n, Q9), gravity
   PB_GRAVITY_STEP / n and % n */
#pragma rodata-name (push, "TABRO")    /* under the ROM with the table: bank 0 is full */
static const int16_t sub_tab[41][6] = {
    { 0, 0, 0, 0, 0, 0 },
    { 32767, 32767, 20972, 20972, 240, 0 },
    { 32767, 25600, 20971, 10486, 120, 0 },
    { 30720, 17066, 13981, 6990, 80, 0 },
    { 23040, 12800, 10485, 5243, 60, 0 },
    { 18432, 10240, 8388, 4194, 48, 0 },
    { 15360, 8533, 6990, 3495, 40, 0 },
    { 13165, 7314, 5991, 2996, 34, 2 },
    { 11520, 6400, 5242, 2621, 30, 0 },
    { 10240, 5688, 4660, 2330, 26, 6 },
    { 9216, 5120, 4194, 2097, 24, 0 },
    { 8378, 4654, 3813, 1906, 21, 9 },
    { 7680, 4266, 3495, 1747, 20, 0 },
    { 7089, 3938, 3226, 1613, 18, 6 },
    { 6582, 3657, 2995, 1498, 17, 2 },
    { 6144, 3413, 2796, 1398, 16, 0 },
    { 5760, 3200, 2621, 1310, 15, 0 },
    { 5421, 3011, 2467, 1233, 14, 2 },
    { 5120, 2844, 2330, 1165, 13, 6 },
    { 4850, 2694, 2207, 1103, 12, 12 },
    { 4608, 2560, 2097, 1048, 12, 0 },
    { 4388, 2438, 1997, 998, 11, 9 },
    { 4189, 2327, 1906, 953, 10, 20 },
    { 4006, 2226, 1823, 911, 10, 10 },
    { 3840, 2133, 1747, 873, 10, 0 },
    { 3686, 2048, 1677, 838, 9, 15 },
    { 3544, 1969, 1613, 806, 9, 6 },
    { 3413, 1896, 1553, 776, 8, 24 },
    { 3291, 1828, 1497, 749, 8, 16 },
    { 3177, 1765, 1446, 723, 8, 8 },
    { 3072, 1706, 1398, 699, 8, 0 },
    { 2972, 1651, 1353, 676, 7, 23 },
    { 2880, 1600, 1310, 655, 7, 16 },
    { 2792, 1551, 1271, 635, 7, 9 },
    { 2710, 1505, 1233, 616, 7, 2 },
    { 2633, 1462, 1198, 599, 6, 30 },
    { 2560, 1422, 1165, 582, 6, 24 },
    { 2490, 1383, 1133, 566, 6, 18 },
    { 2425, 1347, 1103, 551, 6, 12 },
    { 2363, 1312, 1075, 537, 6, 6 },
    { 2304, 1280, 1048, 524, 6, 0 },
};
#pragma rodata-name (pop)


int16_t sub_kpos;                       /* also zmath.asm's advance() */
static int16_t sub_acc_up, sub_acc_down, sub_kang;
static uint8_t sub_n1;

static void flipper_step(World *w, Flipper *f)
{
    int8_t want = f->held && w->flipper_power;
    if (f->w == 0 && f->ang == (want ? f->d.up : f->d.rest)) return;   /* port: at rest, first */
    {
    int16_t lo = f->d.rest < f->d.up ? f->d.rest : f->d.up, hi = f->d.rest < f->d.up ? f->d.up : f->d.rest;
    int16_t target = want ? f->d.up : f->d.rest;
    int8_t sgn = target > f->ang ? 1 : target < f->ang ? -1 : 0;
    int16_t wmax = want ? 42 * 256 : 26 * 256;
    int16_t acc = want ? sub_acc_up : sub_acc_down;      /* 9000 / 5000 rad/s^2 for the substep */
    if (sgn == 0 && f->w == 0) return;                   /* at rest (the rest changes nothing) */
    if (sgn == 0) f->w = 0;
    else if (sgn > 0) {                                  /* port: no multiplies by sgn */
        f->w += acc;
        if (ABS16(f->w) > wmax) f->w = wmax;
        if (f->w <= 0) f->w = ABS16(f->w) < acc ? ABS16(f->w) : acc;
    } else {
        f->w -= acc;
        if (ABS16(f->w) > wmax) f->w = -wmax;
        if (f->w >= 0) f->w = -(ABS16(f->w) < acc ? ABS16(f->w) : acc);
    }
    f->ang += (int16_t)(sub_n1 ? mul16(f->w, sub_kang) >> 15 : mul16(f->w, sub_kang) >> 16);   /* port: Q15 when n = 1 */
    if (f->ang <= lo) {
        f->ang = lo;
        if (f->w < 0) { if (ABS16(f->w) > 8 * 256) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
    if (f->ang >= hi) {
        f->ang = hi;
        if (f->w > 0) { if (ABS16(f->w) > 8 * 256) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
    f->tip_ang = 0x7FFF;                                 /* the tip moved */
    }
}

static int8_t flipper_moving(const World *w, const Flipper *f)
{
    int16_t target = (f->held && w->flipper_power) ? f->d.up : f->d.rest;
    return f->w != 0 || f->ang != target;
}

/* port: move_ball's fast path is in assembly (physasm.s): the speed cap
 * test, x += v dt for both axes (advance()), the grid cell, and for each
 * of the cell's walls, posts, bumpers and sensors and each flipper the
 * box test that rejects nearly all of them. What passes comes here: the
 * original's code, cut into the parts move_ball had inline. */

/* the tables physasm.s walks, made once per table (mb_prepare) */
int16_t mb_sx0[PB_MAX_SEG], mb_sy0[PB_MAX_SEG], mb_sx1[PB_MAX_SEG], mb_sy1[PB_MAX_SEG];
Seg *mb_sp[PB_MAX_SEG];
int16_t mb_cx[PB_MAX_CIRC], mb_cy[PB_MAX_CIRC], mb_crr[PB_MAX_CIRC];
const Circ *mb_cp[PB_MAX_CIRC];
int16_t mb_nx0[PB_MAX_SENS], mb_ny0[PB_MAX_SENS], mb_nx1[PB_MAX_SENS], mb_ny1[PB_MAX_SENS];
const Sensor *mb_np[PB_MAX_SENS];
int16_t mb_fpx[PB_MAX_FLIP], mb_fpy[PB_MAX_FLIP], mb_freach[PB_MAX_FLIP];
Flipper *mb_fp[PB_MAX_FLIP];
uint8_t mb_nflip;
int16_t mb_saucer_x, mb_saucer_y, mb_saucer_r;
const uint16_t *mb_grid_at, *mb_grid;
int16_t mb_x0, mb_y0;                   /* where the ball was at the substep's start */
Table *mb_t;                            /* the table mb_prepare (pb_phys2.c) made them for */


/* the speed cap (when |vx| + |vy| is over it) */
void mb_cap(Ball *b)
{
    int16_t sp = hypot16(b->vx, b->vy);
    if (sp > PB_MAX_SPEED) { int16_t s = div_q14(PB_MAX_SPEED, sp); b->vx = mulq14(b->vx, s); b->vy = mulq14(b->vy, s); }
}

/* a wall, gate, sling or drop target whose box the ball is in */
void mb_seg(World *w, Ball *b, Seg *s)
{
    int16_t imp;
    if (s->kind == SK_DROP && !s->up) return;
    w->tests++;
    imp = hit_segment(b, s);
    if (imp <= 0) return;
    if (s->kind == SK_DROP) {
        if (imp > V_MM(90)) { s->up = 0; emit(w, EV_DROP, b->x, b->y, s->id, 0, imp >> 2, b->id); }
    } else if (s->kind == SK_SLING) {
        if (imp > V_MM(140) && w->active_targets) {
            int16_t nx = -s->uy, ny = s->ux;
            if (mul16(b->x - s->ax, nx) + mul16(b->y - s->ay, ny) < 0) { nx = -nx; ny = -ny; }
            b->vx += mulq14(V_MM(950), nx); b->vy += mulq14(V_MM(950), ny);
            emit(w, EV_SLING, b->x, b->y, s->id, 0, 0, b->id);
        }
    } else if (imp > V_MM(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, imp >> 2, b->id);
}

/* a post or a pop bumper whose box the ball is in */
void mb_circ(World *w, Ball *b, const Circ *c)
{
    int16_t nx, ny, imp;
    w->tests++;
    imp = hit_circle(b, c, &nx, &ny);
    if (imp < 0) return;
    if (c->bumper) {
        if (w->active_targets) {
            int16_t out = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
            if (out < V_MM(1050)) { int16_t add = V_MM(1050) - out; b->vx += mulq14(nx, add); b->vy += mulq14(ny, add); }
            emit(w, EV_BUMPER, c->cx, c->cy, c->id, 0, 0, b->id);
        }
    } else if (imp > V_MM(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, imp >> 2, b->id);
}

/* a sensor whose box the ball is in: 1 when the ball went onto a ramp */
int8_t mb_sens(World *w, Ball *b, const Sensor *s)
{
    int8_t c = crossed(mb_x0, mb_y0, b->x, b->y, s);
    if (!c) return 0;
    if (s->kind == SN_SPIN) {
        Spinner *sp2 = &w->spin[s->idx];
        /* |vy| / 28 rad/s, at most 70: in W, |vy|_V * 16 / 7 */
        int16_t add = (int16_t)(mul16(ABS16(b->vy), 16) / 7);
        sp2->w += add < 70 * 256 ? add : 70 * 256;
        emit(w, EV_SPINHIT, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
        return 0;
    }
    if (s->kind == SN_RAMPIN) {
        if (b->vy > 0) {
            b->state = BS_RAMP; b->ramp = s->idx; b->rs = 0;
            b->rv = mulq14(hypot16(b->vx, b->vy), 15073 /* Q14(0.92) */);
            emit(w, EV_RAMPENTER, b->x, b->y, s->idx, 1, 0, b->id);
            return 1;
        }
        return 0;
    }
    if (w->nev < PB_MAX_EV) {
        emit(w, EV_SENSOR, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
        w->ev[w->nev - 1].kind = s->kind;
    }
    return 0;
}

/* a flipper within reach */
void mb_flip(World *w, Ball *b, const Flipper *f)
{
    int16_t imp;
    w->tests++;
    imp = hit_flipper(b, f);
    if (imp > V_MM(300)) emit(w, EV_FLIPHIT, b->x, b->y, f->d.side, 0, imp >> 2, b->id);
}

/* the saucer, resting on the plunger, the drain */
void mb_tail(World *w, Ball *b)
{
    Table *t = w->t;
    /* the saucer catches a ball that isn't moving too fast */
    if (b->nocatch <= 0 && ABS16(b->x - t->saucer_x) < t->saucer_r && ABS16(b->y - t->saucer_y) < t->saucer_r &&
        hypot16(b->x - t->saucer_x, b->y - t->saucer_y) < t->saucer_r && hypot16(b->vx, b->vy) < V_MM(1300)) {
        b->state = BS_SAUCER; place(b, t->saucer_x, t->saucer_y); b->vx = b->vy = 0;
        b->hold = PB_MS(1200);
        emit(w, EV_SAUCER, b->x, b->y, 0, 0, 0, b->id);
        return;
    }
    /* resting on the plunger */
    if (b->x > P_MM(478) && b->y < t->plunger_rest + P_MM(4) && hypot16(b->vx, b->vy) < V_MM(80)) {
        b->state = BS_LANE; place(b, t->plunger_x, t->plunger_rest); b->vx = b->vy = 0;
        emit(w, EV_LANEREST, b->x, b->y, 0, 0, 0, b->id);
    }
    if (b->y < P_MM(-40)) { b->state = BS_DRAINED; emit(w, EV_DRAIN, b->x, b->y, 0, 0, 0, b->id); }
}

extern void __fastcall__ move_ball(World *w, Ball *b, int16_t grav);   /* physasm.s */

/* One fixed step (1/120 s), split into substeps (see pb.h). */
/* port: |v| / 625, truncated, without a division: (|v| * 13422) >> 23 is
   exact for |v| < 32768 */
static int16_t div625(int16_t v)
{
    int16_t q = (int16_t)(mulu16((uint16_t)ABS16(v), 13422) >> 23);
    return v < 0 ? -q : q;
}

void pb_world_step(World *w)
{
    int16_t fast = 0, tip = 0, n, k, done = 0, gq, gr;
    int8_t i, j, live = 0, nb = w->nballs, nf = w->nflip;
    Ball *b, *b2;
    Flipper *f;

    /* port: pointers through the arrays (cc65 multiplies for each index) */
    for (i = 0, b = w->balls; i < nb; i++, b++) {
        if (b->nocatch > 0) b->nocatch--;
        if (b->state != BS_PLAY) continue;
        live++;
        b->vx -= div625(b->vx); b->vy -= div625(b->vy);       /* rolling resistance, 0.04 h (port: 25 Hz) */
        {   /* port: |v| from above without a square root: max + min / 2 (at most
               12% over: a substep more now and then) */
            uint16_t ax = (uint16_t)ABS16(b->vx), ay = (uint16_t)ABS16(b->vy), s;
            s = ax > ay ? ax + (ay >> 1) : ay + (ax >> 1);
            s += PB_GRAVITY_STEP;
            if (s > 32767) s = 32767;
            if ((int16_t)s > fast) fast = (int16_t)s;
        }
    }
    for (i = 0, f = w->flip; i < nf; i++, f++)
        if (flipper_moving(w, f)) {
            /* port: the tip's speed counts only when a ball could reach the
               flipper this step (at most 7000 mm/s x 1/25 s = 280 mm, 4480 P,
               beyond its reach): far from the balls a flip costs no substeps */
            int16_t reach = f->d.len + f->d.r1 + PB_R + 4480;
            for (j = 0, b = w->balls; j < nb; j++, b++)
                if (b->state == BS_PLAY && ABS16(b->x - f->d.px) < reach && ABS16(b->y - f->d.py) < reach) break;
            if (j < nb) {
                int16_t tp = (int16_t)(mul16(f->d.len + f->d.r2, 21) >> 1);   /* 42 rad/s, in V */
                if (tp > tip) tip = tp;
            }
        }
    /* port: no substep moves anything more than the ball's radius (the
       original: a quarter at 120 Hz): (fast + tip) * 0.16 P a step, 216 P:
       n = (fast + tip) / 1350, by subtraction; the rest from tables (no
       divisions) */
    {
        uint16_t sp = (uint16_t)fast + (uint16_t)tip;
        n = 1;
        if (live) while (sp > 1350 && n < 40) { sp -= 1350; n++; }
    }
    sub_n1 = n == 1;
    w->substeps = (int8_t)n;
    sub_acc_up = sub_tab[n][0];                            /* 9000 rad/s^2 / 25 / n, W (port: 25 Hz) */
    sub_acc_down = sub_tab[n][1];                          /* 5000 rad/s^2 */
    sub_kang = sub_tab[n][2];                              /* W -> A for the substep, Q16 */
    sub_kpos = sub_tab[n][3];                              /* V -> 1/256 P for the substep, Q9 */
    gq = sub_tab[n][4];                                    /* gravity a substep: PB_GRAVITY_STEP / n */
    gr = sub_tab[n][5];                                    /*   and the remainder */
    if (mb_t != w->t) pb_mb_prepare(w);                   /* port: bank 2 */
    for (k = 0; k < n; k++) {
        int16_t grav = gq + (done < gr);                  /* port: adds up to the step's, no divide */
        done++;
        for (i = 0, f = w->flip; i < nf; i++, f++) flipper_step(w, f);
        for (i = 0, b = w->balls; i < nb; i++, b++) if (b->state == BS_PLAY) move_ball(w, b, grav);
        if (nb > 1)
            for (i = 0, b = w->balls; i < nb; i++, b++)
                for (j = i + 1, b2 = b + 1; j < nb; j++, b2++)
                    if (b->state == BS_PLAY && b2->state == BS_PLAY) pb_hit_balls(b, b2);   /* port: bank 2 */
    }
    for (i = 0, b = w->balls; i < nb; i++, b++) {
        if (b->state == BS_RAMP) pb_ramp_step(w, b);    /* port: bank 2 (pb_phys2.c) */
        else if (b->state == BS_SAUCER) { b->hold--; b->vx = b->vy = 0; }
        else if (b->state == BS_LANE) { b->vx = b->vy = 0; }
    }
    /* the spinners keep turning on their own and slow down */
    for (i = 0; i < w->t->nspin; i++) {
        Spinner *sp = &w->spin[i];
        if (sp->w <= 0) continue;
        sp->w -= 51 + mulq14(sp->w, 590);                  /* (5 rad/s + 0.9 w) h (port: 25 Hz) */
        if (sp->w < 0) sp->w = 0;
        sp->ang += (int16_t)(mul16(sp->w, 16) / 25);       /* w h, A */
        while (sp->ang >= PI_A) { sp->ang -= PI_A; emit(w, EV_SPIN, w->t->spin[i].x, w->t->spin[i].y, i, 0, 0, 0); }
    }
}

