/*
 * od_core.c - Orbit Docking's rules, from the Amiga original's od_core.c:
 * thrusters and roll in free space, the spinning station, the docking
 * check, crashes, debris, fuel, the levels, and the autopilot (lines up on
 * the axis, matches the slot's roll and spin, then closes in slowly,
 * holding back while debris crosses).
 *
 * port: 16 bits where the original has 16.16 (positions in 1/16 units with
 * a 1/256 remainder, speeds in 1/256 units a tick: cc65's 32-bit code is
 * big and slow); the thrust turned by the roll is a 14-bit sine times the
 * thrust; distances from the axis are compared squared; the debris swing by
 * an angle that steps each tick (the original: level_ticks % period); sines
 * from a 256-step table.
 */
#include <string.h>
#include <cc65.h>
#include "od.h"

#pragma bss-name ("LOWRAM")

ODGame G;

const char *const od_fail_names[8] = {
    "", "OFF THE AXIS", "TOO FAST", "DRIFTING ACROSS", "ROLLED WRONG", "NOT TURNING WITH IT", "HIT THE HUB", "HIT DEBRIS"
};

/* by level: spin (degrees a second), start offset across, fuel, debris */
static const int8_t lv_spin[OD_LEVELS] = { 4, 7, 10, 10, 14, -18 };
static const uint8_t lv_offset[OD_LEVELS] = { 0, 60, 140, 100, 160, 180 };
static const uint16_t lv_fuel[OD_LEVELS] = { 600, 550, 500, 500, 550, 700 };
static const uint8_t lv_debris[OD_LEVELS] = { 0, 0, 0, 1, 2, 3 };

#define U(v)  ((v) * 16)                 /* units -> positions */

/* sin of a 16-bit angle, Q14, from a quarter wave in 64 steps */
static const int16_t quarter[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};

int16_t od_sin(uint16_t a)
{
    uint8_t i = (uint8_t)(a >> 8), q = i & 63;
    int16_t v;
    switch (i >> 6) {
    case 0: v = quarter[q]; break;
    case 1: v = quarter[64 - q]; break;
    case 2: v = -quarter[q]; break;
    default: v = -quarter[64 - q]; break;
    }
    return v;
}

static void event(uint8_t type, uint8_t a, uint16_t b)
{
    uint8_t n = G.nev;
    if (n < OD_MAX_EVENTS) { G.ev[n] = type; G.eva[n] = a; G.evb[n] = b; G.nev = n + 1; }
}

uint8_t od_pop_event(uint8_t *a, uint16_t *b)
{
    uint8_t e;
    if (!G.nev) return ODE_NONE;
    e = G.ev[0]; *a = G.eva[0]; *b = G.evb[0];
    --G.nev;
    memmove(G.ev, G.ev + 1, G.nev);
    memmove(G.eva, G.eva + 1, G.nev);
    memmove(G.evb, G.evb + 1, G.nev * 2);
    return e;
}

