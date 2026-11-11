/*
 * ld_core.c - Lander 3D's rules (see ld.h).
 */
#include <string.h>
#include <stddef.h>
#include "a8game.h"
#include "a3d.h"
#include "ld.h"

/* all of this file's BSS (its tables and cc65's static locals, -Cl) in LOWRAM */
#pragma bss-name ("LOWRAM")

Level L;

/* the original's worlds: g, amax, limits and fuel as they are; port: the
   grid's cell, the hills in whole metres */
const LdWorld ld_worlds[W_COUNT] = {
    /* name        g     amax  cell hills rough craters max wind curve vs   hs   tilt fuel */
    { "MOON",      415, 1126,  32,  38,   128,  12,    90,  0,   0,   640, 384, 10,  62 },
    { "MARS",      950, 2304,  32,  55,   115,   5,   110,  3,   0,   768, 461, 10,  66 },
    { "ASTEROID",   90,  384,  16,  26,   159,   8,    55,  0,  11,   384, 256, 12,  85 },
};

/* ------------------------------------------------------------ random */
static uint32_t rs;

static uint32_t rng32(void)               /* mulberry32, as the original's */
{
    uint32_t t;
    rs += 0x6D2B79F5UL;
    t = rs;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}
static uint8_t rnd8(void) { return (uint8_t)(rng32() >> 24); }

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }
/* |(a, b)| within 7 % */
static int16_t dist2(int16_t a, int16_t b)
{
    a = iabs(a); b = iabs(b);
    return a > b ? a + (b >> 2) + (b >> 3) : b + (a >> 2) + (a >> 3);
}
static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------ terrain */
static int8_t lat0[4 * 4], lat1[7 * 7], lat2[13 * 13];

/* lattice coordinate and smoothstep weight (0..127) of grid point g for an
   octave of c cells */
static uint8_t lat_i, lat_s;
static void lat_at(uint8_t g, uint8_t c)
{
    uint16_t p = (uint16_t)g * c;
    uint8_t f;
    lat_i = (uint8_t)(p / (LD_N - 1));
    if (lat_i >= c) lat_i = c - 1;
    f = (uint8_t)(((p - lat_i * (LD_N - 1)) << 7) / (LD_N - 1));     /* 0..127 */
    lat_s = (uint8_t)(((uint16_t)f * f >> 7) * (384 - 2 * f) >> 8);  /* 3f^2 - 2f^3, 0..127 */
}

/* a + (b - a) * s / 128 with a, b -64..63 */
#define LERP(a, b, s) ((a) + (int16_t)(((int16_t)((b) - (a)) * (s)) >> 7))

/* add octave o (lattice lat, c cells) times amp/256 into the heights (the
   sum stays within -112..112):
   each row of the lattice is interpolated once for the grid row, then
   along it (port: the original works out every point from scratch) */
static void octave(const int8_t *lat, uint8_t c, int16_t amp)
{
    int8_t row[14];
    uint8_t i, j, k, w = c + 1, ii[LD_N], ss[LD_N];
    int16_t v;
    int8_t *a = L.h;
    for (i = 0; i < LD_N; ++i) { lat_at(i, c); ii[i] = lat_i; ss[i] = lat_s; }
    for (j = 0; j < LD_N; ++j) {
        lat_at(j, c);
        for (k = 0; k < w; ++k)
            row[k] = (int8_t)LERP(lat[lat_i * w + k], lat[(lat_i + 1) * w + k], lat_s);
        for (i = 0; i < LD_N; ++i, ++a) {
            v = LERP(row[ii[i]], row[ii[i] + 1], ss[i]);
            *a += (int8_t)((v * amp) >> 8);
        }
        ++a;                                                    /* (the stride is 32) */
    }
}

static int8_t *hp(uint8_t i, uint8_t j) { return &L.h[((uint16_t)j << 5) + i]; }

static void set_h(uint8_t i, uint8_t j, int16_t v)
{
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    *hp(i, j) = (int8_t)v;
}

/* metres from the world's centre of grid point i */
static int16_t gpos(uint8_t i) { return ((int16_t)i << L.cshift) - (int16_t)L.half; }

