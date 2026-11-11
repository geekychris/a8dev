/*
 * mt.h - Marble Tilt's rules for the Atari, from the Amiga original's
 * mt_core.c: tilt a floating maze board to roll a marble to the exit. The
 * board is 12 x 12 cells written as text, one character a cell:
 *
 *   .  floor          #  wall           (space)  a hole: the marble falls
 *   S  start          E  the exit       C  a checkpoint (you come back here)
 *   B  a bumper: it kicks the marble away
 *   G  a gate: open for three seconds, shut for three
 *   Z  sand: the marble drags through it
 *
 * port: 16-bit fixed point (the original: 16.16). Positions in 1/64 of a
 * board unit (a cell is 40), velocities in 1/256 unit a tick, the tilt in
 * 1/256 radian; the marble moves in 2 steps a tick against the walls (the
 * original: 4). One tick is 40 ms (every second PAL frame).
 */
#ifndef MT_H
#define MT_H
#include <stdint.h>

#define MT_N        12
#define MT_CELL     40
#define MT_R        9
#define MT_LEVELS   8
#define MT_TILT_MAX 31                  /* 0.12 rad, 1/256 rad */
#define MT_GATE_T   75
#define MT_FALL_T   25
#define MT_BETWEEN  60
#define MT_P        64                  /* position units a board unit */

#define MT_IN_LEFT  1
#define MT_IN_RIGHT 2
#define MT_IN_UP    4                   /* tilt away: the marble rolls away from you */
#define MT_IN_DOWN  8

enum { MTE_NONE, MTE_WALL, MTE_BUMP, MTE_FALL, MTE_RESPAWN, MTE_CHECKPOINT, MTE_GATE, MTE_EXIT, MTE_LEVEL,
       MTE_TIMEOUT, MTE_OVER, MTE_WIN };

typedef struct {
    uint8_t level, over, won, between;
    char cell[MT_N][16];                /* [row][col], row 0 the far edge (stride 16: shifts) */
    int16_t x, z, vx, vz;               /* the marble: 1/64 unit from the middle (z away), 1/256 a tick */
    int8_t roll, pitch;                 /* the tilt (1/256 rad): roll lowers the right side, pitch the far side */
    uint8_t falling;                    /* ticks left of a fall, 0 = rolling */
    uint8_t cp_row, cp_col;
    uint8_t gates_open;
    uint16_t falls, bumps;
    int16_t time_left;                  /* ticks */
    uint16_t level_ticks;
    uint16_t score;
    uint8_t ev[8], eva[8], nev;
} MTGame;

extern MTGame G;
extern const char *const mt_level_names[MT_LEVELS];

void mt_new_game(uint8_t level);
void mt_tick(uint8_t in);
uint8_t mt_pop_event(uint8_t *a);       /* the event (MTE_), its value into *a */
uint8_t mt_bot(void);                   /* the autopilot's input */
int8_t mt_col_of(int16_t x);
int8_t mt_row_of(int16_t z);
char mt_at(int8_t row, int8_t col);    /* '#' outside the board */

#endif
