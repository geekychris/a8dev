/*
 * st.h - Stack Tower's rules, ported from the Amiga original's st_core.c
 * (itself the browser game's stack-tower-core.js in C): falling pieces land
 * on the outside of a round tower of 16 columns that wrap round, so moving
 * a piece left or right turns the tower under it. A full ring clears.
 *
 * The same random generator (mulberry32), bag and rules, so a seed deals
 * the same pieces as the browser and the Amiga; times are milliseconds.
 * port: 8-bit fields where the values fit (cc65), the 64-bit multiply of
 * rand_below done in 32 bits, the seed key built without sprintf.
 */
#ifndef ST_H
#define ST_H
#include <stdint.h>

#define ST_COLS     16
#define ST_ROWS     20
#define ST_HIDDEN   4
#define ST_TOTAL    (ST_ROWS + ST_HIDDEN)
#define ST_GARBAGE  9
#define ST_NPIECES  8
#define ST_LOCK_MS  500
#define ST_LOCK_RESETS 15
#define ST_DIG_ROWS 10
#define ST_QUEUE    24

enum { MODE_MARATHON, MODE_RISING, MODE_DIG, MODE_COUNT };

enum { STE_NONE, STE_MOVE, STE_ROTATE, STE_KICK, STE_DROP, STE_HOLD, STE_LOCK, STE_CLEAR,
       STE_LEVEL, STE_GARBAGE, STE_WIN, STE_OVER, STE_COUNT };
typedef struct { uint8_t type; int16_t a, b; } StEvent;
#define ST_MAX_EVENTS 16

typedef struct { int8_t type, rot, col, row; } Piece;

typedef struct {
    uint8_t mode;
    uint32_t seed, rng;
    uint8_t board[ST_COLS * ST_TOTAL];        /* bottom row first; 0 empty, 1-8 pieces, 9 rubble */
    uint8_t queue[ST_QUEUE], nqueue;
    int8_t hold;                              /* -1: none */
    uint8_t can_hold;
    Piece piece;
    int16_t level, start_level, lines, combo, pieces;
    int32_t score, time_ms;
    int16_t fall_ms, lock_ms, resets;
    uint8_t soft, over, won;
    int16_t rise_ms, garbage_left;
    int8_t last_col;
    StEvent ev[ST_MAX_EVENTS];
    uint8_t nev;
} StGame;

extern const int8_t st_cells[ST_NPIECES][5][2];
extern const uint8_t st_ncells[ST_NPIECES];
extern const char *const st_mode_names[MODE_COUNT];

uint32_t st_hash(const char *s);
uint32_t st_rand32(uint32_t *a);
uint8_t st_rand_below(uint32_t *a, uint8_t n);
/* cells of p: columns wrapped (0..15), rows as they are; returns the count */
uint8_t st_cells_of(const Piece *p, int8_t out[5][2]);
void st_init_cells(void);              /* once, before the first game */
/* st_fast.s, st_cellsp: the cells into st_cx/st_cy (columns wrapped), the count */
uint8_t __fastcall__ st_cellsp(const Piece *p);
extern int8_t st_cx[5], st_cy[5];
uint8_t __fastcall__ st_collides(const Piece *p);
int16_t st_interval_ms(int16_t level);

void st_new_game(uint8_t mode, uint32_t seed, int16_t level, int8_t col);
uint8_t st_preview(uint8_t n, uint8_t *out);
uint8_t st_move(int8_t dir);
uint8_t st_rotate(int8_t dir);
int8_t st_ghost_row(void);
uint8_t st_hard_drop(void);
void st_soft_drop(uint8_t on);
uint8_t st_hold(void);
void st_add_garbage(uint8_t gaps);
void st_tick(int16_t dt_ms);
uint8_t st_pop_event(StEvent *e);

extern StGame game;               /* the one game the rules work on */

#endif