static void craters(const LdWorld *W)
{
    uint8_t c, i, j, rr;
    int16_t R, cx, cz, depth, rim, reach, d, dy, q, s, i0, i1, j0, j1;
    int16_t span = (int16_t)(L.half - L.half / 20);
    for (c = 0; c < W->craters; ++c) {
        rr = rnd8();
        R = 10 + (int16_t)(((uint16_t)rr * rr >> 8) * W->crater_max >> 8);
        cx = a3d_mulf(span, (int8_t)rnd8() * 2);
        cz = a3d_mulf(span, (int8_t)rnd8() * 2);
        depth = (R * (46 + (rnd8() >> 3))) >> 8;              /* 0.18..0.30 R */
        rim = (R * (15 + (rnd8() >> 4))) >> 8;                /* 0.06..0.12 R */
        reach = R + R / 2;
        i0 = (int16_t)(cx - reach + (int16_t)L.half) >> L.cshift;
        i1 = ((int16_t)(cx + reach + (int16_t)L.half) >> L.cshift) + 1;
        j0 = (int16_t)(cz - reach + (int16_t)L.half) >> L.cshift;
        j1 = ((int16_t)(cz + reach + (int16_t)L.half) >> L.cshift) + 1;
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > LD_N - 1) i1 = LD_N - 1;
        if (j1 > LD_N - 1) j1 = LD_N - 1;
        for (j = (uint8_t)j0; j <= (uint8_t)j1; ++j)
            for (i = (uint8_t)i0; i <= (uint8_t)i1; ++i) {
                d = dist2(gpos(i) - cx, gpos(j) - cz);
                if (d > reach) continue;
                d = a3d_muldiv(d, 256, R);                      /* d/R, 8.8 */
                dy = 0;
                if (d < 256) dy -= a3d_mulf(depth, 256 - a3d_mulf(d, d));
                q = d - 256;                                    /* the rim: 1 - (q/0.25)^2 */
                if (q > -64 && q < 64) {
                    s = 256 - (a3d_mulf(q, q) << 4);
                    if (s > 0) dy += (rim * s) >> 8;
                }
                set_h(i, j, *hp(i, j) + dy);
            }
    }
}

static const uint8_t pad_r4[3] = { 68, 48, 34 };     /* 17, 12, 8.5 m */
static const uint8_t pad_mult[3] = { 1, 2, 5 };

/* the pad sits in the middle of the cell (i, j)..(i+1, j+1): its four
   corners flattened to their mean, the ring round them eased towards it */
static void flatten(Pad *p, uint8_t i, uint8_t j)
{
    int16_t s = *hp(i, j) + *hp(i + 1, j) + *hp(i, j + 1) + *hp(i + 1, j + 1);
    int8_t y = (int8_t)(s / 4);
    uint8_t a, b;
    for (b = j - 1; b != (uint8_t)(j + 3); ++b)
        for (a = i - 1; a != (uint8_t)(i + 3); ++a) {
            if (a >= i && a <= i + 1 && b >= j && b <= j + 1) set_h(a, b, y);
            else set_h(a, b, (*hp(a, b) + y) / 2);
        }
    p->y = y;
    p->x = gpos(i) + L.cell / 2;
    p->z = gpos(j) + L.cell / 2;
}

