/*
 * Uranus Lander - game definitions (port of amiga_mcp/examples/uranus_lander/game.h).
 *
 * The game keeps the original's 320x256 world and 16.16 fixed-point physics,
 * so terrain, pads, gravity, thrust, fuel and the landing rules are exactly
 * the Amiga's; view.c draws that world on the Atari (x / 2, y * 3 / 4).
 * Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

typedef int16_t WORD;          /* port: the Amiga's exec types */
typedef int32_t LONG;
typedef uint32_t ULONG;

/* World constants (the original's screen) */
#define SCREEN_W    320
#define SCREEN_H    256

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX16(x)     ((Fixed)(x) << 16)
#define FIX_ROUND(x) ((WORD)(((x) + 0x8000L) >> 16))   /* the original's FIX_INT */

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_LANDED     2
#define STATE_CRASHING   3
#define STATE_GAMEOVER   4
#define STATE_ENTER_NAME 5

/* Physics defaults (tunable) */
#define GRAVITY_DEFAULT      1000
#define THRUST_DEFAULT       3200
#define FUEL_DEFAULT         600
#define SAFE_VY_DEFAULT      55000L
#define SAFE_VX_DEFAULT      35000L
#define SAFE_ANGLE_DEFAULT   14
#define TURN_RATE_DEFAULT    2
#define START_LIVES_DEFAULT  3

/* Terrain */
#define TERRAIN_W    320
#define TERRAIN_MIN  130
#define TERRAIN_MAX  238

#define MAX_PADS      5
#define MAX_PARTICLES 32
#define MAX_STARS     60

typedef struct {
    Fixed x, y;
    Fixed vx, vy;
    WORD angle;        /* 0-255, 0 = up */
    WORD fuel;
    uint8_t alive;
    uint8_t thrusting;
} Ship;

typedef struct {
    WORD x;
    WORD width;
    WORD y;
    WORD multiplier;   /* 1, 2, 3, or 5 */
} LandingPad;

/* port: particles and stars are arrays of their fields (fx.s works on
   them): the original's Particle {x, y, vx, vy, life, color, active} and
   Star {x, y, brightness, speed, accum} */
extern Fixed pt_x[MAX_PARTICLES], pt_y[MAX_PARTICLES], pt_vx[MAX_PARTICLES], pt_vy[MAX_PARTICLES];
extern uint8_t pt_life[MAX_PARTICLES], pt_on[MAX_PARTICLES];
extern uint8_t pt_live;                   /* how many are active */
extern WORD st_x[MAX_STARS];
extern uint8_t st_y[MAX_STARS], st_speed[MAX_STARS], st_accum[MAX_STARS];

typedef struct {
    LONG gravity;
    LONG thrust;
    LONG fuel_max;
    LONG safe_vy;
    LONG safe_vx;
    LONG safe_angle;
    LONG turn_rate;
    LONG start_lives;
} Tunables;

typedef struct {
    uint8_t left;
    uint8_t right;
    uint8_t thrust;
    uint8_t up;
} InputState;

typedef struct {
    Ship ship;
    LandingPad pads[MAX_PADS];
    WORD num_pads;
    WORD terrain_y[TERRAIN_W];

    LONG score;
    WORD lives;
    WORD level;
    WORD state;
    WORD state_timer;
    WORD frame;

    WORD landed_pad;
    LONG land_bonus;

    /* sound event flags */
    uint8_t ev_thrust;
    uint8_t ev_crash;
    uint8_t ev_land;
    uint8_t ev_low_fuel;
} GameState;

extern GameState gs;
extern Tunables g_tune;

void game_init(void);
void game_new_level(void);
void game_update(InputState *inp);
void stars_init(void);

#endif /* GAME_H */
