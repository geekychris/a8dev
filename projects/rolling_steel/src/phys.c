/* phys.c - the marble in 2.5D (the Atari edition's approximation of the
   original's phys.c): the course is a grid of half-unit cells, each with
   a deck height and a surface (tools/gen_course.py). A ball on the deck is
   pulled down the slope (the grid's gradient under it, bilinear between
   the cell centres) and pushed by the stick with the original's force,
   both 5/7 of it while it rolls (a solid sphere's share) and all of it on
   ice, where it slides; sand drags. Off an edge it flies (gravity, the
   original's 9.81 + 12 m/s^2) until it lands or falls 12 units. A cell
   standing 1/4 unit above the ball's foot is a wall (rails, kerbs, posts):
   the ball bounces off it with the surface's bounciness (PhysX's: nothing
   below 2 m/s bounces). Constants are the original's.

   One call is two of the original's 1/50 s steps (as the Spectrum port's
   play mode): every per-step amount doubled, the move twice as far (in
   two halves when it's fast, so a rail isn't jumped).

   Enemies: steel chasers (mass 3) roll at you when you're within their
   range, on the same grid, and shove you; blobs swing to and fro along
   their axis and eat you.

   The ball being moved is copied into scalars (B_*) first: cc65 reaches
   them with plain loads and stores, where b_x[i] costs an index sum. */
#include <string.h>
#include "rs.h"

#pragma static-locals (on)

#define R0        128                   /* the marble's radius, Q8 */
#define EDGE      96                    /* how far ahead a wall is felt (Q8) */
#define STEPUP    64                    /* a cell this far above the foot is a wall */
#define SNAP      4                     /* following the deck down: slack (Q8) */
#define G_MARBLE  36                    /* 21.8 u/s^2, per 1/50 s (Q12 units/step) */
#define G_STEEL   16
#define ACCEL     56                    /* 34 u/s^2 */
#define ACCEL_ROLL 40                   /* 5/7 of it while rolling */
#define ACCEL_AIR 17                    /* 30% in the air */
#define MAXSPEED  1065                  /* 13 u/s, flat */
#define BOUNCE_MIN 164                  /* 2 u/s */
#define SHOVE     410                   /* a steel chaser's knock, 5 u/s */
#define FALL_MAX  3072                  /* 12 units below the last deck: gone */
#define FAST      600                   /* above this a move is done in two halves */
enum { S_DECK, S_ROUGH, S_ICE, S_RAIL };

uint16_t b_x[NB], b_z[NB];
int16_t b_y[NB];
uint8_t b_xf[NB], b_zf[NB], b_yf[NB];
int16_t b_vx[NB], b_vz[NB], b_vy[NB];
uint8_t b_gr[NB], b_cls[NB], b_live[NB];
static int16_t b_sx[NB], b_sz[NB];      /* the slope under it, as ground() left it */
static uint8_t b_g[NB], b_r[NB];        /* its gravity, its radius */
uint8_t nen, en_kind[NB];
static uint8_t en_touch[NB];
static uint16_t en_ph[NB];              /* a blob's swing */
uint16_t lg_x, lg_z;
int16_t lg_y;
uint8_t ph_event;

/* the ball being moved */
uint16_t B_x, B_z;                      /* (move.s's ball_move() moves it) */
int16_t B_y, B_vx, B_vz, B_vy, B_sx, B_sz;
uint8_t B_xf, B_zf, B_yf, B_gr, B_cls, B_r, B_g;
void ball_move(void);

static void load_ball(uint8_t i)
{
    B_x = b_x[i]; B_z = b_z[i]; B_y = b_y[i];
    B_xf = b_xf[i]; B_zf = b_zf[i]; B_yf = b_yf[i];
    B_vx = b_vx[i]; B_vz = b_vz[i]; B_vy = b_vy[i];
    B_sx = b_sx[i]; B_sz = b_sz[i];
    B_gr = b_gr[i]; B_cls = b_cls[i]; B_r = b_r[i]; B_g = b_g[i];
}

static void store_ball(uint8_t i)
{
    b_x[i] = B_x; b_z[i] = B_z; b_y[i] = B_y;
    b_xf[i] = B_xf; b_zf[i] = B_zf; b_yf[i] = B_yf;
    b_vx[i] = B_vx; b_vz[i] = B_vz; b_vy[i] = B_vy;
    b_sx[i] = B_sx; b_sz[i] = B_sz;
    b_gr[i] = B_gr; b_cls[i] = B_cls;
}

