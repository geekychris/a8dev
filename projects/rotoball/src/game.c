/*
 * Rotoball's rules, physics and CPU player: the original's main.c
 * (kickoff, check_goal, update_skimmer, resolve_collision, update_ball and
 * the CPU's choice in the main loop), in its order.
 *
 * port: 8.8 fixed point (game.h). The friction's truncation (v * 15 / 16)
 * drops the original's lower 8 bits; the rest is the same arithmetic. The
 * match clock counts steps (50 a second; the original steps once a 1/50 s
 * frame too). A two-player mode drives P2 from the second joystick.
 */
#include "game.h"

Game G;
int16_t sk_x[2], sk_z[2], sk_vx[2], sk_vz[2];
uint8_t sk_ball[2];
int16_t ball_x, ball_z, ball_vx, ball_vz;
int8_t ball_carrier;
static uint16_t sq[182];                    /* the squares (isqrt16) */

/* the facing: P1 +X (cos 1), P2 -X (cos -1); sin is 0 for both */
#define FACE(i) ((i) ? -1 : 1)

static void kickoff(void)
{
    sk_x[0] = -(PITCH_LENGTH / 4 * 3); sk_z[0] = 0;
    sk_x[1] = PITCH_LENGTH / 4 * 3;  sk_z[1] = 0;
    sk_vx[0] = sk_vz[0] = sk_vx[1] = sk_vz[1] = 0;
    sk_ball[0] = sk_ball[1] = 0;
    ball_x = ball_z = ball_vx = ball_vz = 0;
    ball_carrier = -1;
}

void game_init(void)
{
    uint8_t i;
    for (i = 0; i < 182; ++i) sq[i] = (uint16_t)i * i;
    G.state = MS_TITLE;
    G.players = 1;
    G.score[0] = G.score[1] = 0;
    G.steps_left = MATCH_STEPS;
    G.secs = 90;
    G.sub = CLOCK_HZ;
    kickoff();
}

void game_start(uint8_t players)
{
    G.players = players;
    G.score[0] = G.score[1] = 0;
    G.steps_left = MATCH_STEPS;
    G.secs = 90;
    G.sub = CLOCK_HZ;
    G.tackle_cooldown = 0;
    G.tackle_lock = 0;
    G.state = MS_PLAYING;
    kickoff();
}

/* v * 15 / 16, truncated towards zero (the original's friction; the
   ball's 30 / 32 is the same) */
static int16_t fric(int16_t v)
{
    static uint16_t u;
    if (v >= 0) {
        u = (uint16_t)v;
        return (int16_t)(((u << 4) - u) >> 4);
    }
    u = (uint16_t)-v;
    return -(int16_t)(((u << 4) - u) >> 4);
}

/* the ball over a goal line: points by how far out the carrier was */
static void check_goal(void)
{
    int16_t dist;
    uint8_t pts;
    if (ball_x >= PITCH_LENGTH) {
        pts = 1;
        if (ball_carrier == 0) {
            dist = PITCH_LENGTH - sk_x[0];
            pts = (dist > PITCH_LENGTH / 4 * 3) ? 3 : (dist > PITCH_LENGTH / 2) ? 2 : 1;
        }
        G.score[0] += pts;
        G.last_pts = pts;
        G.events |= EV_GOAL1;
        kickoff();
    } else if (ball_x <= -PITCH_LENGTH) {
        pts = 1;
        if (ball_carrier == 1) {
            dist = sk_x[1] + PITCH_LENGTH;
            pts = (dist > PITCH_LENGTH / 4 * 3) ? 3 : (dist > PITCH_LENGTH / 2) ? 2 : 1;
        }
        G.score[1] += pts;
        G.last_pts = pts;
        G.events |= EV_GOAL2;
        kickoff();
    }
}

/* the original's update_skimmer is rules.s's (the accelerations its
   (ACCEL >> 8) * (cos >> 8): 12 and -13 forward and back, 10 and -11
   sideways); the speed cap, which it rarely needs, is here: |v| > 2 units
   (the original's mag2 test) */
extern uint8_t us_c;
extern int16_t us_vx, us_vz;
void __fastcall__ update_skimmer(uint8_t i);

void cap_check(void)
{
    if ((int32_t)us_vx * us_vx + (int32_t)us_vz * us_vz > 512L * 512) {
        us_vx = (int16_t)(us_vx * 3 / 4);
        us_vz = (int16_t)(us_vz * 3 / 4);
    }
}

#define PICKUP_DIST  (3 * ONE)
#define ROTO_MIN     (4 * ONE)              /* 2 x the skimmer's radius */
#define TACKLE_COOLDOWN_F 60

/* floor(sqrt(v)) for v < 32768: a binary search of the squares */

static uint8_t isqrt16(uint16_t v)
{
    static uint8_t r, b;
    r = 0;
    for (b = 128; b; b >>= 1)
        if ((uint8_t)(r | b) < 182 && sq[r | b] <= v) r |= b;
    return r;
}

/* skimmer against skimmer: a tackle takes the ball, and they bump apart
   (the original's resolve_collision). port: in 16 bits: the distance from
   the separation / 8 (the original counts up to its square root), the
   normal n (|n| = 256) by two 16-bit divisions, the products scaled to
   fit 16 bits (a few low bits of the push and the bounce lost) */