static void make_terrain(const LdWorld *W, uint16_t pad_scale)
{
    uint8_t i, j, k, m, n, ok, bi = 0, bj = 0;
    int16_t y, amp1 = W->rough, amp2 = (W->rough * W->rough) >> 8;
    int16_t dx, dz, dd;
    for (k = 0; k < sizeof lat0; ++k) lat0[k] = (int8_t)rnd8() >> 1;
    for (k = 0; k < sizeof lat1; ++k) lat1[k] = (int8_t)rnd8() >> 1;
    for (k = 0; k < sizeof lat2; ++k) lat2[k] = (int8_t)rnd8() >> 1;
    octave(lat0, 3, 256);                                       /* (L.h is 0: memset by ld_new_level) */
    octave(lat1, 6, amp1);
    octave(lat2, 12, amp2);
    for (j = 0; j < LD_N; ++j)
        for (i = 0; i < LD_N; ++i) {
            y = *hp(i, j);                                      /* the octaves' sum, -112..112 */
            y = a3d_mulf(y, W->hills * 4);                     /* x hills / 64 */
            if (W->curve) {                                     /* the asteroid curves away */
                dd = dist2(gpos(i), gpos(j)) / 10;
                y -= (dd * dd * W->curve) >> 8;
            }
            set_h(i, j, y);
        }
    craters(W);
    /* pads: random cells well apart from each other (port: the original
       ranks 80 candidates by roughness, the x5 on the roughest) */
    for (m = 0; m < 3; ++m) {
        for (k = 0; k < 40; ++k) {
            bi = 4 + rnd8() % (LD_N - 9);
            bj = 4 + rnd8() % (LD_N - 9);
            ok = 1;
            for (n = 0; n < m; ++n) {
                dx = gpos(bi) - L.pads[n].x;
                dz = gpos(bj) - L.pads[n].z;
                if (dist2(dx, dz) < (int16_t)(L.half / 2)) ok = 0;
            }
            if (ok) break;
        }
        L.pads[m].r4 = (uint8_t)((pad_r4[m] * pad_scale) >> 8);
        L.pads[m].mult = pad_mult[m];
        flatten(&L.pads[m], bi, bj);
    }
}

/* ground.s does it (L's fields at fixed offsets) */
typedef char ld_check_h[offsetof(Level, h) == 7 && offsetof(Level, half) == 3 && offsetof(Level, cshift) == 6 ? 1 : -1];
extern int16_t lg_x, lg_z;
extern uint8_t lg_xf, lg_zf;
extern int16_t ld_ground_(void);

int16_t ld_ground(int16_t xm, uint8_t xf, int16_t zm, uint8_t zf)
{
    lg_x = xm; lg_xf = xf; lg_z = zm; lg_zf = zf;
    return ld_ground_();
}

int16_t ld_ground_here(void) { return ld_ground(ld_m[0], ld_frac[0], ld_m[2], ld_frac[2]); }

/* the craft's feet (2 m under its middle), 1/16 m */
static int16_t feet16(void) { return (ld_m[1] << 4) + (ld_frac[1] >> 4) - 32; }

int16_t ld_agl(void) { return feet16() - ld_ground_here(); }

/* ------------------------------------------------------------ levels */
void ld_new_level(uint8_t world, uint8_t level, uint32_t seed)
{
    const LdWorld *W = &ld_worlds[world];
    uint8_t k = level < 1 ? 0 : level - 1 > 8 ? 8 : level - 1, t, a, ast = world == W_ASTEROID;
    int16_t start_dist = ast ? 110 + 18 * k : 180 + 28 * k, sx = 0, sz = 0, x, z, lim, g, best = -32767, sc;
    int16_t start_h = ast ? 307 + 128 * k : 768 + 410 * k;      /* m/s 8.8 */
    int16_t fuel = W->fuel - (7 * k) / 2, f2 = (W->fuel * 141) >> 8;
    uint16_t pad_scale = k * 13 > 97 ? 159 : 256 - 13 * k;      /* 1 - 0.05k, >= 0.62 (/256) */
    const Pad *p1;

    memset(&L, 0, sizeof L);
    L.world = world;
    L.level = level;
    L.cell = W->cell;
    L.cshift = W->cell == 16 ? 4 : 5;
    L.half = (uint16_t)W->cell * (LD_N - 1) / 2;
    L.ap_target = -1;
    rs = seed ? seed : 0x9e3779b9UL;
    make_terrain(W, pad_scale);
    /* the start: start_dist from the x1 pad, inside the world */
    p1 = &L.pads[0];
    lim = (int16_t)(L.half - L.half / 5);
    for (t = 0; t < 16; ++t) {
        a = rnd8();
        x = p1->x + a3d_mulf(start_dist, a3d_cos(a) * 2);
        z = p1->z + a3d_mulf(start_dist, a3d_sin(a) * 2);
        sc = ((iabs(x) < lim && iabs(z) < lim) ? 1000 : 0) - dist2(x, z) / 10 + (rnd8() & 7);
        if (sc > best) { best = sc; sx = x; sz = z; }
    }
    /* above the highest ground on the way to the pad */
    g = -127;
    for (t = 0; t <= 10; ++t) {
        x = ld_ground(sx + (int16_t)((p1->x - sx) * t / 10), 0, sz + (int16_t)((p1->z - sz) * t / 10), 0) >> 4;
        if (x > g) g = x;
    }
    memset(ld_p, 0, sizeof ld_p);
    memset(ld_v, 0, sizeof ld_v);
    ((int16_t *)&ld_p[0])[1] = sx;
    ((int16_t *)&ld_p[2])[1] = sz;
    ((int16_t *)&ld_p[1])[1] = g + (ast ? 70 : 115) + 8 * k;
    /* moving roughly towards the pad (port: the original turns the direction
       by up to +-0.6 rad; here a sideways part of up to +-1/2) */
    x = p1->x - sx; z = p1->z - sz;
    g = dist2(x, z);
    a = rnd8();
    ld_v[0] = (fx)a3d_muldiv(x + a3d_mulf(z, (int8_t)a), start_h, g) << 8;
    ld_v[2] = (fx)a3d_muldiv(z - a3d_mulf(x, (int8_t)a), start_h, g) << 8;
    ld_v[1] = -FXI(3) / 2;
    ld_sync();
    if (fuel < f2) fuel = f2;
    L.fuel = L.fuel_max = (uint16_t)fuel * (LD_HZ * 16);        /* steps of full throttle x 16 */
}

