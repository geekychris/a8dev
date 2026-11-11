/*
 * Lunar Rider - game definitions (port of amiga_mcp/examples/lunar_rider/game.h).
 * The original's constants and structures; changes for the Atari are
 * marked "port:". Coordinates stay the original's 320 x 256 screen.
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

typedef int16_t WORD;          /* port: the Amiga's exec types */
typedef int32_t LONG;

/* Screen constants */
#define SCREEN_W    320
#define SCREEN_H    256

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_GAMEOVER   3
#define STATE_CHECKPOINT 4

/* Terrain constants */
/* port: a 1024-pixel ring (the original's is 640): indices are a mask
   instead of a 32-bit modulo; the world it generates is the same */
#define TERRAIN_LEN     1024
#define TERRAIN_MASK    1023
#define GROUND_Y        200     /* ground baseline Y */
#define TERRAIN_FLAT    0
#define TERRAIN_CRATER_SM 1     /* small crater */
#define TERRAIN_CRATER_LG 2     /* large crater */
#define TERRAIN_ROCK    3       /* boulder obstacle */
#define TERRAIN_HILL    4       /* cosmetic bump */
#define TERRAIN_MINE    5       /* ground mine */

/* Buggy constants */
#define BUGGY_W         24
#define BUGGY_H         12
#define BUGGY_X         80      /* fixed screen X position */
#define BUGGY_GROUND_Y  (GROUND_Y - BUGGY_H)
#define JUMP_VEL        (-600)  /* initial jump velocity (fixed point /100) */
#define GRAVITY         40      /* gravity per frame (/100) */

/* Scroll speeds (fixed point /100) */
#define SPEED_NORMAL    150
#define SPEED_FAST      300
#define SPEED_SLOW      50

/* Bullet limits */
#define MAX_FWD_BULLETS 2
#define MAX_UP_BULLETS  2
#define MAX_BOMBS       8
#define MAX_ENEMIES     8
#define MAX_EXPLOSIONS  6

/* Enemy types */
#define ENEMY_NONE      0
#define ENEMY_UFO_SM    1
#define ENEMY_UFO_LG    2
#define ENEMY_METEOR    3

/* Checkpoint distance */
#define CHECKPOINT_DIST 500

/* port: the original's Buggy, Bullet, Enemy, Bomb and Explosion structs
   are arrays of their fields here ("struct of arrays"): cc65 reaches
   en_x[i] in two instructions, e->x through a pointer in about twenty, and
   the logic ran at a third of the frame rate that way. Same fields, same
   rules; the velocities that never change (a forward bullet's dx = 5, an
   upward one's dy = -5, a bomb's dy = 2) are constants. */
typedef struct {
    WORD x, y;          /* screen position */
    WORD vy;            /* vertical velocity (/100) */
    uint8_t on_ground;  /* flag */
    uint8_t alive;
    uint8_t wheel_frame;/* 0 or 1 */
    uint8_t vk;         /* port: vy = JUMP_VEL + vk * GRAVITY (see game.c) */
} Buggy;

extern WORD fb_x[MAX_FWD_BULLETS], fb_y[MAX_FWD_BULLETS];
extern uint8_t fb_on[MAX_FWD_BULLETS];
extern WORD ub_x[MAX_UP_BULLETS], ub_y[MAX_UP_BULLETS];
extern uint8_t ub_on[MAX_UP_BULLETS];
extern WORD en_x[MAX_ENEMIES], en_y[MAX_ENEMIES];
extern int8_t en_dx[MAX_ENEMIES], en_dy[MAX_ENEMIES];
extern uint8_t en_type[MAX_ENEMIES], en_on[MAX_ENEMIES], en_bomb[MAX_ENEMIES], en_flash[MAX_ENEMIES];
extern int8_t en_hp[MAX_ENEMIES];
extern WORD bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
extern uint8_t bm_on[MAX_BOMBS];
extern WORD ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
extern uint8_t ex_r[MAX_EXPLOSIONS], ex_on[MAX_EXPLOSIONS];
extern int8_t ex_life[MAX_EXPLOSIONS];
/* how many of each are active (loops over none are skipped) */
extern uint8_t n_fb, n_ub, n_en, n_bm, n_ex;

typedef struct {
    /* port: the original's scroll_x is 1/100 pixel in a LONG, divided by
       100 wherever it is used; here whole pixels and the hundredths apart */
    uint32_t scroll_px;         /* total distance scrolled (world pixels) */
    uint8_t scroll_sub;         /* and 1/100 pixels, 0..99 */
    WORD scroll_speed;          /* current speed (/100) */

    Buggy buggy;

    LONG score;
    WORD lives;
    WORD state;
    WORD state_timer;
    WORD frame;
    uint8_t frame60;            /* port: frame % 60, counted */

    WORD checkpoint_cur;        /* current checkpoint letter (0=A) */
    WORD checkpoint_dist;       /* distance to next checkpoint (world pixels) */

    WORD difficulty;            /* increases each loop */

    /* Sound event flags - set by game, consumed by main loop */
    uint8_t ev_shoot;
    uint8_t ev_explode;
    uint8_t ev_jump;
    uint8_t ev_checkpoint;
    uint8_t ev_death;

    /* port: terrain changed after it was drawn (a rock shot, a mine set
       off): world pixels, for the renderer to redraw */
    uint32_t dirty[4];
    uint8_t n_dirty;
    uint8_t death_cause;        /* port: for the log: DEATH_* */
} GameState;

/* port: the terrain ring is outside GameState (two 1K arrays) */
extern uint8_t terrain[TERRAIN_LEN];     /* TERRAIN_* at each X pixel; a hill
                                            has its bump (0..8) in bits 4-7 */

/* port: what killed the buggy (the log says) */
#define DEATH_CRATER 1
#define DEATH_ROCK   2
#define DEATH_MINE   3
#define DEATH_BOMB   4
#define DEATH_ENEMY  5

typedef struct {
    uint8_t left;
    uint8_t right;
    uint8_t jump;
    uint8_t fire;
} InputState;

extern GameState gs;
extern uint16_t gen_x;                   /* port: terrain_gen_x (low 16 bits) */
void game_init(GameState *g);
void game_update(GameState *g, InputState *inp);
void game_generate(GameState *g);        /* port: terrain, a few pixels a frame */
#define buggy_world_x(g) ((g)->scroll_px + BUGGY_X)

#endif
