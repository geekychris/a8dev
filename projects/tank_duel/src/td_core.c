/*
 * td_core.c - Tank Duel's rules (the original's td_core.c): driving and
 * collisions, shells (banked once off the walls), the bulwark's shield,
 * mortar rounds and their splash, crates, rounds. The enemies' minds and
 * the autopilot are in td_ai.c. port: 16 bits and a tick of 80 ms (td.h);
 * the changes are marked "port:".
 */
#include <string.h>
#include "td.h"
#include "td_int.h"

#pragma static-locals (on)

#pragma bss-name (push, "LOWRAM")             /* ($0C00-$1FFF: not zeroed; td_new_game clears it) */
TDGame G;
#pragma bss-name (pop)

const char *const td_tank_names[TK_COUNT] = { "PLAYER", "SCOUT", "MORTAR", "BULWARK" };

/* by type: top speed (pos a tick: the original's 6, 7, 3, 4 units an
   original tick, doubled), turn a tick (doubled), hits it takes, reload
   (ticks: halved) */
const pos vmax[TK_COUNT] = { U(12), U(14), U(6), U(8) };
const uint16_t turn[TK_COUNT] = { 1400, 1300, 600, 200 };
static const int8_t hits[TK_COUNT] = { TD_HP, 1, 2, 2 };
const uint8_t reload_t[TK_COUNT] = { TD_RELOAD, 30, 55, 28 };
#define MORTAR_FLIGHT 23                /* port: 45 original ticks */

/* the enemies each round: scouts, mortars, bulwarks */
static const uint8_t rounds[TD_ROUNDS][3] = {
    { 2, 0, 0 }, { 2, 0, 1 }, { 1, 1, 1 }, { 2, 0, 2 }, { 2, 2, 0 }, { 1, 1, 2 }, { 2, 2, 2 }, { 1, 2, 3 }
};

void event(uint8_t type, uint8_t a, int16_t b, pos x, pos z)
{
    if (G.nev < TD_MAX_EVENTS) {
        TDEvent *e = &G.ev[G.nev++];
        e->type = type; e->a = a; e->b = b; e->x = x; e->z = z;
    }
}

uint8_t td_pop_event(TDEvent *e)
{
    if (!G.nev) return 0;
    *e = G.ev[0];
    memmove(G.ev, G.ev + 1, (G.nev - 1) * sizeof(TDEvent));
    G.nev--;
    return 1;
}

