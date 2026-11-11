/*
 * td_core.c - Tank Duel's rules, from the Amiga original's td_core.c:
 * driving and collisions, shells (banked once off the walls), the
 * bulwark's shield, mortar rounds and their splash, crates, the enemies'
 * minds, rounds, and the autopilot.
 *
 * port: positions in 1/16 units, speeds in 1/16 units a tick (16 bits; the
 * original: 16.16); a square root only where a push or a splash needs the
 * distance itself, squared distances elsewhere; atan2 from a 65-entry
 * table; sines from a 256-step table. For cc65's sake (and the 28K the
 * program has) the tank or shell being worked on is copied into plain
 * variables (T_..., S_...) and back, and functions take their arguments in
 * variables, not on the C stack: indexing 16-bit arrays and reading
 * parameters is several times bigger code.
 */
#include <string.h>
#include <cc65.h>
#include "td.h"

#pragma bss-name ("LOWRAM")

TDGame G;
uint8_t nobs, o_kind[TD_MAX_OBS], o_hp[TD_MAX_OBS], o_r[TD_MAX_OBS];
int16_t o_x[TD_MAX_OBS], o_z[TD_MAX_OBS];

const char *const td_tank_names[TK_COUNT] = { "PLAYER", "SCOUT", "MORTAR", "BULWARK" };

#define P TD_P
static const int16_t vmax[TK_COUNT] = { 6 * P, 7 * P, 3 * P, 4 * P };
static const int16_t turn[TK_COUNT] = { 700, 650, 300, 100 };
static const int8_t hits[TK_COUNT] = { TD_HP, 1, 2, 2 };
static const uint8_t reload_t[TK_COUNT] = { TD_RELOAD, 60, 110, 55 };
#define SHIELD_ARC 6370                 /* the bulwark's shield: 35 degrees each side of ahead */

static const uint8_t rounds[TD_ROUNDS][3] = {
    { 2, 0, 0 }, { 2, 0, 1 }, { 1, 1, 1 }, { 2, 0, 2 }, { 2, 2, 0 }, { 1, 1, 2 }, { 2, 2, 2 }, { 1, 2, 3 }
};

/* ---------------------------------------------------------------- maths */
/* sin >> 6 (Q8) by the high byte of the angle: the rules' only use of it */
const int16_t sin8[256] = {
    0, 6, 12, 18, 25, 31, 37, 43, 49, 56, 62, 68, 74, 80, 86, 92,
    97, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 209, 212, 216, 219, 222, 225, 228, 231, 234,
    236, 238, 241, 243, 244, 246, 248, 249, 251, 252, 253, 254, 254, 255, 255, 255,
    256, 255, 255, 255, 254, 254, 253, 252, 251, 249, 248, 246, 244, 243, 241, 238,
    236, 234, 231, 228, 225, 222, 219, 216, 212, 209, 205, 201, 197, 193, 189, 185,
    181, 176, 171, 167, 162, 157, 152, 147, 142, 136, 131, 126, 120, 115, 109, 103,
    97, 92, 86, 80, 74, 68, 62, 56, 49, 43, 37, 31, 25, 18, 12, 6,
    0, -6, -12, -18, -25, -31, -37, -43, -49, -56, -62, -68, -74, -80, -86, -92,
    -97, -103, -109, -115, -120, -126, -131, -136, -142, -147, -152, -157, -162, -167, -171, -176,
    -181, -185, -189, -193, -197, -201, -205, -209, -212, -216, -219, -222, -225, -228, -231, -234,
    -236, -238, -241, -243, -244, -246, -248, -249, -251, -252, -253, -254, -254, -255, -255, -255,
    -256, -255, -255, -255, -254, -254, -253, -252, -251, -249, -248, -246, -244, -243, -241, -238,
    -236, -234, -231, -228, -225, -222, -219, -216, -212, -209, -205, -201, -197, -193, -189, -185,
    -181, -176, -171, -167, -162, -157, -152, -147, -142, -136, -131, -126, -120, -115, -109, -103,
    -97, -92, -86, -80, -74, -68, -62, -56, -49, -43, -37, -31, -25, -18, -12, -6
};
#define S8(a) sin8[(uint8_t)((uint16_t)(a) >> 8)]
#define C8(a) sin8[(uint8_t)(((uint16_t)(a) >> 8) + 64)]