/* ------------------------------------------------------------ the step */
#define RATE      910                   /* 5 degrees a step (the original's 1.1 rad/s) */
#define MAX_TILT  13653                 /* 75 degrees */

static void push_ev(uint8_t e) { if (L.nev < 8) L.ev[L.nev++] = e; }

uint8_t ld_pop_event(void)
{
    uint8_t e;
    if (!L.nev) return LDE_NONE;
    e = L.ev[0];
    memmove(L.ev, L.ev + 1, --L.nev);
    return e;
}

uint8_t ld_tilt_deg(void)
{
    int16_t p = iabs(L.pitch), r = iabs(L.roll);
    int16_t t = dist2(p >> 2, r >> 2);                          /* (65536 a turn) / 4 */
    return (uint8_t)(a3d_mulf(t, 360) >> 6);
}

static void finish(uint8_t outcome)
{
    L.outcome = outcome;
    if (outcome != OUT_CRASHED && outcome != OUT_LOST) { memset(ld_v, 0, sizeof ld_v); ld_sync(); }
    L.throttle = 0;
    push_ev(outcome == OUT_LANDED ? LDE_LANDED : outcome == OUT_OFFPAD ? LDE_OFFPAD :
            outcome == OUT_CRASHED ? LDE_CRASHED : LDE_LOST);
}