static void resolve_collision(void)
{
    static int16_t dx, dz, dist, nx, nz, overlap, px, pz, rel, imp;
    static uint8_t ax, az;
    dx = sk_x[1] - sk_x[0];                 /* (mod 2^16: tested in range first) */
    dz = sk_z[1] - sk_z[0];
    if ((uint16_t)(dx + ROTO_MIN) >= 2 * ROTO_MIN || (uint16_t)(dz + ROTO_MIN) >= 2 * ROTO_MIN) {
        G.tackle_lock = 0;                  /* apart: tackles re-armed */
        return;
    }
    ax = (uint8_t)((uint16_t)(dx < 0 ? -dx : dx) >> 3);   /* < 128 */
    az = (uint8_t)((uint16_t)(dz < 0 ? -dz : dz) >> 3);
    dist = (int16_t)isqrt16((uint16_t)ax * ax + (uint16_t)az * az) << 3;
    if (dist >= ROTO_MIN) { G.tackle_lock = 0; return; }

    if (G.tackle_cooldown == 0 && !G.tackle_lock && ball_carrier >= 0) {
        uint8_t nc = ball_carrier == 0 ? 1 : 0, oc = (uint8_t)(1 - nc);
        int16_t k = FACE(nc) * ONE;
        sk_ball[oc] = 0;
        sk_ball[nc] = 1;
        ball_carrier = (int8_t)nc;
        G.tackle_cooldown = TACKLE_COOLDOWN_F;
        G.tackle_lock = 1;
        G.events |= EV_TACKLE;
        sk_vx[nc] += k;                     /* the new carrier kicked forward */
        sk_vx[oc] -= k / 2;                 /* the old one back */
    }

    if (dist < 8) dist = 8;
    /* n = d * 256 / dist: |d| * 64 / (dist / 4) */
    nx = (int16_t)(((uint16_t)(dx < 0 ? -dx : dx) << 6) / (uint16_t)(dist >> 2));
    nz = (int16_t)(((uint16_t)(dz < 0 ? -dz : dz) << 6) / (uint16_t)(dist >> 2));
    if (dx < 0) nx = -nx;
    if (dz < 0) nz = -nz;
    overlap = ROTO_MIN - dist;
    /* the original's push (overlap * n) >> 1 is 16.16: >> 9 here */
    px = ((overlap >> 2) * (nx >> 1)) >> 6;
    pz = ((overlap >> 2) * (nz >> 1)) >> 6;
    sk_x[0] -= px;
    sk_z[0] -= pz;
    sk_x[1] += px;
    sk_z[1] += pz;

    /* the closing speed along n (the original's rel_vn, >> 8) */
    rel = (((sk_vx[1] - sk_vx[0]) >> 2) * (nx >> 2) >> 4) + (((sk_vz[1] - sk_vz[0]) >> 2) * (nz >> 2) >> 4);
    if (rel >= 0) return;
    /* imp = (-rel_vn * n) >> 8 is 16.16; each gets half: >> 9 more */
    imp = ((-rel >> 3) * (nx >> 2)) >> 4;
    sk_vx[0] -= imp;
    sk_vx[1] += imp;
    imp = ((-rel >> 3) * (nz >> 2)) >> 4;
    sk_vz[0] -= imp;
    sk_vz[1] += imp;
}

static int32_t dist2(int16_t x, int16_t z)
{
    int16_t dx = ball_x - x, dz = ball_z - z;
    if ((uint16_t)(dx + PICKUP_DIST) >= 2 * PICKUP_DIST || (uint16_t)(dz + PICKUP_DIST) >= 2 * PICKUP_DIST)
        return 0x7FFFFFFFL;
    return (int32_t)dx * dx + (int32_t)dz * dz;
}

static void update_ball(void)
{
    if (ball_carrier < 0) {
        int32_t dd1 = dist2(sk_x[0], sk_z[0]), dd2 = dist2(sk_x[1], sk_z[1]);
        const int32_t r2 = (int32_t)PICKUP_DIST * PICKUP_DIST;
        if (dd1 < r2 && dd1 <= dd2) { ball_carrier = 0; sk_ball[0] = 1; G.events |= EV_PICKUP; }
        else if (dd2 < r2) { ball_carrier = 1; sk_ball[1] = 1; G.events |= EV_PICKUP; }
        ball_x += ball_vx;
        ball_z += ball_vz;
        ball_vx = fric(ball_vx);            /* 30 / 32 */
        ball_vz = fric(ball_vz);
    } else {
        /* carried: 2 units in front of the carrier */
        uint8_t c = (uint8_t)ball_carrier;
        ball_x = sk_x[c] + FACE(c) * 2 * ONE;
        ball_z = sk_z[c];
        ball_vx = ball_vz = 0;
    }
    if (ball_z > PITCH_WIDTH) { ball_z = PITCH_WIDTH; ball_vz = -ball_vz; }
    if (ball_z < -PITCH_WIDTH) { ball_z = -PITCH_WIDTH; ball_vz = -ball_vz; }
}

/* the original's CPU (chase the ball, or carry it to P1's goal) is rules.s's
   cpu_controls */

void game_step(uint8_t c1, uint8_t c2)
{
    G.events = 0;
    if (G.state != MS_PLAYING) return;
    if (G.players == 1) c2 = cpu_controls();   /* decided before anything moves */
    us_c = c1;
    update_skimmer(0);
    us_c = c2;
    update_skimmer(1);
    resolve_collision();
    update_ball();
    check_goal();
    if (G.tackle_cooldown) --G.tackle_cooldown;
    if (--G.sub == 0) {                     /* (steps_left + 49) / 50 */
        G.sub = CLOCK_HZ;
        --G.secs;
    }
    if (--G.steps_left == 0) {
        G.state = MS_FULLTIME;
        G.events |= EV_FULL;
    }
}