/* the angle to a point, the minds' distances and steps along a heading:
   in asm (mathb.s; tools/host/mathb.c for the host check) */
extern int16_t M_a, M_b;
extern uint8_t M_d;
extern uint16_t angle_to_(void), odist_(void);
extern int16_t along_(void), along8_(void);
#undef td_angle_to
#define td_angle_to(dx, dz) (M_a = (dx), M_b = (dz), angle_to_())

/* port: square roots of 16 bits (the original's distances in 8ths, below) */
static uint8_t isqrt16(uint16_t v)
{
    uint16_t r = 0, bit = 1u << 14;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint8_t)r;
}

static int16_t iabs(int16_t a) { return a < 0 ? -a : a; }
#define ADIFF(a, b) ((int16_t)((a) - (b)))

/* port: the minds' ranges by an octagonal distance (max + 3/8 min, within
   7%) in whole units, not a squared one */
#define USQ(dx, dz) (M_a = (dx), M_b = (dz), odist_())
#define R2(u) ((uint16_t)(u))

/* d units out along heading a, in positions */
#define along(d, a) (M_d = (d), M_a = (a), along_())
#define ALONGZ(d, a) (M_d = (d), M_a = (a) + 0x4000, along_())

/* port: a 16-bit xorshift (the original: mulberry32 in 32 bits) */
static uint16_t rnd(void)
{
    uint16_t x = G.rng;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    return G.rng = x;
}

/* events (a ring of TD_MAX_EVENTS): the place goes in EX, EZ first */
static int16_t EX, EZ;
static void event(uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t n = G.nev;
    if (((n - G.evr) & (TD_MAX_EVENTS - 1)) == TD_MAX_EVENTS - 1) return;   /* full */
    G.ev[n] = type; G.eva[n] = a; G.evb[n] = b; G.evx[n] = EX; G.evz[n] = EZ;
    G.nev = (n + 1) & (TD_MAX_EVENTS - 1);
}
#define AT(x, z) (EX = (x), EZ = (z))

uint8_t td_pop_event(uint8_t *a, uint8_t *b, int16_t *x, int16_t *z)
{
    uint8_t r = G.evr;
    if (r == G.nev) return TDE_NONE;
    *a = G.eva[r]; *b = G.evb[r]; *x = G.evx[r]; *z = G.evz[r];
    G.evr = (r + 1) & (TD_MAX_EVENTS - 1);
    return G.ev[r];
}

uint8_t td_enemies_left(void)
{
    uint8_t k, n = 0;
    for (k = 1; k < G.ntanks; k++) n += G.t_alive[k];
    return n;
}

/* port: dx, dz inside the circle of radius r (|dx|, |dz| < r <= 2040
   positions) in half units: 16-bit squares, not 32 */
static uint8_t incircle(int16_t dx, int16_t dz, uint16_t r)
{
    uint8_t a = (uint8_t)((uint16_t)iabs(dx) >> 3), b = (uint8_t)((uint16_t)iabs(dz) >> 3), c = (uint8_t)(r >> 3);
    return (uint16_t)(a * a) + (uint16_t)(b * b) < (uint16_t)(c * c);
}

/* ---------------------------------------------------------------- the current tank */
static uint8_t tk;                      /* its index */
static int16_t T_x, T_z, T_v;
static uint16_t T_yaw;
static void tget(void) { T_x = G.t_x[tk]; T_z = G.t_z[tk]; T_v = G.t_v[tk]; T_yaw = G.t_yaw[tk]; }
static void tput(void) { G.t_x[tk] = T_x; G.t_z[tk] = T_z; G.t_v[tk] = T_v; G.t_yaw[tk] = T_yaw; }

/* ---------------------------------------------------------------- obstacles */
/* Q_x, Q_z (positions) with radius Q_r (units): the next obstacle from Q_k
   within its square of reach (o_r + Q_r): returns its index, with Q_dx,
   Q_dz from it and Q_reach; 0xFF when there are no more */