/* sin, 64 steps a quarter turn, Q7 */
static const int8_t qsin[65] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73,
    76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127
};
static int8_t isin8(uint8_t a)          /* 256 per turn */
{
    uint8_t q = a & 63;
    int8_t v;
    if (a & 64) q = 64 - q;
    v = qsin[q];
    return (a & 128) ? -v : v;
}

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

/* speed_of(vx, vz): max + min / 4 + min / 8, the octagon (within 6% of
   the length): maths.s */

/* the deck under (gx, gz): maths.s's ground_q() */
uint8_t ground(uint16_t gx, uint16_t gz)
{
    gq_x = gx;
    gq_z = gz;
    return ground_q();
}

static void cap(void)
{
    uint16_t s = speed_of(B_vx, B_vz);
    uint16_t f;
    if (s > MAXSPEED) {                  /* scale by MAXSPEED / s (Q8, 16-bit) */
        f = (uint16_t)(MAXSPEED << 4) / (s >> 4);
        B_vx = B_vx < 0 ? -(int16_t)((((uint16_t)-B_vx >> 3) * f) >> 5) : (int16_t)((((uint16_t)B_vx >> 3) * f) >> 5);
        B_vz = B_vz < 0 ? -(int16_t)((((uint16_t)-B_vz >> 3) * f) >> 5) : (int16_t)((((uint16_t)B_vz >> 3) * f) >> 5);
    }
}

static void ball_init(uint8_t i, uint16_t x, int16_t y, uint16_t z, uint8_t r, uint8_t g)
{
    b_x[i] = x; b_y[i] = y; b_z[i] = z;
    b_xf[i] = b_zf[i] = b_yf[i] = 0;
    b_vx[i] = b_vz[i] = b_vy[i] = 0;
    b_gr[i] = 0;
    b_cls[i] = S_DECK;
    b_sx[i] = b_sz[i] = 0;
    b_live[i] = 1;
    b_r[i] = r;
    b_g[i] = g;
}

void phys_place(uint16_t x, int16_t y, uint16_t z)
{
    ball_init(0, x, y, z, R0, G_MARBLE);
    lg_x = x; lg_y = y; lg_z = z;
}

void phys_enemies_home(void)
{
    uint8_t i;
    const EnemySpec *e = enemies;
    for (i = 1; i <= nen; ++i, ++e) {
        ball_init(i, e->x, e->y, e->z, e->kind ? 141 : 179, G_STEEL);     /* blob .55, steel .7 */
        en_ph[i] = 0;
        en_touch[i] = 0;
    }
}

void phys_reset(void)
{
    uint8_t i;
    nen = MH->nenemy > NB - 1 ? NB - 1 : MH->nenemy;
    for (i = 0; i < nen; ++i) en_kind[i + 1] = enemies[i].kind;
    phys_enemies_home();
}

void __fastcall__ marble_force(int8_t jx, int8_t jz);
static void marble(int8_t jx, int8_t jz)
{
    load_ball(0);
    marble_force(jx, jz);               /* force.s: the push, the slope and drag, the boxes */
    cap();
    ball_move();
    store_ball(0);
    if (B_gr) {
        lg_x = B_x;
        lg_y = B_y;
        lg_z = B_z;
    } else if (lg_y - B_y > FALL_MAX || B_y < MH->kill_y)
        ph_event |= PE_FELL;
}

