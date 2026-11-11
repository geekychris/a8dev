/*
 * Warden Keep's rules: the original's game.h / game.cpp in C (one 8 x 8
 * room in isometric view, the north and west walls, barrels pushed one
 * tile, jewels taken by walking over them; all the jewels win the room).
 * Tile-locked steps, one per input edge (the auto-repeat is main.c's, as
 * in the original's main.cpp).
 *
 * port: four rooms (the original has one: its README lists more rooms as
 * follow-up work; room 1 is the original's), stone blocks inside rooms 3
 * and 4, a clock (the Spectrum port's: 60 seconds a room, running out ends
 * the game, 10 points a second left when a room is won), and a restart of
 * the room (OPTION: the original's barrels can be pushed into a block that
 * never moves again). gs.relaxed plays it as the original: no clock.
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define WORLD_W   8
#define WORLD_H   8
#define NUM_ROOMS 4
#define MAX_OBJ   16

enum { O_NONE, O_BLOCK, O_BARREL, O_JEWEL };
enum { GM_TITLE, GM_PLAYING, GM_CLEARED, GM_WIN, GM_OVER };

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_UP     0x04
#define INPUT_DOWN   0x08

#define ROOM_SECONDS 60
#define BONUS        10                 /* points a second left */

/* events (the sound, the log, the redraw) */
enum { EV_STEP = 1, EV_PUSH = 2, EV_JEWEL = 4, EV_CLEAR = 8, EV_TIMEUP = 16, EV_BUMP = 32, EV_SECOND = 64 };

/* the objects as arrays, in object_at's order (blocks, barrels, jewels) */
extern uint8_t obj_kind[MAX_OBJ], obj_x[MAX_OBJ], obj_y[MAX_OBJ], num_obj;

typedef struct {
    uint8_t mode, room, relaxed;
    uint16_t score, room_score;         /* room_score: at the room's start (OPTION); at most 3600 */
    uint8_t jewels_left, jewels_total;
    int8_t px, py;
    uint8_t facing;                     /* 0 N, 1 E, 2 S, 3 W */
    uint8_t seconds, sub, tps;          /* the clock: seconds left, ticks into it, ticks a second */
    uint16_t bonus;
    uint8_t events;
    uint8_t moved_obj, moved_ox, moved_oy;  /* the barrel pushed this tick (0xFF none), from */
    uint8_t taken_obj, taken_x, taken_y;    /* the jewel taken this tick (0xFF none) */
} GameState;
extern GameState gs;

extern const uint8_t *const room_data[NUM_ROOMS];
extern const char *const room_name[NUM_ROOMS];

void game_load_room(uint8_t room);
void game_tick(uint8_t input);          /* one 1/50 s (1/60 NTSC); input: a step this tick */
#endif