int16_t Q_x, Q_z, Q_dx, Q_dz, Q_reach;
uint8_t Q_r, Q_k;
extern uint8_t near_obs(void);          /* (obs.s) */
/* ... and inside it: a crate's square, or the circle of the others */
static uint8_t inside(uint8_t k)
{
    return o_kind[k] == OB_CRATE || incircle(Q_dx, Q_dz, Q_reach);
}

/* Q_x, Q_z with radius Q_r inside an obstacle or a wall? */
static uint8_t blocked(void)
{
    int16_t lim = (TD_ARENA - Q_r) * P;
    uint8_t k;
    if (Q_x > lim || Q_x < -lim || Q_z > lim || Q_z < -lim) return 1;
    for (Q_k = 0; (k = near_obs()) != 0xFF;)
        if (inside(k)) return 1;
    return 0;
}

/* the current tank out of the obstacles and inside the walls */
static void push_out(void)
{
    int16_t lim = (TD_ARENA - TD_TANK_R) * P, px, pz, ax, az;
    uint8_t k, r8, d8;
    Q_r = TD_TANK_R;
    for (Q_k = 0;;) {
        Q_x = T_x; Q_z = T_z;
        if ((k = near_obs()) == 0xFF) break;
        if (o_kind[k] == OB_CRATE) {
            px = Q_reach - iabs(Q_dx);
            pz = Q_reach - iabs(Q_dz);
            if (px < pz) T_x += Q_dx < 0 ? -px : px;
            else T_z += Q_dz < 0 ? -pz : pz;
        } else {
            /* out to the circle (port: in 8ths of a unit, 16 bits) */
            ax = Q_dx >> 3; az = Q_dz >> 3; r8 = (uint8_t)(Q_reach >> 3);
            d8 = isqrt16((uint16_t)(ax * ax) + (uint16_t)(az * az));
            if (d8 >= r8) continue;
            if (d8 < 2) { ax = 2; d8 = 2; }
            T_x = o_x[k] + (ax * r8 / d8) * 8;
            T_z = o_z[k] + (az * r8 / d8) * 8;
        }
    }
    if (T_x > lim) T_x = lim;
    if (T_x < -lim) T_x = -lim;
    if (T_z > lim) T_z = lim;
    if (T_z < -lim) T_z = -lim;
}

/* the current tank: turn towards heading D_want and drive at D_speed */
#pragma bss-name (push, "LOWRAM")
static uint8_t ph120[TD_MAX_TANKS], moved[TD_MAX_TANKS];
static uint16_t m_want[TD_MAX_TANKS];
static int16_t m_speed[TD_MAX_TANKS];
#pragma bss-name (pop)
static uint16_t D_want;
static int16_t D_speed;
static void drive(void)
{
    int16_t d = ADIFF(D_want, T_yaw), r = turn[G.t_type[tk]];
    if (d > r) d = r;
    if (d < -r) d = -r;
    T_yaw += d;
    if (T_v < D_speed) { T_v += P / 2; if (T_v > D_speed) T_v = D_speed; }
    else if (T_v > D_speed) { T_v -= 10; if (T_v < D_speed) T_v = D_speed; }   /* 0.6 */
    /* (T_v * sin) >> 8: |T_v| along the heading, turned round when backing */
    M_d = (uint8_t)(T_v < 0 ? -T_v : T_v);
    M_a = T_v < 0 ? T_yaw + 0x8000 : T_yaw;
    T_x += along8_();
    M_a += 0x4000;
    T_z += along8_();
    push_out();
}

static void add_tank(uint8_t type)
{
    tk = G.ntanks;
    if (tk >= TD_MAX_TANKS) return;
    G.t_type[tk] = type;
    G.t_alive[tk] = 1;
    G.t_held[tk] = 0;
    G.t_hp[tk] = hits[type];
    T_v = 0;
    tput();
    G.t_ai[tk] = 0;
    ph120[tk] = 0;
    m_want[tk] = T_yaw; m_speed[tk] = 0;
    G.t_reload[tk] = reload_t[type] / 2 + (uint8_t)(rnd() % 30);
    G.t_orbit[tk] = (rnd() & 1) ? 1 : -1;
    ++G.ntanks;
}