static void enemy(uint8_t i)
{
    const EnemySpec *e = &enemies[i - 1];
    int16_t dx, dz, dy, mvx, mvz;
    uint16_t len;
    uint16_t mx = b_x[0], mz = b_z[0];
    int16_t my = b_y[0];
    if (en_kind[i]) {
        /* a blob: swings along its axis while you're about */
        int8_t s;
        if (b_live[0]) en_ph[i] += 2 * e->speed;
        s = isin8((uint8_t)(en_ph[i] >> 8));
        b_x[i] = e->x + (((e->ax >> 2) * s) >> 5);
        b_z[i] = e->z + (((e->az >> 2) * s) >> 5);
        dx = (int16_t)(mx - b_x[i]);
        dz = (int16_t)(mz - b_z[i]);
        dy = my - b_y[i];
        if (b_live[0] && iabs(dx) < 269 && iabs(dz) < 269 && iabs(dy) < 269 && speed_of(dx, dz) < 269)
            ph_event |= PE_EATEN;
        return;
    }
    load_ball(i);
    dx = (int16_t)(mx - B_x);
    dz = (int16_t)(mz - B_z);
    /* steel: at you if you're in its range, home otherwise; asleep when
       you're far and it's at rest */
    len = (iabs(dx) > 16000 || iabs(dz) > 16000) ? 0xFFFF : speed_of(dx, dz);
    /* asleep while you're out of its range (and 8 units more) and it's at
       rest (the original: 40 units; this saves the Atari the work) */
    if (len > e->range + 2048) {
        if (B_gr && speed_of(B_vx, B_vz) < 26) return;
        /* back home (within a unit): it stays put (PhysX's rest; the grid
           has no static friction, and a home on a slope would creep) */
        dx = (int16_t)(e->x - B_x);
        dz = (int16_t)(e->z - B_z);
        if (B_gr && iabs(dx) < 256 && iabs(dz) < 256) {
            b_vx[i] = b_vz[i] = 0;
            return;
        }
    }
    if (!b_live[0] || len > e->range) {
        dx = (int16_t)(e->x - B_x);
        dz = (int16_t)(e->z - B_z);
        len = speed_of(dx, dz);
    }
    if (len > 51) {
        while (len > 255) { dx >>= 1; dz >>= 1; len >>= 1; }   /* the direction, small */
        B_vx += (int16_t)(dx * (int16_t)(2 * e->speed)) / (int16_t)len;
        B_vz += (int16_t)(dz * (int16_t)(2 * e->speed)) / (int16_t)len;
    }
    B_vx -= B_vx >> 7;
    B_vz -= B_vz >> 7;
    cap();
    ball_move();
    if (B_y < MH->kill_y + 1536) {
        ball_init(i, e->x, e->y, e->z, 179, G_STEEL);
        return;
    }
    /* against the marble: mass 1 against 3, and the knock */
    dx = (int16_t)(mx - B_x);
    dz = (int16_t)(mz - B_z);
    dy = my - B_y;
    if (b_live[0] && iabs(dx) < 307 && iabs(dz) < 307 && iabs(dy) < 307 && (len = speed_of(dx, dz)) < 307) {
        int16_t nx, nz, vn, j, pen;
        if (len < 4) len = 4;
        nx = (int16_t)(dx << 5) / (int16_t)(len >> 2);  /* Q7 */
        nz = (int16_t)(dz << 5) / (int16_t)(len >> 2);
        pen = 307 - len;
        b_x[0] += (nx * ((pen * 3) >> 2)) >> 7;
        b_z[0] += (nz * ((pen * 3) >> 2)) >> 7;
        B_x -= (nx * (pen >> 2)) >> 7;
        B_z -= (nz * (pen >> 2)) >> 7;
        mvx = b_vx[0];
        mvz = b_vz[0];
        vn = (int16_t)(((long)(mvx - B_vx) * nx + (long)(mvz - B_vz) * nz) >> 7);
        if (vn < 0) {
            j = ((-vn - (vn < -BOUNCE_MIN ? (vn * 3) >> 4 : 0)) * 3) >> 2;
            mvx += (int16_t)(((long)nx * j) >> 7);
            mvz += (int16_t)(((long)nz * j) >> 7);
            B_vx -= (int16_t)(((long)nx * (j / 3)) >> 7);
            B_vz -= (int16_t)(((long)nz * (j / 3)) >> 7);
        }
        if (!en_touch[i]) {
            mvx += (nx * (SHOVE >> 3)) >> 4;
            mvz += (nz * (SHOVE >> 3)) >> 4;
            ph_event |= PE_CLACK;
        }
        b_vx[0] = mvx;
        b_vz[0] = mvz;
        en_touch[i] = 1;
    } else
        en_touch[i] = 0;
    store_ball(i);
}

void phys_step(int8_t jx, int8_t jz)
{
    uint8_t i;
    ph_event = 0;
    if (b_live[0]) marble(jx, jz);
    for (i = 1; i <= nen; ++i) enemy(i);
}
