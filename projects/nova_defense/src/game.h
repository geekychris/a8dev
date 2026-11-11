/*
 * Nova Defense (Atari 8-bit) - game definitions. Port of
 * amiga_mcp/examples/nova_defense/game.h; the rules run in the original's
 * 320 x 256 coordinates (the Atari shows x at half width, y one scan line a
 * pixel). Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 256

#define ALIEN_COLS 8
#define ALIEN_ROWS 6
#define ALIEN_W 14
#define ALIEN_H 10
#define ALIEN_CELL_W 26
#define ALIEN_CELL_H 16         /* port: 15; an alien row is two character rows */
#define ALIEN_SHIFT 12          /* port: 13 (half a cell): whole colour clocks */
#define ALIEN_ROW_SHIFT(r) (((r) & 1) ? ALIEN_SHIFT : 0)
#define ALIEN_GRID_W (ALIEN_COLS * ALIEN_CELL_W + ALIEN_CELL_W / 2)
#define ALIEN_START_X ((SCREEN_W - ALIEN_GRID_W) / 2)
#define ALIEN_START_Y 46

#define ALIEN_TYPE_C 0          /* rows 0-1: eye pods, 30 points */
#define ALIEN_TYPE_B 1          /* rows 2-3: rays, 20 */
#define ALIEN_TYPE_A 2          /* rows 4-5: spore crawlers, 10 */

#define SHIELD_COUNT 3
#define SHIELD_W 32
#define SHIELD_H 14
#define SHIELD_Y 200

#define PLAYER_W 15
#define PLAYER_H 8
#define PLAYER_Y 232
#define PLAYER_SPEED 3
#define PLAYER_MIN_X 8
#define PLAYER_MAX_X (SCREEN_W - 8 - PLAYER_W)

#define MAX_ALIEN_BULLETS 3
#define PLAYER_BULLET_SPEED 10
#define ALIEN_BULLET_SPEED 3

#define UFO_Y 36
#define UFO_W 16
#define UFO_H 7
#define UFO_SPEED 1

#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_GAMEOVER   3
#define STATE_WAVE_CLEAR 4

#define MAX_EXPLOSIONS 8
#define EXP_ALIEN 0
#define EXP_UFO   1
#define EXP_PLAYER 2

typedef struct { int16_t x, y; uint8_t active; } Bullet;

/* port (speed): alive[][] as a bit mask per row (bit c = column c) */
extern uint8_t alive_mask[ALIEN_ROWS];
extern int16_t grid_x, grid_y;
extern int8_t swarm_dir;
extern uint8_t move_timer, move_delay, anim_frame, alive_count, fire_timer, fire_delay;

extern int16_t player_x;
extern int8_t lives;
extern uint32_t score, hiscore;
extern uint8_t wave, state;
extern int16_t state_timer;

extern Bullet player_bullet;
extern Bullet alien_bullets[MAX_ALIEN_BULLETS];

extern int16_t ufo_x;
extern int8_t ufo_dir;
extern uint8_t ufo_active;
extern int16_t ufo_timer, ufo_score_val;

extern uint8_t shield_px[SHIELD_COUNT + 1][SHIELD_H][SHIELD_W];   /* [3]: a fresh shield */
extern int16_t shield_x[SHIELD_COUNT];
extern uint16_t shield_dirty;           /* bit per shield row changed: render.c */

/* explosions: where, how long, what (EXP_*); for an alien its row/column */
extern int16_t ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
extern uint8_t ex_timer[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS], ex_kind[MAX_EXPLOSIONS];
extern uint8_t ex_row[MAX_EXPLOSIONS], ex_col[MAX_EXPLOSIONS];

/* sound events, for the main loop */
extern uint8_t ev_shoot, ev_alien_hit, ev_player_hit, ev_ufo_hit, ev_march;
extern uint8_t march_note;

void game_init(void);
void game_init_wave(void);
void game_update(uint8_t left, uint8_t right, uint8_t fire_pressed);
uint16_t game_rand(void);

/* port (speed): column and row offsets from tables (cc65 multiplies by
   26 with a general 16-bit multiply) */
extern const uint8_t col_x[ALIEN_COLS + 1];     /* c * ALIEN_CELL_W */
extern const uint8_t row_y[ALIEN_ROWS];         /* r * ALIEN_CELL_H */
#define ALIEN_X(r, c) (grid_x + col_x[c] + ALIEN_ROW_SHIFT(r))
#define ALIEN_Y(r)    (grid_y + row_y[r])
#endif