/* the grid near_obs looks in: each obstacle in the cells (256 units, 8 x 8)
   within its reach + GRID_R */
#define GRID_R 40
extern uint8_t grid_lo[64], grid_hi[64];
static uint8_t gcell(int16_t u)                        /* (units) */
{
    int16_t c = (u + 1024) >> 8;
    return c < 0 ? 0 : c > 7 ? 7 : (uint8_t)c;
}
static void grid_build(void)
{
    uint8_t k, x, z, x1, z1, r;
    uint16_t bit;
    memset(grid_lo, 0, 64);
    memset(grid_hi, 0, 64);
    for (k = 0; k < nobs; k++) {
        r = o_r[k] + GRID_R;
        bit = 1u << k;
        x1 = gcell((o_x[k] >> 4) + r);
        z1 = gcell((o_z[k] >> 4) + r);
        for (z = gcell((o_z[k] >> 4) - r); z <= z1; z++)
            for (x = gcell((o_x[k] >> 4) - r); x <= x1; x++) {
                grid_lo[z * 8 + x] |= (uint8_t)bit;
                grid_hi[z * 8 + x] |= (uint8_t)(bit >> 8);
            }
    }
}

static void place_obstacles(void)
{
    static const uint8_t kinds[13] = { OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PYRAMID, OB_PYRAMID, OB_PYRAMID,
                                       OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE };
    uint8_t k, tries, n, ok;
    nobs = 0;
    for (k = 0; k < 13; k++) {
        n = nobs;
        o_kind[n] = kinds[k];
        Q_r = o_r[n] = kinds[k] == OB_PILLAR ? 26 : kinds[k] == OB_PYRAMID ? 50 : 34;
        o_hp[n] = kinds[k] == OB_CRATE ? 2 : 1;
        Q_r += 90;                                      /* 90 apart */
        for (tries = 0; tries < 40; tries++) {
            Q_x = ((int16_t)(rnd() % 1500) - 750) * P;
            Q_z = ((int16_t)(rnd() % 1300) - 650) * P;
            ok = !(Q_x < 220 * P && Q_x > -220 * P && Q_z < -450 * P);   /* clear of the player's start */
            for (Q_k = 0; ok && near_obs() != 0xFF;)
                if (incircle(Q_dx >> 1, Q_dz >> 1, Q_reach >> 1)) ok = 0;   /* (round, as the original) */
            if (ok) { o_x[n] = Q_x; o_z[n] = Q_z; nobs++; break; }
        }
    }
    grid_build();
}

void td_load_round(uint8_t round)
{
    uint8_t k, n = 0, t, j;
    int8_t hp = G.t_hp[0];
    G.round = round;
    memset(G.s_alive, 0, sizeof(G.s_alive));
    memset(G.m_alive, 0, sizeof(G.m_alive));
    place_obstacles();
    G.ntanks = 0;
    T_x = 0; T_z = -800 * P; T_yaw = 0;
    add_tank(TK_PLAYER);
    if (round > 1) G.t_hp[0] = hp + 30 > TD_HP ? TD_HP : hp + 30;
    G.t_reload[0] = 0;
    for (t = 0; t < 3; t++)
        for (k = 0; k < rounds[round - 1][t]; k++, n++) {
            Q_x = (-600 + (int16_t)(((uint16_t)n * 397 + (uint16_t)(rnd() % 160)) % 1200)) * P;
            Q_z = (450 + (int16_t)(rnd() % 350)) * P;
            Q_r = TD_TANK_R + 10;
            for (j = 0; j < 20 && blocked(); j++) Q_x += 60 * P;
            T_x = Q_x; T_z = Q_z; T_yaw = 0x8000;
            add_tank(TK_SCOUT + t);
        }
}

void td_new_game(uint16_t seed, uint8_t round)
{
    memset(&G, 0, sizeof(G));
    nobs = 0;
    G.seed = seed;
    G.rng = seed | 1;
    if (round < 1) round = 1;
    if (round > TD_ROUNDS) round = TD_ROUNDS;
    td_load_round(round);
    G.t_hp[0] = TD_HP;
}

/* port: tanks pushed apart along the axis they overlap least on (the
   original: along the line between them, by a square root) */