/* mulberry32 */
static uint32_t rnd(void)
{
    uint32_t t = (G.rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

static int16_t iabs(int16_t a) { return a < 0 ? -a : a; }
static int32_t labs_(int32_t a) { return a < 0 ? -a : a; }

int16_t od_roll_error(void)
{
    uint16_t d = (uint16_t)((G.roll - G.st_angle) >> 8) & 0x7FFF;   /* the slot repeats every half turn */
    return d >= 0x4000 ? (int16_t)(d - 0x8000) : (int16_t)d;
}

int16_t od_debris_x(uint8_t k)
{
    return (G.d_amp[k] * (od_sin((uint16_t)(G.d_ang[k] >> 16)) >> 7)) >> 7;
}

/* (x^2 + y^2) of the craft, in 1/16 units squared */
int32_t od_lat2(void)
{
    uint16_t x = G.x < 0 ? -G.x : G.x, y = G.y < 0 ? -G.y : G.y;
    return (int32_t)(umul16x16r32(x, x) + umul16x16r32(y, y));
}
#define lat2 od_lat2
#define SQ16(u) ((int32_t)(u) * (u) * 256)

static void place(void)
{
    uint16_t a = (uint16_t)rnd();
    int16_t off = lv_offset[G.level - 1];
    G.x = (int16_t)(((int32_t)off * od_cos(a)) >> 10);    /* off * cos (Q14), 1/16 units */
    G.y = (int16_t)(((int32_t)off * od_sin(a)) >> 10);
    G.z = U(OD_START_Z);
    G.xf = G.yf = G.zf = 0;
    G.vx = G.vy = G.vz = 0;
    G.roll = (int32_t)(rnd() & 0xFFFF) << 8;
    G.roll_rate = 0;
}

void od_load_level(uint8_t level)
{
    uint8_t k;
    uint16_t period;
    G.level = level;
    G.st_angle = 0;
    G.st_spin = (int32_t)lv_spin[level - 1] * 65536L / 360 * 256 / 25;
    G.fuel = G.fuel_max = lv_fuel[level - 1] * 2;
    G.ndebris = lv_debris[level - 1];
    for (k = 0; k < G.ndebris; k++) {
        G.d_z[k] = 520 - k * 140;
        G.d_y[k] = (int16_t)(rnd() % 61) - 30;
        G.d_amp[k] = 140 + (int16_t)(rnd() % 60);
        period = 150 + (uint16_t)(rnd() % 100);
        G.d_step[k] = 0xFFFFFFFFUL / period;
        G.d_ang[k] = (rnd() & 0xFFFF) << 16;
    }
    G.level_ticks = 0;
    place();
}

void od_new_game(uint32_t seed, uint8_t level)
{
    memset(&G, 0, sizeof(G));
    G.seed = G.rng = seed;
    G.lives = OD_LIVES;
    if (level < 1) level = 1;
    if (level > OD_LEVELS) level = OD_LEVELS;
    od_load_level(level);
}

static void crash(uint8_t why)
{
    G.crashes++;
    G.lives--;
    G.last_fail = why;
    event(why == 7 ? ODE_DEBRIS : ODE_CRASH, why, G.lives);
    if (G.lives == 0) { G.over = 1; event(ODE_OVER, 0, G.score); return; }
    event(ODE_LOST, G.lives, 0);
    G.fuel = G.fuel_max;
    place();
}

static void contact(void)
{
    int32_t l2 = lat2(), rr;
    uint8_t why = 0;
    int16_t bonus;
    if (l2 > SQ16(OD_HUB_R)) return;                                /* it misses the hub's face */
    rr = G.roll_rate - G.st_spin;
    if (l2 > SQ16(OD_TOL_AXIS)) why = 1;
    else if (-G.vz > OD_TOL_CLOSE) why = 2;
    else if (iabs(G.vx) > OD_TOL_DRIFT || iabs(G.vy) > OD_TOL_DRIFT) why = 3;
    else if (od_roll_error() > OD_TOL_ROLL || od_roll_error() < -OD_TOL_ROLL) why = 4;
    else if (labs_(rr) > OD_TOL_SPIN) why = 5;
    if (why) { crash(why); return; }
    bonus = 1000 + (G.fuel >> 1) * 2 + 500 * G.level - (G.level_ticks / 25) * 2;
    if (bonus < 500) bonus = 500;
    G.score += bonus;
    G.docks++;
    G.between = OD_BETWEEN;
    G.z = U(OD_PORT_Z + OD_NOSE);
    G.zf = 0;
    G.vx = G.vy = G.vz = 0;
    G.roll_rate = G.st_spin;
    event(ODE_DOCK, G.level, bonus);
}

/* fuel spent: cost half units; 0 = out */
static uint8_t spend(uint8_t cost)
{
    if (G.fuel <= cost) {
        G.fuel = 0;
        event(ODE_FUEL_OUT, 0, 0);
        return 0;
    }
    G.fuel -= cost;
    return 1;
}

/* the thrust (0.04 a tick^2: 10/256) times a Q14 value, rounded */
#define THRUST(q) ((int16_t)(((int16_t)((q) >> 4) * OD_ACC + 512) >> 10))

/* p (1/16 units) and f (its 1/256 remainder) moved by v (1/256 units) */
static void move(int16_t *p, uint8_t *f, int16_t v)
{
    int16_t t = *f + v;
    *p += t >> 4;
    *f = (uint8_t)(t & 15);
}

void od_tick(uint8_t in)
{
    int8_t bx, by;
    uint8_t k;
    uint16_t a;
    int16_t c, s, zprev;
    int32_t q;

    if (G.over || G.won) return;
    G.tick++;
    G.st_angle = (G.st_angle + G.st_spin) & 0xFFFFFFL;
    if (G.between) {
        G.roll = (G.roll + G.roll_rate) & 0xFFFFFFL;                /* docked: turning with it */
        if (--G.between == 0) {
            if (G.level >= OD_LEVELS) { G.won = 1; event(ODE_WIN, 0, G.score); return; }
            od_load_level(G.level + 1);
            event(ODE_LEVEL, G.level, 0);
        }
        return;
    }
    G.level_ticks++;
    for (k = 0; k < G.ndebris; k++) G.d_ang[k] += G.d_step[k];
    /* the thrusters across are the craft's own: right is the view's right,
       whatever the roll (looking back down the axis, the view's right is
       the station's (-cos, -sin) of the roll, its up (-sin, cos)) */
    bx = (in & OD_IN_RIGHT ? 1 : 0) - (in & OD_IN_LEFT ? 1 : 0);
    by = (in & OD_IN_UP ? 1 : 0) - (in & OD_IN_DOWN ? 1 : 0);
    if ((bx || by) && G.fuel) {
        a = (uint16_t)(G.roll >> 8);
        c = od_cos(a); s = od_sin(a);
        q = 0;
        if (bx > 0) q -= c; else if (bx < 0) q += c;
        if (by > 0) q -= s; else if (by < 0) q += s;
        G.vx += THRUST(q);
        q = 0;
        if (bx > 0) q -= s; else if (bx < 0) q += s;
        if (by > 0) q += c; else if (by < 0) q -= c;
        G.vy += THRUST(q);
        spend((bx ? 2 : 0) + (by ? 2 : 0));
    }
    if ((in & OD_IN_FWD) && G.fuel) { G.vz -= OD_ACC; spend(2); }
    if ((in & OD_IN_BACK) && G.fuel) { G.vz += OD_ACC; spend(2); }
    if (G.fuel && (in & (OD_IN_ROLL_L | OD_IN_ROLL_R))) {
        G.roll_rate += (in & OD_IN_ROLL_R) ? OD_ROLL_ACC : -OD_ROLL_ACC;
        spend(1);
    }
    G.roll = (G.roll + G.roll_rate) & 0xFFFFFFL;
    zprev = G.z;
    move(&G.x, &G.xf, G.vx);
    move(&G.y, &G.yf, G.vy);
    move(&G.z, &G.zf, G.vz);
    /* drifted right away: lost in space */
    if (G.z > U(1600) || iabs(G.x) > U(900) || iabs(G.y) > U(900)) { crash(1); return; }
    /* debris */
    for (k = 0; k < G.ndebris; k++) {
        if (iabs(G.z - U(G.d_z[k])) < U(22) && iabs(G.y - U(G.d_y[k])) < U(26) &&
            iabs(G.x - U(od_debris_x(k))) < U(26)) { crash(7); return; }
    }
    /* the hub: its face, and its side */
    if (G.z <= U(OD_PORT_Z + OD_NOSE) && zprev > U(OD_PORT_Z + OD_NOSE)) contact();
    else if (G.z < U(OD_PORT_Z + OD_NOSE) && G.z > -U(300) && lat2() < SQ16(OD_HUB_R + 10)) crash(6);
}

/* ---------- the autopilot ---------- */

uint16_t od_isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint16_t)r;
}