/* mulberry32 (the original's) */
uint32_t td_rand(void)
{
    uint32_t t = (G.rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

int16_t adiff(uint16_t a, uint16_t b) { return (int16_t)(a - b); }

uint16_t td_angle_to(pos dx, pos dz) { return td_atan2(dx, dz); }

/* |(dx, dz)| < r: the length only when both are below r */
uint8_t td_near(pos dx, pos dz, pos r)
{
    if (dx >= r || dx <= -r || dz >= r || dz <= -r) return 0;
    return td_len(dx, dz) < (uint16_t)r;
}

uint8_t td_beyond(pos dx, pos dz, pos r)
{
    if (dx > r || dx < -r || dz > r || dz < -r) return 1;
    return td_len(dx, dz) > (uint16_t)r;
}

uint8_t td_enemies_left(void)
{
    uint8_t k, n = 0;
    for (k = 1; k < G.ntanks; k++) if (G.tk[k].alive) n++;
    return n;
}

uint8_t td_blocked(pos x, pos z, uint8_t r)
{
    uint8_t k;
    pos lim = U(TD_ARENA - r);
    const Obstacle *o;
    if (x > lim || x < -lim || z > lim || z < -lim) return 1;
    oq_x = x; oq_z = z; oq_extra = r; oq_from = 0;
    while ((k = obs_next()) != 0xFF) {          /* port: the boxes in asm (obs.s) */
        o = &G.ob[k];
        if (o->kind == OB_CRATE || td_len(x - o->x, z - o->z) < (uint16_t)U(o->r + r)) return 1;
        oq_from = k + 1;
    }
    return 0;
}

uint8_t td_add_tank(uint8_t type, pos x, pos z, uint16_t yaw)
{
    Tank *t;
    if (G.ntanks >= TD_MAX_TANKS) return 0xFF;
    t = &G.tk[G.ntanks];
    memset(t, 0, sizeof(*t));
    t->type = type;
    t->alive = 1;
    t->hp = hits[type];
    t->x = x;
    t->z = z;
    t->yaw = yaw;
    t->reload = (int8_t)(reload_t[type] / 2 + (uint8_t)(td_rand() % 15));
    t->orbit = (td_rand() & 1) ? 1 : -1;
    return G.ntanks++;
}

static void place_obstacles(void)
{
    uint8_t k, tries, j, ok;
    static const uint8_t kinds[13] = { OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PYRAMID, OB_PYRAMID, OB_PYRAMID,
                                       OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE };
    G.nobs = 0;
    for (k = 0; k < 13; k++) {
        Obstacle *o = &G.ob[G.nobs];
        o->kind = kinds[k];
        o->r = o->kind == OB_PILLAR ? 26 : o->kind == OB_PYRAMID ? 50 : 34;
        o->hp = o->kind == OB_CRATE ? 2 : 1;
        for (tries = 0; tries < 40; tries++) {
            pos x = U((int16_t)(td_rand() % 1500) - 750), z = U((int16_t)(td_rand() % 1300) - 650);
            ok = 1;
            if (x < U(220) && x > -U(220) && z < -U(450)) ok = 0;   /* clear of the player's start */
            for (j = 0; j < G.nobs && ok; j++)
                if (td_near(x - G.ob[j].x, z - G.ob[j].z, U(o->r + G.ob[j].r + 90))) ok = 0;
            if (ok) { o->x = x; o->z = z; G.nobs++; break; }
        }
    }
}

void td_load_round(uint8_t round)
{
    uint8_t k, n = 0, t, j;
    int8_t keep = G.tk[0].hp;
    G.round = round;
    memset(G.sh, 0, sizeof(G.sh));
    memset(G.mo, 0, sizeof(G.mo));
    place_obstacles();
    obs_base = G.ob;
    obs_n = G.nobs;
    G.ntanks = 0;
    td_add_tank(TK_PLAYER, 0, -U(800), 0);
    if (round > 1) G.tk[0].hp = keep + 30 > TD_HP ? TD_HP : keep + 30;
    G.tk[0].reload = 0;
    for (t = 0; t < 3; t++)
        for (k = 0; k < rounds[round - 1][t]; k++, n++) {
            pos x = U(-600 + (int16_t)((n * 397 + (int16_t)(td_rand() % 160)) % 1200));
            pos z = U(450 + (int16_t)(td_rand() % 350));
            for (j = 0; j < 20 && td_blocked(x, z, TD_TANK_R + 10); j++) x += U(60);
            td_add_tank(TK_SCOUT + t, x, z, 0x8000);
        }
}

void td_new_game(uint32_t seed, uint8_t round)
{
    memset(&G, 0, sizeof(G));
    G.seed = G.rng = seed;
    if (round < 1) round = 1;
    if (round > TD_ROUNDS) round = TD_ROUNDS;
    td_load_round(round);
    G.tk[0].hp = TD_HP;
}

/* ---------- moving and colliding ---------- */
/* (dx, dz) * reach / d: out to the reach along (dx, dz) */
static pos along(pos v, pos reach, uint16_t d)
{
    return (pos)(((int32_t)v * reach) / (int16_t)d);
}

void push_out(Tank *t)
{
    uint8_t k;
    pos lim = U(TD_ARENA - TD_TANK_R), dx, dz, reach, px, pz;
    uint16_t d;
    const Obstacle *o;
    oq_extra = TD_TANK_R;
    oq_from = 0;
    for (;;) {
        oq_x = t->x;                            /* (asked again from where it was pushed to) */
        oq_z = t->z;
        if ((k = obs_next()) == 0xFF) break;    /* port: the boxes in asm (obs.s) */
        oq_from = k + 1;
        o = &G.ob[k];
        reach = U(o->r + TD_TANK_R);
        dx = t->x - o->x;
        dz = t->z - o->z;
        if (o->kind == OB_CRATE) {
            px = reach - (dx < 0 ? -dx : dx);
            pz = reach - (dz < 0 ? -dz : dz);
            if (px < pz) t->x += dx < 0 ? -px : px;
            else t->z += dz < 0 ? -pz : pz;
        } else {
            d = td_len(dx, dz);
            if (d >= (uint16_t)reach) continue;
            if (d < 16) { dx = 16; d = 16; }
            t->x = o->x + along(dx, reach, d);
            t->z = o->z + along(dz, reach, d);
        }
    }
    if (t->x > lim) t->x = lim;
    if (t->x < -lim) t->x = -lim;
    if (t->z > lim) t->z = lim;
    if (t->z < -lim) t->z = -lim;
}

static void tanks_apart(void)
{
    uint8_t i, j;
    Tank *a, *b;
    pos dx, dz, push;
    uint16_t d;
    for (i = 0, a = G.tk; i < G.ntanks; i++, a++) {
        if (!a->alive) continue;
        for (j = i + 1, b = a + 1; j < G.ntanks; j++, b++) {
            if (!b->alive) continue;
            dx = b->x - a->x;
            dz = b->z - a->z;
            if (!td_near(dx, dz, U(2 * TD_TANK_R))) continue;
            d = td_len(dx, dz);
            if (d < 16) { dx = 16; d = 16; }
            push = (pos)((U(2 * TD_TANK_R) - (int16_t)d) / 2);
            dx = along(dx, push, d);
            dz = along(dz, push, d);
            a->x -= dx; a->z -= dz;
            b->x += dx; b->z += dz;
        }
    }
}

/* turn towards heading `want` and drive at `speed` (negative: back) */
void drive(Tank *t, uint16_t want, pos speed)
{
    int16_t d = adiff(want, t->yaw), r = (int16_t)turn[t->type];
    if (d > r) d = r;
    if (d < -r) d = -r;
    t->yaw += d;
    /* the original's 0.5 / 0.6 units an original tick, twice */
    if (t->v < speed) { t->v += U(2); if (t->v > speed) t->v = speed; }
    else if (t->v > speed) { t->v -= 38; if (t->v < speed) t->v = speed; }
    t->x += td_scale(t->v, td_sin(t->yaw));
    t->z += td_scale(t->v, td_cos(t->yaw));
    push_out(t);
}

uint8_t td_fire(uint8_t tank)
{
    Tank *t = &G.tk[tank];
    uint8_t k;
    int16_t fx = td_sin(t->yaw), fz = td_cos(t->yaw);
    Shell *s = G.sh;
    if (t->reload > 0 || !t->alive) return 0;
    for (k = 0; k < TD_MAX_SHELLS; k++, s++) if (!s->alive) break;
    if (k == TD_MAX_SHELLS) return 0;
    s->alive = 1;
    s->owner = tank;
    s->bounces = 1;
    s->life = TD_SHELL_LIFE;
    s->x = t->x + td_scale(U(TD_TANK_R + 6), fx);
    s->z = t->z + td_scale(U(TD_TANK_R + 6), fz);
    s->dx = td_scale(TD_SHELL_STEP, fx);
    s->dz = td_scale(TD_SHELL_STEP, fz);
    t->reload = (int8_t)reload_t[t->type];
    if (tank == 0) G.shots++;
    event(tank ? TDE_EFIRE : TDE_FIRE, tank, 0, t->x, t->z);
    return 1;
}

void lob(uint8_t tank)
{
    Tank *t = &G.tk[tank], *p = &G.tk[0];
    uint8_t k;
    Mortar *m = G.mo;
    for (k = 0; k < TD_MAX_MORTARS; k++, m++) if (!m->alive) break;
    if (k == TD_MAX_MORTARS) return;
    m->alive = 1;
    m->owner = tank;
    m->t = 0;
    m->flight = MORTAR_FLIGHT;
    m->x0 = t->x;
    m->z0 = t->z;
    /* where the player will be in half the flight */
    m->tx = p->x + td_scale(p->v, td_sin(p->yaw)) * (MORTAR_FLIGHT / 2);
    m->tz = p->z + td_scale(p->v, td_cos(p->yaw)) * (MORTAR_FLIGHT / 2);
    t->reload = (int8_t)reload_t[t->type];
    event(TDE_LOB, tank, k, m->tx, m->tz);
}

static void damage(uint8_t tank, uint8_t amount, pos x, pos z)
{
    Tank *t = &G.tk[tank];
    if (!t->alive) return;
    if (tank == 0) {
        t->hp -= amount;
        G.damage_taken += amount;
        event(TDE_HURT, amount, t->hp, x, z);
        if (t->hp <= 0) { t->hp = 0; t->alive = 0; G.over = 1; event(TDE_DEAD, 0, 0, t->x, t->z); }
        return;
    }
    if (--t->hp > 0) { event(TDE_HIT, tank, t->hp, x, z); return; }
    t->alive = 0;
    G.kills++;
    G.score += t->type == TK_SCOUT ? 100 : t->type == TK_MORTAR ? 200 : 300;
    event(TDE_KILL, tank, t->type, t->x, t->z);
}

/* ---------- shells ---------- */
static void shells(void)
{
    uint8_t k, s, j;
    Shell *sh = G.sh;
    const pos lim = U(TD_ARENA);
    for (k = 0; k < TD_MAX_SHELLS; k++, sh++) {
        if (!sh->alive) continue;
        if (--sh->life == 0) { sh->alive = 0; continue; }
        for (s = 0; s < TD_SHELL_STEPS && sh->alive; s++) {
            sh->x += sh->dx;
            sh->z += sh->dz;
            /* the walls: one bounce, then it's spent */
            if (sh->x > lim || sh->x < -lim || sh->z > lim || sh->z < -lim) {
                if (!sh->bounces) { sh->alive = 0; event(TDE_SPLASH, 0, 0, sh->x, sh->z); break; }
                sh->bounces--;
                if (sh->x > lim || sh->x < -lim) { sh->dx = -sh->dx; sh->x = sh->x > 0 ? 2 * lim - sh->x : -2 * lim - sh->x; }
                if (sh->z > lim || sh->z < -lim) { sh->dz = -sh->dz; sh->z = sh->z > 0 ? 2 * lim - sh->z : -2 * lim - sh->z; }
                event(TDE_BOUNCE, k, 0, sh->x, sh->z);
            }
            /* obstacles (port: the boxes in asm, obs.s) */
            oq_x = sh->x; oq_z = sh->z; oq_extra = 0; oq_from = 0;
            while ((j = obs_next()) != 0xFF) {
                Obstacle *o = &G.ob[j];
                oq_from = j + 1;
                if (o->kind == OB_CRATE) {
                    sh->alive = 0;
                    if (--o->hp == 0) { if (sh->owner == 0) G.score += 20; event(TDE_CRATE_BREAK, j, 0, o->x, o->z); }
                    else event(TDE_CRATE_HIT, j, o->hp, sh->x, sh->z);
                    break;
                } else if (td_len(sh->x - o->x, sh->z - o->z) < (uint16_t)U(o->r)) {
                    sh->alive = 0;
                    event(TDE_SPLASH, 1, 0, sh->x, sh->z);
                    break;
                }
            }
            if (!sh->alive) break;
            /* tanks: its own only after a bounce */
            {
                Tank *t = G.tk;
                for (j = 0; j < G.ntanks; j++, t++) {
                    pos dx, dz;
                    if (!t->alive || (j == sh->owner && sh->bounces)) continue;
                    if (sh->owner != 0 && j != 0) continue;         /* enemies' shells pass their own side */
                    dx = sh->x - t->x;
                    if (dx >= U(TD_TANK_R) || dx <= -U(TD_TANK_R)) continue;
                    dz = sh->z - t->z;
                    if (dz >= U(TD_TANK_R) || dz <= -U(TD_TANK_R)) continue;
                    if (td_len(dx, dz) >= U(TD_TANK_R)) continue;
                    if (t->type == TK_BULWARK && iabs(adiff(td_angle_to(dx, dz), t->yaw)) < SHIELD_ARC) {
                        /* off the shield */
                        event(TDE_GLANCE, j, 0, sh->x, sh->z);
                        if (sh->bounces) {
                            uint16_t a = 2 * t->yaw - td_angle_to(-sh->dx, -sh->dz);
                            sh->bounces--;
                            sh->dx = td_scale(TD_SHELL_STEP, td_sin(a));
                            sh->dz = td_scale(TD_SHELL_STEP, td_cos(a));
                            sh->x = t->x + td_scale(U(TD_TANK_R + 4), td_sin(a));
                            sh->z = t->z + td_scale(U(TD_TANK_R + 4), td_cos(a));
                            sh->owner = j;
                        } else sh->alive = 0;
                        break;
                    }
                    sh->alive = 0;
                    if (sh->owner == 0 && j != 0) G.hits++;
                    damage(j, TD_SHELL_DMG, sh->x, sh->z);
                    break;
                }
            }
        }
    }
}

static void mortars(void)
{
    uint8_t k, j;
    Mortar *m = G.mo;
    for (k = 0; k < TD_MAX_MORTARS; k++, m++) {
        if (!m->alive || ++m->t < m->flight) continue;
        m->alive = 0;
        event(TDE_SPLASH, 2, k, m->tx, m->tz);
        for (j = 0; j < G.ntanks; j++) {
            Tank *t = &G.tk[j];
            uint16_t d;
            if (!t->alive) continue;
            if (td_beyond(t->x - m->tx, t->z - m->tz, U(TD_SPLASH_R))) continue;
            d = td_len(t->x - m->tx, t->z - m->tz) >> 4;                 /* units */
            if (d >= TD_SPLASH_R) continue;
            if (j == 0) damage(0, (uint8_t)(TD_SPLASH_DMG - TD_SPLASH_DMG * d / TD_SPLASH_R), m->tx, m->tz);
            else if (d < TD_SPLASH_R / 2) damage(j, 1, m->tx, m->tz);
        }
    }
}

/* ---------- the tick ---------- */
void td_tick(uint8_t in)
{
    Tank *p = &G.tk[0];
    uint8_t k;
    int8_t dir;
    pos speed;
    if (G.over || G.won) return;
    G.tick++;
    if (G.between > 0) {
        if (--G.between == 0) {
            if (G.round >= TD_ROUNDS) { G.won = 1; event(TDE_WIN, 0, 0, 0, 0); return; }
            td_load_round(G.round + 1);
            event(TDE_ROUND, G.round, 0, 0, 0);
        }
        return;
    }
    /* the player */
    dir = (in & TD_IN_RIGHT ? 1 : 0) - (in & TD_IN_LEFT ? 1 : 0);
    speed = (in & TD_IN_UP) ? vmax[0] : (in & TD_IN_DOWN) ? -vmax[0] * 3 / 5 : 0;
    drive(p, p->yaw + dir * (int16_t)turn[0], speed);
    if (p->reload > 0) p->reload--;
    if (in & TD_IN_FIRE) td_fire(0);
    /* the enemies */
    for (k = 1; k < G.ntanks; k++) if (G.tk[k].alive && !G.tk[k].held) think(k);
    tanks_apart();
    for (k = 0; k < G.ntanks; k++) if (G.tk[k].alive) push_out(&G.tk[k]);
    shells();
    mortars();
    if (!G.over && G.ntanks > 1 && td_enemies_left() == 0) {
        int16_t bonus = 500 * G.round + p->hp * 5;
        G.score += bonus;
        G.between = TD_BETWEEN;
        memset(G.sh, 0, sizeof(G.sh));
        memset(G.mo, 0, sizeof(G.mo));
        event(TDE_ROUND_CLEAR, G.round, bonus, 0, 0);
    }
}