static void tanks_apart(void)
{
    uint8_t i, j;
    int16_t dx, dz, px, pz, two = 2 * TD_TANK_R * P;
    memset(moved, 0, sizeof(moved));
    for (i = 0; i < G.ntanks; i++) {
        if (!G.t_alive[i]) continue;
        for (j = i + 1; j < G.ntanks; j++) {
            if (!G.t_alive[j]) continue;
            dx = G.t_x[j] - G.t_x[i];
            if (dx >= two || dx <= -two) continue;
            dz = G.t_z[j] - G.t_z[i];
            if (dz >= two || dz <= -two || !incircle(dx, dz, two)) continue;
            px = (two - iabs(dx)) >> 1;
            pz = (two - iabs(dz)) >> 1;
            if (px < pz) { if (dx < 0) px = -px; G.t_x[i] -= px; G.t_x[j] += px; }
            else { if (dz < 0) pz = -pz; G.t_z[i] -= pz; G.t_z[j] += pz; }
            moved[i] = moved[j] = 1;
        }
    }
}

/* the current tank fires (if it can) */
static void fire(void)
{
    uint8_t k;
    if (G.t_reload[tk] || !G.t_alive[tk]) return;
    for (k = 0; k < TD_MAX_SHELLS; k++) if (!G.s_alive[k]) break;
    if (k == TD_MAX_SHELLS) return;
    G.s_alive[k] = 1;
    G.s_owner[k] = tk;
    G.s_bounces[k] = 1;
    G.s_life[k] = TD_SHELL_LIFE;
    G.s_x[k] = T_x + along(34, T_yaw);                 /* 34 units out */
    G.s_z[k] = T_z + ALONGZ(34, T_yaw);
    G.s_dx[k] = S8(T_yaw) * 2;               /* 32 units a tick */
    G.s_dz[k] = C8(T_yaw) * 2;
    G.t_reload[tk] = reload_t[G.t_type[tk]];
    if (!tk) G.shots++;
    AT(T_x, T_z);
    event(tk ? TDE_EFIRE : TDE_FIRE, tk, 0);
}

/* the current tank lobs a mortar round at where the player will be */
static void lob(void)
{
    uint8_t k;
    int16_t v = G.t_v[0];
    uint16_t a = G.t_yaw[0];
    for (k = 0; k < TD_MAX_MORTARS; k++) if (!G.m_alive[k]) break;
    if (k == TD_MAX_MORTARS) return;
    G.m_alive[k] = 1;
    G.m_owner[k] = tk;
    G.m_t[k] = 0;
    G.m_x0[k] = T_x;
    G.m_z0[k] = T_z;
    /* where the player will be in half the flight */
    G.m_tx[k] = G.t_x[0] + ((v * S8(a)) >> 8) * (TD_MORTAR_FLIGHT / 2);
    G.m_tz[k] = G.t_z[0] + ((v * C8(a)) >> 8) * (TD_MORTAR_FLIGHT / 2);
    G.t_reload[tk] = reload_t[G.t_type[tk]];
    AT(G.m_tx[k], G.m_tz[k]);
    event(TDE_LOB, tk, k);
}

/* tank j takes `amount` (at EX, EZ) */
static void damage(uint8_t j, uint8_t amount)
{
    uint8_t ty;
    if (!G.t_alive[j]) return;
    if (!j) {
        G.t_hp[0] -= amount;
        G.damage_taken += amount;
        if (G.t_hp[0] < 0) G.t_hp[0] = 0;
        event(TDE_HURT, amount, G.t_hp[0]);
        if (!G.t_hp[0]) { G.t_alive[0] = 0; G.over = 1; AT(G.t_x[0], G.t_z[0]); event(TDE_DEAD, 0, 0); }
        return;
    }
    if (--G.t_hp[j] > 0) { event(TDE_HIT, j, G.t_hp[j]); return; }
    G.t_alive[j] = 0;
    G.kills++;
    ty = G.t_type[j];
    G.score += ty == TK_SCOUT ? 100 : ty == TK_MORTAR ? 200 : 300;
    AT(G.t_x[j], G.t_z[j]);
    event(TDE_KILL, j, ty);
}