static void contact(void)
{
    const LdWorld *W = &ld_worlds[L.world];
    LdResult *r = &L.res;
    int16_t g, feet = feet16(), d, slope;
    uint8_t k, i, j;
    const Pad *p;
    if (feet > (128 << 4)) return;                              /* above any ground */
    g = ld_ground_here();
    if (feet > g) return;
    ld_p[1] += (fx)(g - feet) << 12;                            /* out of the ground */
    ld_sync();
    memset(r, 0, sizeof *r);
    r->vs = -ld_v88[1];
    r->hs = dist2(ld_v88[0], ld_v88[2]);
    r->tilt = ld_tilt_deg();
    r->vs_ok = r->vs <= W->lim_vs;
    r->hs_ok = r->hs <= W->lim_hs;
    r->tilt_ok = r->tilt <= W->lim_tilt;
    for (k = 0; k < 3; ++k) {
        p = &L.pads[k];
        d = dist2(((ld_m[0] - p->x) << 2) + (ld_frac[0] >> 6), ((ld_m[2] - p->z) << 2) + (ld_frac[2] >> 6));   /* 1/4 m */
        if (d <= (int16_t)p->r4 - 5) { r->pad = p->mult; break; }
    }
    if (r->vs_ok && r->hs_ok && r->tilt_ok && r->pad) {
        r->soft = (uint16_t)((60L * (W->lim_vs - r->vs)) / W->lim_vs + (30L * (W->lim_tilt - r->tilt)) / W->lim_tilt +
                             (20L * (W->lim_hs - r->hs)) / W->lim_hs);
        r->fuel_bonus = (uint16_t)a3d_muldiv(L.fuel >> 1, 150, L.fuel_max >> 1);
        r->score = (100 + r->soft + r->fuel_bonus) * r->pad;
        finish(OUT_LANDED);
        return;
    }
    /* off the pads: safe on gentle ground (< 14 degrees: the grid cell's
       rise under 1/4 of its run) */
    i = (uint8_t)((uint16_t)(ld_m[0] + (int16_t)L.half) >> L.cshift);
    j = (uint8_t)((uint16_t)(ld_m[2] + (int16_t)L.half) >> L.cshift);
    if (i > LD_N - 2) i = LD_N - 2;
    if (j > LD_N - 2) j = LD_N - 2;
    slope = dist2(*hp(i + 1, j) - *hp(i, j), *hp(i, j + 1) - *hp(i, j));
    if (r->vs_ok && r->hs_ok && r->tilt_ok && slope * 4 <= L.cell) {
        r->score = 25;
        finish(OUT_OFFPAD);
    } else {
        finish(OUT_CRASHED);
    }
}

LdInput ld_in;

/* sin of a binary angle (65536 a turn) in Q8, interpolated between the
   table's 256 steps (the autopilot's small tilts are under one step) */
static int16_t sinq8(int16_t a)
{
    uint8_t i = (uint8_t)((uint16_t)a >> 8), f = (uint8_t)a;
    int16_t s0 = a3d_sin8[i], s1 = a3d_sin8[(uint8_t)(i + 1)];
    return (s0 << 1) + (((s1 - s0) * f) >> 7);
}

void ld_step(void)
{
    const LdInput *in = &ld_in;
    const LdWorld *W = &ld_worlds[L.world];
    uint8_t thr = in->throttle;
    int16_t sr, cr, sp, cp, ux, uy, uz;
    int16_t A, ax, ay, az;
    ++L.t;
    if (L.outcome) return;
    if (!L.fuel) thr = 0;
    /* the attitude: the stick turns it at a fixed rate, letting go holds it */
    L.rcs = in->pitch || in->roll;
    if (in->aim) {                                              /* the autopilot: towards its angles */
        L.pitch += clamp16(in->tp - L.pitch, -RATE, RATE);
        L.roll += clamp16(in->tr - L.roll, -RATE, RATE);
    } else {
        L.pitch = clamp16(L.pitch + in->pitch * RATE, -MAX_TILT, MAX_TILT);
        L.roll = clamp16(L.roll + in->roll * RATE, -MAX_TILT, MAX_TILT);
    }
    if (L.fuel) {
        uint16_t burn = ((thr >> 4) << 1) + (L.rcs ? 1 : 0);
        if (L.fuel > burn) L.fuel -= burn;
        else { L.fuel = 0; push_ev(LDE_FUEL_OUT); }
    }
    L.throttle = thr;
    /* thrust along the craft's up: u = (-sin r, cos r cos p, cos r sin p) */
    sr = a3d_sin((uint8_t)(L.roll >> 8)); cr = a3d_cos((uint8_t)(L.roll >> 8));
    sp = a3d_sin((uint8_t)(L.pitch >> 8)); cp = a3d_cos((uint8_t)(L.pitch >> 8));
    ux = (int8_t)-sr;
    uy = (int8_t)(a3d_mul8s(cr, cp) >> 7);
    uz = (int8_t)(a3d_mul8s(cr, sp) >> 7);
    A = a3d_mulf(W->amax, thr);
    ax = a3d_mulf(A, ux * 2);
    ay = a3d_mulf(A, uy * 2) - W->g;
    az = a3d_mulf(A, uz * 2);
    if (W->wind) {                                              /* Mars: drag towards the wind */
        ax += (L.windx - ld_v88[0]) / 25;                     /* (drag 0.04 /s) */
        az += (L.windz - ld_v88[2]) / 25;
    }
    ld_acc[0] = ax; ld_acc[1] = ay; ld_acc[2] = az;
    ld_integrate();                                             /* v += a dt, p += v dt */
    contact();
    if (!L.outcome && ((uint16_t)iabs(ld_m[0]) > L.half + L.half / 7 ||
                       (uint16_t)iabs(ld_m[2]) > L.half + L.half / 7 || ld_m[1] > 3000))
        finish(OUT_LOST);
    if (W->wind && (L.t & 7) == 0) {                            /* the gusts */
        uint8_t ph = (uint8_t)(L.t >> 3);
        int16_t s = (int16_t)W->wind * (166 + (a3d_sin(ph * 3) >> 1));    /* 8.8 m/s */
        L.windx = a3d_mulf(s, a3d_cos(ph) * 2);
        L.windz = a3d_mulf(s, a3d_sin(ph) * 2);
    }
}

