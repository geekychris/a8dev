/*
 * Frank the Frog (Atari 8-bit) - game constants. Port of
 * amiga_mcp/examples/frank_the_frog/game.h. The rules run in the original's
 * 320-pixel coordinates; the Atari shows them at half width (160 colour
 * clocks, 20 columns of 8). Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SCREEN_W 320            /* the original's coordinates */
#define TILE_W 16
#define GRID_COLS (SCREEN_W / TILE_W)   /* 20 columns */
#define GRID_ROWS 13

#define ROW_HOME     0
#define ROW_RIVER_5  1
#define ROW_RIVER_4  2
#define ROW_RIVER_3  3
#define ROW_RIVER_2  4
#define ROW_RIVER_1  5
#define ROW_MEDIAN   6
#define ROW_ROAD_5   7
#define ROW_ROAD_4   8
#define ROW_ROAD_3   9
#define ROW_ROAD_2   10
#define ROW_ROAD_1   11
#define ROW_START    12

#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DYING    2
#define STATE_LEVEL    3
#define STATE_GAMEOVER 4

#define MAX_LANE_OBJECTS 3
#define NUM_HOMES 4
#define DIR_LEFT  (-1)
#define DIR_RIGHT  1

#endif