/* ---------------------------------------------------------------- shells */
static int16_t S_x, S_z, S_dx, S_dz;
static uint8_t S_bounces, S_owner, S_alive;

/* the current shell against the tanks: its own only after a bounce */
static void shell_tanks(void)
{
    uint8_t j;
    int16_t dx, dz, r = TD_TANK_R * P;
    uint16_t a, ty;
    for (j = 0; j < G.ntanks && S_alive; j++) {
        if (!G.t_alive[j] || (j == S_owner && S_bounces)) continue;
        if (S_owner && j) continue;                     /* enemies' shells pass their own side */
        dx = S_x - G.t_x[j];
        if (dx >= r || dx <= -r) continue;
        dz = S_z - G.t_z[j];
        if (dz >= r || dz <= -r || !incircle(dx, dz, r)) continue;
        AT(S_x, S_z);
        ty = G.t_yaw[j];
        if (G.t_type[j] == TK_BULWARK && iabs(ADIFF(td_angle_to(dx, dz), ty)) < SHIELD_ARC) {
            /* off the shield */
            event(TDE_GLANCE, j, 0);
            if (S_bounces) {
                a = 2 * ty - td_angle_to(-S_dx, -S_dz);
                S_bounces--;
                S_dx = S8(a) * 2;
                S_dz = C8(a) * 2;
                S_x = G.t_x[j] + along(32, a);
                S_z = G.t_z[j] + ALONGZ(32, a);
                S_owner = j;
            } else S_alive = 0;
            return;
        }
        S_alive = 0;
        if (!S_owner && j) G.hits++;
        damage(j, TD_SHELL_DMG);
    }
}

static void shells(void)
{
    uint8_t k, s, j;
    int16_t lim = TD_ARENA * P;
    for (k = 0; k < TD_MAX_SHELLS; k++) {
        if (!G.s_alive[k]) continue;
        if (--G.s_life[k] == 0) { G.s_alive[k] = 0; continue; }
        S_x = G.s_x[k]; S_z = G.s_z[k]; S_dx = G.s_dx[k]; S_dz = G.s_dz[k];
        S_bounces = G.s_bounces[k]; S_owner = G.s_owner[k]; S_alive = 1;
        for (s = 0; s < 2 && S_alive; s++) {         /* port: two steps of 16 a tick (the original: four of 8) */
            S_x += S_dx >> 1;                       /* (S_dx, S_dz are even) */
            S_z += S_dz >> 1;
            /* the walls: one bounce, then it's spent */
            if (S_x > lim || S_x < -lim || S_z > lim || S_z < -lim) {
                AT(S_x, S_z);
                if (!S_bounces) { S_alive = 0; event(TDE_SPLASH, 0, 0); break; }
                S_bounces--;
                if (S_x > lim) { S_dx = -S_dx; S_x = lim - (S_x - lim); }
                else if (S_x < -lim) { S_dx = -S_dx; S_x = -lim - (S_x + lim); }
                if (S_z > lim) { S_dz = -S_dz; S_z = lim - (S_z - lim); }
                else if (S_z < -lim) { S_dz = -S_dz; S_z = -lim - (S_z + lim); }
                event(TDE_BOUNCE, k, 0);
            }
            /* obstacles */
            Q_x = S_x; Q_z = S_z; Q_r = 0;
            for (Q_k = 0; S_alive && (j = near_obs()) != 0xFF;) {
                if (!inside(j)) continue;
                S_alive = 0;
                if (o_kind[j] == OB_CRATE) {
                    AT(o_x[j], o_z[j]);
                    if (--o_hp[j] == 0) { if (!S_owner) G.score += 20; event(TDE_CRATE_BREAK, j, 0); }
                    else event(TDE_CRATE_HIT, j, o_hp[j]);
                } else { AT(S_x, S_z); event(TDE_SPLASH, 1, 0); }
            }
            shell_tanks();
        }
        G.s_x[k] = S_x; G.s_z[k] = S_z; G.s_dx[k] = S_dx; G.s_dz[k] = S_dz;
        G.s_bounces[k] = S_bounces; G.s_owner[k] = S_owner; G.s_alive[k] = S_alive;
    }
}

