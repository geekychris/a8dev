/*
 * td.h - Tank Duel's rules for the Atari, from the Amiga original's td.h /
 * td_core.c: a tank fight in a walled arena, seen from the turret. Shells
 * bounce once off the arena's walls, so a shot can be banked round cover.
 * Three kinds of enemy:
 *
 *   scout    quick and light; circles you and snipes
 *   mortar   slow; lobs shells that land where you will be (a ring shows
 *            where), hurting everything near
 *   bulwark  a heavy tank with a shield across its front: shots there
 *            glance off, so hit it from the side or behind
 *
 * Crates break after two hits; pillars and pyramids stand. Eight rounds.
 *
 * x across, z ahead, angles 16-bit (0 = +z, a quarter turn = +x), one tick
 * 40 ms (every second PAL frame).
 *
 * port: positions in 1/16 units and speeds in 1/16 units a tick, in 16 bits
 * (the original: 16.16); distances from a square root only where the
 * original's push needs one, compared squared elsewhere; atan2 from a table.
 */
#ifndef TD_GAME_H
#define TD_GAME_H
#include <stdint.h>

#define TD_ARENA     1000
#define TD_MAX_OBS   13
#define TD_MAX_TANKS 8                  /* tank 0 is the player */
#define TD_MAX_SHELLS 12
#define TD_MAX_MORTARS 6
#define TD_ROUNDS    8
#define TD_TANK_R    28
#define TD_P         16                 /* position units a world unit */
#define TD_SHELL_SPEED (32 * TD_P)
#define TD_SHELL_LIFE 50
#define TD_RELOAD    25
#define TD_HP        100
#define TD_SHELL_DMG 20
#define TD_SPLASH_R  90
#define TD_SPLASH_DMG 35
#define TD_BETWEEN   75
#define TD_MORTAR_FLIGHT 45

#define TD_IN_LEFT  1
#define TD_IN_RIGHT 2
#define TD_IN_UP    4
#define TD_IN_DOWN  8
#define TD_IN_FIRE  16

enum { OB_PILLAR, OB_PYRAMID, OB_CRATE };
enum { TK_PLAYER, TK_SCOUT, TK_MORTAR, TK_BULWARK, TK_COUNT };

enum { TDE_NONE, TDE_FIRE, TDE_EFIRE, TDE_LOB, TDE_BOUNCE, TDE_GLANCE, TDE_HIT, TDE_KILL, TDE_HURT, TDE_SPLASH,
       TDE_CRATE_HIT, TDE_CRATE_BREAK, TDE_ROUND_CLEAR, TDE_ROUND, TDE_DEAD, TDE_WIN, TDE_COUNT };
#define TD_MAX_EVENTS 16                /* a power of two */

typedef struct {
    uint16_t seed;
    uint16_t rng;
    uint8_t round, over, won, between;
    /* the tanks, as arrays (cc65 likes them better than structs) */
    uint8_t ntanks;
    uint8_t t_type[TD_MAX_TANKS], t_alive[TD_MAX_TANKS], t_held[TD_MAX_TANKS];
    int8_t t_hp[TD_MAX_TANKS], t_orbit[TD_MAX_TANKS];
    int16_t t_x[TD_MAX_TANKS], t_z[TD_MAX_TANKS], t_v[TD_MAX_TANKS];
    uint16_t t_yaw[TD_MAX_TANKS], t_ai[TD_MAX_TANKS];
    uint8_t t_reload[TD_MAX_TANKS];
    /* shells */
    uint8_t s_alive[TD_MAX_SHELLS], s_owner[TD_MAX_SHELLS], s_bounces[TD_MAX_SHELLS], s_life[TD_MAX_SHELLS];
    int16_t s_x[TD_MAX_SHELLS], s_z[TD_MAX_SHELLS], s_dx[TD_MAX_SHELLS], s_dz[TD_MAX_SHELLS];
    /* mortar rounds: from, to, ticks in the air */
    uint8_t m_alive[TD_MAX_MORTARS], m_owner[TD_MAX_MORTARS], m_t[TD_MAX_MORTARS];
    int16_t m_x0[TD_MAX_MORTARS], m_z0[TD_MAX_MORTARS], m_tx[TD_MAX_MORTARS], m_tz[TD_MAX_MORTARS];
    uint16_t score, tick;
    uint16_t kills, shots, hits, damage_taken;
    uint8_t ev[TD_MAX_EVENTS], eva[TD_MAX_EVENTS], evb[TD_MAX_EVENTS], nev, evr;
    int16_t evx[TD_MAX_EVENTS], evz[TD_MAX_EVENTS];
} TDGame;

extern TDGame G;
/* the obstacles (outside G for the asm): kind, hp (crates' hits left),
   position (1/16 units), radius or half size (units) */
extern uint8_t nobs, o_kind[TD_MAX_OBS], o_hp[TD_MAX_OBS], o_r[TD_MAX_OBS];
extern int16_t o_x[TD_MAX_OBS], o_z[TD_MAX_OBS];
extern const char *const td_tank_names[TK_COUNT];

void td_new_game(uint16_t seed, uint8_t round);
void td_load_round(uint8_t round);
void td_tick(uint8_t in);
uint8_t td_pop_event(uint8_t *a, uint8_t *b, int16_t *x, int16_t *z);
uint8_t td_bot(void);
uint8_t td_enemies_left(void);

#endif
