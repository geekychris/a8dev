/*
 * td_ai.c - Tank Duel's enemies' minds and the autopilot (the original's
 * td_core.c). port: 16 bits, 80 ms ticks (td.h): the minds' timers count
 * half as many ticks (scouts snipe 35..59 of 60, change their way round
 * every 200).
 */
#include <stddef.h>
#include "td.h"
#include "td_int.h"

#pragma static-locals (on)

/* ---------- the enemies ---------- */
#ifdef TT
extern uint16_t tt_ai[4];
extern uint16_t a3d_ticks(void);
extern uint16_t a3d_since(uint16_t t0);
static uint16_t ta;
#define A0() ta = a3d_ticks()
#define A1(n) tt_ai[n] += a3d_since(ta)
#else
#define A0()
#define A1(n)
#endif
void think(uint8_t k)
{
    Tank *t = &G.tk[k], *p = &G.tk[0];
    pos dx, dz, speed;
    uint16_t d, to, want;
    A0();
    dx = p->x - t->x; dz = p->z - t->z; speed = vmax[t->type];
    d = td_len(dx, dz); to = td_angle_to(dx, dz); want = to;
    A1(0); A0();
    if (++t->ai_a >= 60) t->ai_a = 0;       /* port: the original's ai_timer % 60, % 200 */
    if (++t->ai_b >= 200) t->ai_b = 0;
    if (t->reload > 0) t->reload--;
    switch (t->type) {
    case TK_SCOUT:
        if (t->ai_a > 35) {            /* turn in to snipe */
            want = to;
            speed = U(2);                       /* the original's 1 unit an original tick */
            if (iabs(adiff(to, t->yaw)) < 700 && d < U(900)) td_fire(k);
        } else if (d > U(420)) want = to + t->orbit * 0x1000;
        else if (d < U(260)) want = to + t->orbit * 0x6000;
        else want = to + t->orbit * 0x4000;
        if (t->ai_b == 199) t->orbit = -t->orbit;
        break;
    case TK_MORTAR:
        if (d < U(450)) want = to + 0x8000;
        else if (d > U(750)) want = to;
        else speed = 0;
        if (t->reload == 0 && d < U(1100)) lob(k);
        break;
    case TK_BULWARK:
        if (d < U(240)) speed = 0;
        if (iabs(adiff(to, t->yaw)) < 600 && d < U(900)) td_fire(k);
        break;
    }
    A1(1); A0();
    /* something in the way ahead: steer round it */
    if (speed > 0 && td_blocked(t->x + td_scale8(U(70), td_sin8(t->yaw)), t->z + td_scale8(U(70), td_cos8(t->yaw)), TD_TANK_R))
        want = t->yaw + t->orbit * 0x2000;
    A1(2); A0();
    drive(t, want, speed);
    A1(3);
}

/* ---------- the autopilot ---------- */
uint8_t td_bot(void)
{
    const Tank *p = &G.tk[0], *e = NULL, *t;
    uint16_t best = 0, d, dd, to;
    pos dx, dz;
    uint8_t k, in = 0;
    uint16_t want;
    int16_t diff;
    if (G.over || G.won || G.between || !p->alive) return 0;
    for (k = 1, t = G.tk + 1; k < G.ntanks; k++, t++) {
        if (!t->alive) continue;
        dd = td_len(t->x - p->x, t->z - p->z);
        if (!e || dd < best) { e = t; best = dd; }
    }
    if (!e) return 0;
    dx = e->x - p->x;
    dz = e->z - p->z;
    d = best;
    to = td_angle_to(dx, dz);
    /* a bulwark: circle it at 320, the way it is already turning away
     * from, until well off its shield; then stop, aim and fire */
    if (e->type == TK_BULWARK) {
        int16_t rel = adiff(td_angle_to(-dx, -dz), e->yaw), side = rel >= 0 ? 1 : -1, lean;
        if (iabs(rel) < SHIELD_ARC + 2600) {
            lean = (int16_t)(((int16_t)d - U(320)) >> 4);      /* units off 320, x 40, within +-0x2000 */
            if (lean > 204) lean = 204;
            if (lean < -204) lean = -204;
            lean *= 40;
            want = to - side * (0x4000 - lean);
            in |= TD_IN_UP;
        } else want = to;
    } else {
        want = to;
        if (d > U(420)) in |= TD_IN_UP;
        else if (d < U(230)) in |= TD_IN_DOWN;
    }
    /* out of a mortar's ring */
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        const Mortar *m = &G.mo[k];
        if (m->alive && td_near(p->x - m->tx, p->z - m->tz, U(TD_SPLASH_R + 40))) {
            want = td_angle_to(p->x - m->tx, p->z - m->tz);
            in = (in & ~TD_IN_DOWN) | TD_IN_UP;
        }
    }
    /* something in the way */
    if ((in & TD_IN_UP) && td_blocked(p->x + td_scale8(U(60), td_sin8(p->yaw)), p->z + td_scale8(U(60), td_cos8(p->yaw)), TD_TANK_R))
        want = p->yaw + 0x3000;
    diff = adiff(want, p->yaw);
    if (diff > 500) in |= TD_IN_RIGHT;           /* port: 250 an original tick */
    if (diff < -500) in |= TD_IN_LEFT;
    /* shoot when lined up on an enemy and not at its shield */
    for (k = 1, t = G.tk + 1; k < G.ntanks; k++, t++) {
        pos tdx, tdz;
        if (!t->alive) continue;
        tdx = t->x - p->x;
        tdz = t->z - p->z;
        if (iabs(adiff(td_angle_to(tdx, tdz), p->yaw)) > 450 || td_beyond(tdx, tdz, U(1000))) continue;
        if (t->type == TK_BULWARK && iabs(adiff(td_angle_to(-tdx, -tdz), t->yaw)) < SHIELD_ARC) continue;
        if (p->reload == 0) in |= TD_IN_FIRE;
    }
    return in;
}