static void mortars(void)
{
    uint8_t k, j;
    uint16_t d;
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        if (!G.m_alive[k] || ++G.m_t[k] < TD_MORTAR_FLIGHT) continue;
        G.m_alive[k] = 0;
        AT(G.m_tx[k], G.m_tz[k]);
        event(TDE_SPLASH, 2, k);
        for (j = 0; j < G.ntanks; j++) {
            if (!G.t_alive[j]) continue;
            d = USQ(G.t_x[j] - G.m_tx[k], G.t_z[j] - G.m_tz[k]);   /* units */
            if (d >= TD_SPLASH_R) continue;
            if (!j) damage(0, TD_SPLASH_DMG - (uint8_t)(d * TD_SPLASH_DMG / TD_SPLASH_R));
            else if (d < TD_SPLASH_R / 2) damage(j, 1);
        }
    }
}

/* ---------------------------------------------------------------- the enemies */
#define ORB(a) (orbit > 0 ? (uint16_t)(a) : (uint16_t)-(a))
/* the current tank (an enemy) */
static void think(void)
{
    int16_t dx, dz;
    uint16_t d2, to;
    uint8_t ph;
    int8_t orbit = G.t_orbit[tk];
    uint8_t type = G.t_type[tk];
    if (G.t_reload[tk]) G.t_reload[tk]--;
    /* (the original's ai % 120 and ai % 400, as counters) */
    if (++G.t_ai[tk] == 400) G.t_ai[tk] = 0;
    if (++ph120[tk] == 120) ph120[tk] = 0;
    ph = ph120[tk];
    if (type == TK_SCOUT && G.t_ai[tk] == 399) G.t_orbit[tk] = -orbit;
    /* port: each mind thinks every other tick (half the tanks a tick) and
       drives on what it chose in between */
    if ((G.tick ^ tk) & 1) { D_want = m_want[tk]; D_speed = m_speed[tk]; drive(); return; }
    dx = G.t_x[0] - T_x; dz = G.t_z[0] - T_z;
    d2 = USQ(dx, dz);
    to = td_angle_to(dx, dz);
    D_want = to;
    D_speed = vmax[type];
    if (type == TK_SCOUT) {
        if (ph > 70) {                            /* turn in to snipe */
            D_speed = P;
            if (iabs(ADIFF(to, T_yaw)) < 700 && d2 < R2(900)) fire();
        } else if (d2 > R2(420)) D_want = to + ORB(0x1000);
        else if (d2 < R2(260)) D_want = to + ORB(0x6000);
        else D_want = to + ORB(0x4000);
    } else if (type == TK_MORTAR) {
        if (d2 < R2(450)) D_want = to + 0x8000;
        else if (d2 <= R2(750)) D_speed = 0;
        if (!G.t_reload[tk] && d2 < R2(1100)) lob();
    } else {                                            /* the bulwark */
        if (d2 < R2(240)) D_speed = 0;
        if (iabs(ADIFF(to, T_yaw)) < 600 && d2 < R2(900)) fire();
    }
    /* something in the way ahead: steer round it */
    if (D_speed > 0) {
        Q_x = T_x + along(70, T_yaw); Q_z = T_z + ALONGZ(70, T_yaw); Q_r = TD_TANK_R;
        if (blocked()) D_want = T_yaw + ORB(0x2000);
    }
    m_want[tk] = D_want; m_speed[tk] = D_speed;
    drive();
}

