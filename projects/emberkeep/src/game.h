/*
 * Emberkeep: the game's state and rules (the original's game.h / game.cpp).
 * port: C, 16-bit fields where the original has 32-bit LONGs (the score is
 * 32-bit), the enemies as arrays of fields.
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define TILE_W   16
#define TILE_H   16
#define GRID_W   20
#define GRID_H   15
#define PLAY_W   (GRID_W * TILE_W)          /* 320 */
#define PLAY_H   (GRID_H * TILE_H)          /* 240 */

enum { T_FLOOR, T_WALL, T_RUNE, T_PORTAL, T_OIL, T_HAZARD };

#define WALK_SPEED            2
#define LIGHT_RADIUS_PX      40
#define LANTERN_MAX        1000
#define LANTERN_DRAIN_FRAMES  4
#define OIL_REFILL          400
#define MAX_ENEMIES           6

enum { GM_TITLE, GM_PLAYING, GM_WIN, GM_LOSE, GM_LEVELWIN };

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_UP     0x04
#define INPUT_DOWN   0x08
#define INPUT_START  0x10

#define RUNE_SCORE   100
#define OIL_SCORE    25
#define LEVEL_SCORE  500

/* events of the last tick (for the screen and the sound) */
#define EV_RUNE    1
#define EV_OIL     2
#define EV_DIE     4
#define EV_PORTAL  8                        /* through the portal */
#define EV_LEVEL   16                       /* a chamber (re)loaded: redraw it */
#define EV_OPEN    32                       /* the last rune: the portal opens */

typedef struct {
    uint8_t mode, level_index, lives, runes_remaining, runes_total;
    uint32_t score;
    int16_t lantern;
    uint16_t state_timer;
    int16_t px, py;                          /* the player, top left */
    uint8_t facing, frame;                  /* 0 up, 1 right, 2 down, 3 left */
    int16_t spawn_x, spawn_y;
    uint16_t tick;
    uint8_t num_enemies;
    uint8_t events;
    uint8_t changed_row, changed_col;       /* the tile a pickup changed (0xFF: none) */
} GameState;

extern GameState gs;
extern uint8_t grid[GRID_H][GRID_W];
extern int16_t e_x[MAX_ENEMIES], e_y[MAX_ENEMIES], e_min[MAX_ENEMIES], e_max[MAX_ENEMIES];
extern int8_t e_dx[MAX_ENEMIES], e_dy[MAX_ENEMIES];
extern uint8_t e_axis[MAX_ENEMIES], e_colour[MAX_ENEMIES], e_alive[MAX_ENEMIES], e_frame[MAX_ENEMIES];

void game_init(void);
void game_load_level(uint8_t index);
void game_tick(uint8_t input);

/* levels.c */
#define NUM_LEVELS 4
extern const char *const level_name[NUM_LEVELS];
extern const char *const level_tiles[NUM_LEVELS];
typedef struct { uint8_t axis, colour; int8_t dx, dy; uint16_t x, y, min_pos, max_pos; } EnemySpec;
extern const EnemySpec *const level_enemies[NUM_LEVELS];
#endif