int8_t ld_nearest_pad(void)
{
    uint8_t k, pick = 0;
    int16_t d, best = 32767;
    for (k = 0; k < 3; ++k) {
        d = dist2(L.pads[k].x - ld_m[0], L.pads[k].z - ld_m[2]);
        d = a3d_muldiv(d, 16, L.pads[k].r4);              /* the easiest: distance / size */
        if (d < best) { best = d; pick = k; }
    }
    return (int8_t)pick;
}

/* ------------------------------------------------------------ the autopilot */
/* port: the original's plan (cruise at a clearance, then come down over the
   pad slowly, steering by tilting) as a PD controller on 8.8 maths: it sets
   the angles and the throttle (the step turns the craft at the stick's
   rate). Decides 6 times a second. */
void ld_autopilot(void)
{
    LdInput *out = &ld_in;
    const LdWorld *W = &ld_worlds[L.world];
    const Pad *pad;
    int16_t ex, ez, dist, h, axd, azd, ayd, vyd, lim, inv, tmax, g;
    uint8_t k;
    if (L.ap_target < 0) L.ap_target = ld_nearest_pad();
    pad = &L.pads[L.ap_target];
    ex = pad->x - ld_m[0];
    ez = pad->z - ld_m[2];
    dist = dist2(ex, ez);
    h = ld_agl() >> 4;                                          /* m above the ground */
    if (ex > 120) ex = 120;
    if (ex < -120) ex = -120;
    if (ez > 120) ez = 120;
    if (ez < -120) ez = -120;
    /* horizontal: 0.05 x the error - 0.6 x the velocity (8.8 m/s2) */
    lim = W->amax >> 2;
    axd = clamp16(ex * 13 - a3d_mulf(ld_v88[0], 154), -lim, lim);
    azd = clamp16(ez * 13 - a3d_mulf(ld_v88[2], 154), -lim, lim);
    /* vertical: hold 30 m while away, then down over the pad */
    L.ap_down = dist * 4 < pad->r4 * 3;                        /* within 3/4 of its radius */
    if (!L.ap_down) vyd = clamp16((30 - h) * 77, -1024, 768);  /* 0.3 x the error, -4..3 m/s */
    else if (h > 5) vyd = -clamp16(h * 38 + 128, 128, a3d_mulf(W->lim_vs, 141) + h * 13);   /* the original's: faster when high */
    else vyd = -a3d_mulf(W->lim_vs, 100);              /* (16-bit products overflow) */
    ayd = a3d_mulf(vyd - ld_v88[1], 384) + W->g;                /* 1.5 x the error + g */
    if (ayd < 32) ayd = 32;
    /* the tilt: accel / thrust (radians ~ the ratio; 10430 a radian) */
    inv = a3d_muldiv(10430, 256, ayd);
    tmax = h < 8 ? 1456 : 4551;                                 /* 8, 25 degrees */
    out->tr = clamp16(a3d_mulf(-axd, inv), -tmax, tmax);
    out->tp = clamp16(a3d_mulf(azd, inv), -tmax, tmax);
    out->aim = 1;
    g = a3d_muldiv(ayd, 255, W->amax);
    k = g > 255 ? 255 : g < 0 ? 0 : (uint8_t)g;
    out->throttle = k;
}
