/*
 * Rock Blaster (Atari 8-bit) - game definitions. Port of
 * amiga_mcp/examples/rock_blaster/game.h, by way of the ZX Spectrum port
 * (spectrum_dev/projects/rock_blaster), which did the same sums in 16 bits.
 *
 * port: the world is 320 x 216 (the original's 320 x 256, cut to fit the
 * Atari's screen at one scan line a pixel), shown at 160 colour clocks
 * across: every speed, radius and shape is the original's. Positions are
 * 16-bit (1/128 px) except the ship's, which keeps the original's 16.16.
 * port (speed): objects are arrays, one entry each (objs.s walks them).
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define WORLD_W 320
#define WORLD_H 216                         /* port: was 256 */

typedef int32_t Fixed;                      /* 16.16, the ship */
#define FX16(x)     ((Fixed)(x) << 16)

#define P7(px)      ((uint16_t)(px) << 7)   /* 1/128 px: everything else */
#define V7(v)       ((int16_t)((v) * 128))
#define W7          P7(WORLD_W)
#define H7          P7(WORLD_H)

/* Ship */
#define SHIP_MAX_SPEED  FX16(3)
#define SHIP_TURN_SPEED 3
#define SHIP_RADIUS     8
#define SHIP_INVULN_TIME 100

extern Fixed ship_x, ship_y, ship_dx, ship_dy;
extern uint8_t ship_angle;                  /* 0 = right, 64 = down */
extern uint8_t ship_alive, ship_thrust_on;
extern uint8_t ship_invuln;
extern int16_t ship_px, ship_py;            /* whole pixels */

/* Bullets */
#define MAX_BULLETS     8
#define BULLET_LIFE     40
extern uint16_t bl_x[MAX_BULLETS], bl_y[MAX_BULLETS];
extern int16_t bl_dx[MAX_BULLETS], bl_dy[MAX_BULLETS];
extern int16_t bl_px[MAX_BULLETS], bl_py[MAX_BULLETS];
extern uint8_t bl_active[MAX_BULLETS], bl_life[MAX_BULLETS];

/* Rocks */
#define ROCK_LARGE   0
#define ROCK_MEDIUM  1
#define ROCK_SMALL   2
#define MAX_ROCKS    24
extern uint16_t rk_x[MAX_ROCKS], rk_y[MAX_ROCKS];
extern int16_t rk_dx[MAX_ROCKS], rk_dy[MAX_ROCKS];
extern int16_t rk_px[MAX_ROCKS], rk_py[MAX_ROCKS];
extern uint8_t rk_active[MAX_ROCKS], rk_size[MAX_ROCKS], rk_shape[MAX_ROCKS];

/* Particles */
#define MAX_PARTICLES 64
extern uint16_t pt_x[MAX_PARTICLES], pt_y[MAX_PARTICLES];
extern int16_t pt_dx[MAX_PARTICLES], pt_dy[MAX_PARTICLES];
extern uint8_t pt_active[MAX_PARTICLES], pt_life[MAX_PARTICLES];
extern uint8_t particles_live;

/* Game state */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DEAD     2
#define STATE_GAMEOVER 3
extern uint32_t gs_score;
extern int8_t gs_lives;
extern uint8_t gs_level, gs_state;
extern int16_t gs_state_timer;
extern int8_t gs_rock_count;
extern uint16_t gs_frame;

/* Tunables (the original's defaults, *100) */
#define TUNE_ROCK_SPEED      80
#define TUNE_SHIP_THRUST     20
#define TUNE_BULLET_SPEED    400
#define TUNE_START_LIVES     3
#define TUNE_START_ROCKS     4
#define TUNE_ROCKS_PER_LEVEL 2

void game_init(void);
void game_spawn_rocks(uint8_t count);
void game_update(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire);
void game_spawn_particles(uint16_t x, uint16_t y, uint8_t count);

extern const uint8_t rock_radius_tab[3];

#define SCORE_LARGE  20
#define SCORE_MEDIUM 50
#define SCORE_SMALL  100

/* sound effects (main.c) */
void sfx_shoot(void);
void sfx_explode_large(void);
void sfx_explode_small(void);
void sfx_die(void);

/* phys.s */
void ship_move(void);                       /* drag, move, wrap, px/py */
extern int16_t ma, mb;
extern int32_t mres;
void mul16s(void);                          /* mres = ma * mb */

/* objs.s */
void rocks_move(void);                      /* x += dx, y += dy, wrapped; px, py */
void bullets_move(void);                    /* the same for the active bullets */
void particles_move(void);                  /* move, slow down, age; gone off screen */
uint8_t __fastcall__ hit_first(uint8_t r);  /* first rock within r + its radius of hit_px/py, or 0xFF */
void hits_build(void);
uint8_t __fastcall__ bullet_scan(uint8_t i);  /* next bullet from i that hits; hit_rock */
extern uint8_t hit_rock;
void particles_table(void);                 /* game.c: the velocities, at start-up */                      /* the live rocks' list for hit_first (rocks_move makes it too) */
extern int16_t hit_px, hit_py;
#endif