/* ticks to cover d starting at speed v0 (both towards the station),
   thrusting up to speed vc; port: in 1/256 units, the thrust 10 */
static int16_t ticks_to(int32_t d, int16_t v0, int16_t vc)
{
    int16_t t_acc;
    int32_t d_acc;
    if (d <= 0) return 0;
    if (v0 < 0) v0 = 0;
    if (v0 >= vc) return (int16_t)(d / vc);
    t_acc = (vc - v0) / OD_ACC;
    d_acc = (int32_t)(v0 + vc) / 2 * t_acc;
    if (d <= d_acc) return (int16_t)((od_isqrt((int32_t)v0 * v0 + 2L * OD_ACC * d) - v0) / OD_ACC);
    return t_acc + (int16_t)((d - d_acc) / vc);
}



static int16_t clampi(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }

uint8_t od_bot(void)
{
    uint8_t in = 0, k, aligned, clear;
    int16_t wx, wy, br, bu, want, err, c, s, t0, t1, t, ahead, v8, dr, rw;
    int32_t d;
    uint16_t a;
    uint32_t ang, step3;
    if (G.over || G.won || G.between) return 0;
    /* across: towards the axis, gently - the speed wanted (x / 30 a tick, at
       most 1), turned into the craft's own right and up (1/256 units) */
    wx = clampi(-G.x / 15, -32, 32) * 8 - G.vx;
    wy = clampi(-G.y / 15, -32, 32) * 8 - G.vy;
    a = (uint16_t)(G.roll >> 8);
    c = od_cos(a) >> 7; s = od_sin(a) >> 7;            /* Q7 */
    wx >>= 2; wy >>= 2;                                 /* port: in 16 bits */
    br = (-wx * c - wy * s) >> 5;
    bu = (-wx * s + wy * c) >> 5;
    if (br > 8) in |= OD_IN_RIGHT;                      /* 0.03 */
    else if (br < -8) in |= OD_IN_LEFT;
    if (bu > 8) in |= OD_IN_UP;
    else if (bu < -8) in |= OD_IN_DOWN;
    /* roll: onto the slot, turning with it (port: in 1/16 of the a8 units, 16 bits) */
    err = od_roll_error();
    d = (G.roll_rate - G.st_spin) >> 4;
    dr = d > 32767L ? 32767 : d < -32768L ? -32768 : (int16_t)d;
    rw = -(err + err / 3);                              /* - err * 256 / 12, / 16 */
    if (rw > (140 << 4)) rw = 140 << 4;
    if (rw < -(140 << 4)) rw = -(140 << 4);
    if (dr < rw - (3 << 4)) in |= OD_IN_ROLL_R;
    else if (dr > rw + (3 << 4)) in |= OD_IN_ROLL_L;
    /* along: in slowly once lined up; hold off otherwise */
    aligned = lat2() < SQ16(6) && err < 700 && err > -700 && dr < (8 << 4) && dr > -(8 << 4) &&
              iabs(G.vx) < 77 && iabs(G.vy) < 77;     /* 0.3 */
    if (aligned) want = -clampi((G.z - U(OD_PORT_Z + OD_NOSE)) / 15 * 4, 179, 768);   /* (z - 80) / 60, 0.7..3 */
    else if (G.z > U(300)) want = -154;                /* 0.6 */
    else want = 0;
    /* debris ahead: through its slice at 1.5 a tick, and only when it will
       be clear of us the whole way across; otherwise wait short of it, or
       back out if already close */
    for (k = 0; k < G.ndebris; k++) {
        ahead = G.z - U(G.d_z[k]);
        if (ahead <= -U(26) || ahead > U(450)) continue;
        if (want > -384) want = -384;                   /* 1.5 */
        v8 = -G.vz;
        t0 = ticks_to((int32_t)(ahead - U(30)) * 16, v8, 384);
        t1 = ticks_to((int32_t)(ahead + U(30)) * 16, v8, 384);
        if (t0 < 0) t0 = 0;
        clear = 1;
        t = t0 - 6;
        /* port: the swing's angle at tick t stepped, not worked out each time */
        ang = G.d_ang[k] + (t > 0 ? (uint32_t)t * G.d_step[k] : 0);
        step3 = G.d_step[k] * 3;
        for (; t <= t1 + 6 && clear; t += 3) {
            if (iabs(U((G.d_amp[k] * (od_sin((uint16_t)(ang >> 16)) >> 7)) >> 7) - G.x) < U(70)) clear = 0;
            if (t >= 0) ang += step3;
            else if (t + 3 > 0) ang += (uint32_t)(t + 3) * G.d_step[k];
        }
        if (!clear) want = ahead > U(60) ? 0 : 205;     /* 0.8 */
    }
    if (G.vz > want + OD_ACC) in |= OD_IN_FWD;
    else if (G.vz < want - OD_ACC) in |= OD_IN_BACK;
    return in;
}
