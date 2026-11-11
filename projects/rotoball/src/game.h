/*
 * Rotoball: the game's state and rules (the original's rotoball.h and the
 * gameplay half of its main.c).
 *
 * Coordinates are the original's: +X down the pitch towards P2's goal, -X
 * towards P1's; +Z is P1's left. port: 8.8 fixed point in 16 bits (1/256
 * of a unit) where the original has 16.16 in 32. Its rules work in 1/256
 * anyway (every product is (a >> 8) * (b >> 8)), so the accelerations come
 * out exactly, truncations and all (12 and -13, 10 and -11).
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define ONE           256
#define PITCH_LENGTH  (100 * ONE)          /* goals at +/- 100 */
#define PITCH_WIDTH   (40 * ONE)           /* sidelines at +/- 40 */
#define HOVER_H       (2 * ONE)

#define MS_TITLE     0
#define MS_PLAYING   1
#define MS_FULLTIME  2

#define CLOCK_HZ      50
#define MATCH_STEPS   (90 * CLOCK_HZ)      /* 90 seconds */

/* a player's controls for a step */
#define C_FWD   1
#define C_BACK  2
#define C_LEFT  4
#define C_RIGHT 8

/* the last step's events */
#define EV_PICKUP  1
#define EV_TACKLE  2
#define EV_GOAL1   4                        /* P1 scored */
#define EV_GOAL2   8                        /* P2 scored */
#define EV_FULL    16

/* skimmer 0 is P1 (the bottom pane, facing +X), 1 is P2 or the CPU (the top
   pane, facing -X). port: the structs as arrays */
extern int16_t sk_x[2], sk_z[2], sk_vx[2], sk_vz[2];
extern uint8_t sk_ball[2];
extern int16_t ball_x, ball_z, ball_vx, ball_vz;
extern int8_t ball_carrier;                 /* -1 free, 0 P1, 1 P2 */

typedef struct {
    uint8_t score[2];
    uint16_t steps_left;
    uint8_t secs, sub;                      /* the clock: seconds shown (rounded up), steps to the next */
    uint8_t state;                          /* MS_* */
    uint8_t players;                        /* 1: P2 is the CPU */
    uint8_t events;                         /* EV_* of the last step */
    uint8_t last_pts;
    uint8_t tackle_cooldown, tackle_lock;
} Game;
extern Game G;

void game_init(void);
void game_start(uint8_t players);
void game_step(uint8_t c1, uint8_t c2);
uint8_t cpu_controls(void);

#endif
