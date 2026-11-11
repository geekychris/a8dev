/*
 * bb.h - Block Breaker 3D's rules for the Atari, from the Amiga original's
 * bb.h / bb_core.c: breakout down a tunnel. The paddle is a frame on the
 * tunnel's near opening (z = 0) that moves up, down, left and right; the
 * ball flies down the tunnel to a block of bricks at the far end (6 across,
 * 4 high, 3 deep) and back. Where the ball meets the paddle steers it.
 *
 * World units, y up, z into the tunnel. One tick is 40 ms (every second
 * PAL frame).
 *
 * port: positions and speeds in 1/32 units in 16 bits (the original:
 * 1/256 in 32 bits); 32-bit maths only where the original multiplies two
 * of them (the paddle's steering, the ball's speed, the autopilot).
 */
#ifndef BB_GAME_H
#define BB_GAME_H
#include <stdint.h>

#define BB_COLS    6
#define BB_ROWS    4
#define BB_LAYERS  3
#define BB_CELLS   72
#define BB_W       168
#define BB_H       120
#define BB_D       800
#define BB_CW      56
#define BB_CH      60
#define BB_CD      60
#define BB_Z0      620
#define BB_R       10
#define BB_PAD_W   40
#define BB_PAD_H   28
#define BB_WIDE_W  62
#define BB_WIDE_H  42
#define BB_PAD_SPEED 13
#define BB_LOST_Z  (-60)
#define BB_LIVES   3
#define BB_LEVELS  8
#define BB_MAX_CAPS 4
#define BB_POWER_TICKS 375
#define BB_BETWEEN 50
#define BB_FX      32

enum { BK_EMPTY, BK_RED, BK_ORANGE, BK_YELLOW, BK_GREEN, BK_CYAN, BK_BLUE, BK_TOUGH, BK_STEEL, BK_BONUS, BK_COUNT };
enum { CAP_WIDE, CAP_SLOW, CAP_LIFE, CAP_PLOUGH, CAP_COUNT };

#define BB_IN_LEFT  1
#define BB_IN_RIGHT 2
#define BB_IN_UP    4
#define BB_IN_DOWN  8
#define BB_IN_FIRE  16

enum { BBE_NONE, BBE_WALL, BBE_PADDLE, BBE_HIT, BBE_BREAK, BBE_STEEL, BBE_LOST, BBE_SERVE,
       BBE_CAPSULE, BBE_CAP_MISS, BBE_CLEAR, BBE_LEVEL, BBE_OVER, BBE_WIN, BBE_COUNT };
#define BB_MAX_EVENTS 16

typedef struct {
    uint32_t seed, rng;
    uint8_t cell[BB_CELLS];             /* BK_*: [layer][row][col], layer 0 nearest, row 0 lowest */
    uint8_t hits[BB_CELLS];
    uint8_t level, lives, over, won, left, stuck, between;
    uint16_t score;
    int16_t px, py;                     /* the paddle's centre, 1/32 units */
    int16_t bx, by, bz, vx, vy, vz;     /* the ball */
    int16_t speed;
    uint16_t wide_t, slow_t, plough_t;
    uint16_t paddle_hits, broken, tick;
    uint8_t caps_dropped;
    int16_t cx[BB_MAX_CAPS], cy[BB_MAX_CAPS], cz[BB_MAX_CAPS];
    uint8_t ctype[BB_MAX_CAPS], cactive[BB_MAX_CAPS];
    uint8_t ev[BB_MAX_EVENTS], eva[BB_MAX_EVENTS], evb[BB_MAX_EVENTS], nev;
} BBGame;

extern BBGame G;
extern const char *const bb_level_names[BB_LEVELS];
extern uint8_t bb_dirty;                /* set when a brick changes (the picture redraws) */

#define bb_index(c, r, l) ((uint8_t)(((l) * BB_ROWS + (r)) * BB_COLS + (c)))
uint8_t bb_breakable(uint8_t type);
void bb_new_game(uint32_t seed, uint8_t level);
void bb_load_level(uint8_t level);
void bb_tick(uint8_t in);
uint8_t bb_pad_w(void);
uint8_t bb_pad_h(void);
uint8_t bb_pop_event(uint8_t *a, uint8_t *b);
uint8_t bb_bot(void);
void bb_clear_all(void);

#endif