/* ---------------------------------------------------------------- the tick */
void td_tick(uint8_t in)
{
    uint16_t bonus;
    if (G.over || G.won) return;
    G.tick++;
    if (G.between) {
        if (--G.between == 0) {
            if (G.round >= TD_ROUNDS) { G.won = 1; event(TDE_WIN, 0, 0); return; }
            td_load_round(G.round + 1);
            event(TDE_ROUND, G.round, 0);
        }
        return;
    }
    /* the player */
    tk = 0;
    tget();
    D_want = T_yaw + (in & TD_IN_RIGHT ? turn[0] : 0) - (in & TD_IN_LEFT ? turn[0] : 0);
    D_speed = (in & TD_IN_UP) ? 6 * P : (in & TD_IN_DOWN) ? -(6 * P * 3 / 5) : 0;   /* (vmax[0]) */
    drive();
    if (G.t_reload[0]) G.t_reload[0]--;
    if (in & TD_IN_FIRE) fire();
    tput();
    /* the enemies */
    for (tk = 1; tk < G.ntanks; tk++)
        if (G.t_alive[tk] && !G.t_held[tk]) { tget(); think(); tput(); }
    tanks_apart();
    for (tk = 0; tk < G.ntanks; tk++)
        if (G.t_alive[tk] && moved[tk]) { tget(); push_out(); tput(); }   /* (drive() did the others) */
    shells();
    mortars();
    if (!G.over && G.ntanks > 1 && !td_enemies_left()) {
        bonus = 500 * G.round + G.t_hp[0] * 5;
        G.score += bonus;
        G.between = TD_BETWEEN;
        memset(G.s_alive, 0, sizeof(G.s_alive));
        memset(G.m_alive, 0, sizeof(G.m_alive));
        event(TDE_ROUND_CLEAR, G.round, 0);
    }
}

/* ---------------------------------------------------------------- the autopilot */
uint8_t td_bot(void)
{
    uint8_t k, e = 0xFF, in = 0;
    uint16_t best = 0, dd;
    int16_t dx, dz, lean, diff, px = G.t_x[0], pz = G.t_z[0], rel;
    uint16_t want, to, yaw = G.t_yaw[0];
    if (G.over || G.won || G.between || !G.t_alive[0]) return 0;
    for (k = 1; k < G.ntanks; k++) {
        if (!G.t_alive[k]) continue;
        dd = USQ(G.t_x[k] - px, G.t_z[k] - pz);
        if (e == 0xFF || dd < best) { e = k; best = dd; }
    }
    if (e == 0xFF) return 0;
    dx = G.t_x[e] - px;
    dz = G.t_z[e] - pz;
    want = to = td_angle_to(dx, dz);
    /* a bulwark: circle it at 320, the way it is already turning away
       from, until well off its shield; then stop, aim and fire */
    if (G.t_type[e] == TK_BULWARK) {
        rel = ADIFF(td_angle_to(-dx, -dz), G.t_yaw[e]);
        if (iabs(rel) < SHIELD_ARC + 2600) {
            lean = ((int16_t)best - 320) * 40;
            if (lean > 0x2000) lean = 0x2000;
            if (lean < -0x2000) lean = -0x2000;
            want = rel >= 0 ? to - (0x4000 - lean) : to + (0x4000 - lean);
            in = TD_IN_UP;
        }
    } else if (best > R2(420)) in = TD_IN_UP;
    else if (best < R2(230)) in = TD_IN_DOWN;
    /* out of a mortar's ring */
    for (k = 0; k < TD_MAX_MORTARS; k++)
        if (G.m_alive[k] && USQ(px - G.m_tx[k], pz - G.m_tz[k]) < R2(TD_SPLASH_R + 40)) {
            want = td_angle_to(px - G.m_tx[k], pz - G.m_tz[k]);
            in = TD_IN_UP;
        }
    /* something in the way */
    if (in & TD_IN_UP) {
        Q_x = px + along(60, yaw); Q_z = pz + ALONGZ(60, yaw); Q_r = TD_TANK_R;
        if (blocked()) want = yaw + 0x3000;
    }
    diff = ADIFF(want, yaw);
    if (diff > 250) in |= TD_IN_RIGHT;
    if (diff < -250) in |= TD_IN_LEFT;
    /* shoot when lined up on an enemy and not at its shield */
    if (!G.t_reload[0])
        for (k = 1; k < G.ntanks; k++) {
            if (!G.t_alive[k]) continue;
            dx = G.t_x[k] - px;
            dz = G.t_z[k] - pz;
            if (iabs(ADIFF(td_angle_to(dx, dz), yaw)) > 450 || USQ(dx, dz) > R2(1000)) continue;
            if (G.t_type[k] == TK_BULWARK && iabs(ADIFF(td_angle_to(-dx, -dz), G.t_yaw[k])) < SHIELD_ARC) continue;
            in |= TD_IN_FIRE;
        }
    return in;
}
