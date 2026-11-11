/*
 * td.h - Tank Duel's rules (the original's td.h / td_core.c, via the
 * Spectrum Next port), for the 6502.
 *
 * A tank fight in a walled arena, seen from the turret. Shells bounce once
 * off the walls (and off a bulwark's shield), crates break. Three kinds of
 * enemy: scouts circle and snipe, mortars lob shells where you will be,
 * bulwarks carry a shield across their front. Eight rounds.
 *
 * x across, z ahead, angles 16-bit (0 = +z, a quarter turn = +x).
 *
 * port (Atari edition): the original's 16.16 fixed point is 32-bit
 * arithmetic, library calls in cc65; here:
 *  - positions are int16 in 1/16 of a unit (the arena is +-1000 units:
 *    +-16000), speeds the same per tick;
 *  - a tick is 80 ms, two of the original's: speeds, turns and shell
 *    steps are doubled, the timers count half as many ticks; the rules run
 *    12.5 times a second;
 *  - sines are Q8 from a 256-step table, lengths are alpha-max-beta-min
 *    (within 4%), so a fight goes as the original's but not bit for bit.
 * tools/run_orig.c and tools/run_port.c play the autopilot on both, on the
 * host, over 20 seeds, to compare rounds, kills and scores.
 */
#ifndef TD_GAME_H
#define TD_GAME_H

#include <stdint.h>

typedef int16_t pos;                    /* 1/16 of a unit */
#define U(i)        ((pos)((i) * 16))   /* units -> pos */
#define UNITS(p)    ((p) >> 4)

#define TD_ARENA     1000               /* the walls: x and z from -1000 to 1000 */
#define TD_MAX_OBS   13
#define TD_MAX_TANKS 8                  /* tank 0 is the player */
#define TD_MAX_SHELLS 12                /* port: 16 */
#define TD_MAX_MORTARS 6
#define TD_ROUNDS    8
#define TD_TANK_R    28
#define TD_SHELL_STEP U(16)             /* port: a shell moves 16 units a step, 4 steps a tick (64 a tick: the original's 32 an original tick, in steps of 8) */
#define TD_SHELL_STEPS 4
#define TD_SHELL_LIFE 25                /* ticks (50 of the original's) */
#define TD_RELOAD    13                 /* port: 25 original ticks */
#define TD_HP        100
#define TD_SHELL_DMG 20
#define TD_SPLASH_R  90
#define TD_SPLASH_DMG 35
#define TD_BETWEEN   38                 /* port: 75 */

/* input bits */
#define TD_IN_LEFT  1
#define TD_IN_RIGHT 2
#define TD_IN_UP    4
#define TD_IN_DOWN  8
#define TD_IN_FIRE  16

enum { OB_PILLAR, OB_PYRAMID, OB_CRATE };
enum { TK_PLAYER, TK_SCOUT, TK_MORTAR, TK_BULWARK, TK_COUNT };

enum { TDE_NONE, TDE_FIRE, TDE_EFIRE, TDE_LOB, TDE_BOUNCE, TDE_GLANCE, TDE_HIT, TDE_KILL, TDE_HURT, TDE_SPLASH,
       TDE_CRATE_HIT, TDE_CRATE_BREAK, TDE_ROUND_CLEAR, TDE_ROUND, TDE_DEAD, TDE_WIN, TDE_COUNT };
typedef struct { uint8_t type, a; int16_t b; pos x, z; } TDEvent;
#define TD_MAX_EVENTS 16

typedef struct {
    uint8_t kind, hp;                   /* OB_*; hp: crates' hits left (0: gone) */
    pos x, z;
    uint8_t r;                          /* pillars, pyramids: radius; crates: half size (units) */
} Obstacle;

typedef struct {
    uint8_t type, alive;
    int8_t hp;
    pos x, z, v;                        /* v: pos a tick */
    uint16_t yaw;
    int8_t reload, orbit;               /* orbit: which way a scout circles */
    uint8_t ai_a, ai_b;                 /* port: the mind's clocks, 0..59 and 0..199 (no divisions) */
    uint8_t held;                       /* a practice target: doesn't move or shoot */
    uint16_t want;                      /* port: the mind's last heading and speed (it */
    pos speed;                          /*   thinks every other tick) */
} Tank;

typedef struct {
    uint8_t alive, owner, bounces, life;
    pos x, z, dx, dz;                   /* dx, dz: a step's move */
} Shell;

typedef struct {
    uint8_t alive, owner, t, flight;    /* ticks in the air, and how long it will be */
    pos x0, z0, tx, tz;                 /* from, and where it will land */
} Mortar;

typedef struct {
    uint32_t seed, rng;
    uint8_t round, over, won, between;
    Tank tk[TD_MAX_TANKS];
    uint8_t ntanks;
    Obstacle ob[TD_MAX_OBS];
    uint8_t nobs;
    Shell sh[TD_MAX_SHELLS];
    Mortar mo[TD_MAX_MORTARS];
    int32_t score;
    uint16_t tick;
    uint8_t kills;
    uint16_t shots, hits, damage_taken;
    TDEvent ev[TD_MAX_EVENTS];
    uint8_t nev;
} TDGame;

extern TDGame G;
extern const char *const td_tank_names[TK_COUNT];

uint32_t td_rand(void);
void td_new_game(uint32_t seed, uint8_t round);
void td_load_round(uint8_t round);
void td_tick(uint8_t in);
uint8_t td_pop_event(TDEvent *e);
uint8_t td_bot(void);                   /* the autopilot's input */
uint8_t td_enemies_left(void);
uint16_t td_angle_to(pos dx, pos dz);   /* the heading towards (dx, dz) */
uint8_t td_blocked(pos x, pos z, uint8_t r);   /* inside an obstacle or a wall? */
uint8_t td_fire(uint8_t tank);          /* 1 if a shell went */

/* maths (td_fix.c, atan.s, fast.s): the angle of a vector, a length */
uint16_t td_atan2(pos x, pos z);        /* the angle of (x, z) from +z towards +x */
uint16_t td_len(pos x, pos z);          /* |(x, z)| within 4% */
#ifndef HOST                            /* port: straight to the asm (atan.s, fast.s) */
extern pos at_x, at_z, ln_x, ln_z;
extern uint16_t __fastcall__ td_atan2_(void);
extern uint16_t __fastcall__ td_len_(void);
#define td_atan2(x, z) (at_x = (x), at_z = (z), td_atan2_())
#define td_len(x, z)   (ln_x = (x), ln_z = (z), td_len_())
#endif
pos td_scale(pos v, int16_t s14);       /* v * s / 16384 */
/* port: Q8 sines, and v * s8 / 256 by a8_3d's multiply (asm) */
extern const int16_t td_sin8_tab[320];
#define td_sin8(a)  (td_sin8_tab[(uint8_t)((uint16_t)(a) >> 8)])
#define td_cos8(a)  (td_sin8_tab[(uint8_t)((uint16_t)(a) >> 8) + 64])
extern int16_t a3d_mb;
extern int16_t __fastcall__ a3d_mulf_(int16_t a);
#define td_scale8(v, s8) (a3d_mb = (s8), a3d_mulf_(v))

#endif
