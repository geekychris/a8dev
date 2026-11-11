/*
 * Frank the Frog - lane objects (traffic + river), from
 * amiga_mcp/examples/frank_the_frog/lanes.c. Port changes are marked "port:";
 * drawing is in render.c (the lanes are scrolled by ANTIC).
 */
#include <string.h>
#include "lanes.h"

uint8_t lane_row[NUM_LANES];
int8_t  lane_dir[NUM_LANES];
uint8_t lane_speed[NUM_LANES];
uint8_t lane_kind[NUM_LANES];
uint8_t lane_river[NUM_LANES];
uint8_t lane_num[NUM_LANES];
uint8_t lane_width[NUM_LANES];
int16_t lane_period[NUM_LANES];
int16_t lane_shift[NUM_LANES];
int16_t lane_base[NUM_LANES * MAX_LANE_OBJECTS];
uint8_t row_lane[GRID_ROWS];

/* Home ponds: centre x of each pond on the top bank */
const int16_t home_cx[NUM_HOMES] = { 40, 120, 216, 280 };
uint8_t home_filled[NUM_HOMES];

static void setup_lane(uint8_t idx, uint8_t row, int8_t dir, uint8_t speed,
                       uint8_t kind, uint8_t is_river, uint8_t num, uint8_t obj_w, int16_t spacing)
{
    uint8_t i;

    lane_row[idx] = row;
    row_lane[row] = idx;
    lane_dir[idx] = dir;
    lane_speed[idx] = speed;
    lane_kind[idx] = kind;
    lane_river[idx] = is_river;
    lane_num[idx] = num;
    lane_width[idx] = obj_w;
    lane_period[idx] = SCREEN_W + 16 + obj_w;   /* port: the wrap distance */
    lane_shift[idx] = 0;
    /* port: on multiples of 8 (whole Atari characters at half width) */
    for (i = 0; i < num; i++) lane_base[idx * MAX_LANE_OBJECTS + i] = (int16_t)((i * spacing + 4) & ~7);
}

void lanes_init(uint8_t level)
{
    uint8_t base;

    memset(row_lane, 0xFF, sizeof(row_lane));
    base = level;
    if (base > 6) base = 6;

    /* Road lanes (port: the bus is 40 wide, was 36: five characters) */
    setup_lane(0, ROW_ROAD_1, DIR_RIGHT, 2 + base/2, KIND_CAR_RED,    0, 3, 16, 120);
    setup_lane(1, ROW_ROAD_2, DIR_LEFT,  3 + base/3, KIND_CAR_YELLOW, 0, 3, 16, 110);
    setup_lane(2, ROW_ROAD_3, DIR_RIGHT, 2 + base/2, KIND_CAR_BLUE,   0, 3, 16, 110);
    setup_lane(3, ROW_ROAD_4, DIR_LEFT,  2 + base/3, KIND_BUS,        0, 2, 40, 160);
    setup_lane(4, ROW_ROAD_5, DIR_RIGHT, 3 + base/2, KIND_CAR_RED,    0, 3, 16, 120);

    /* River lanes: logs and lily-pad rafts */
    setup_lane(5, ROW_RIVER_1, DIR_RIGHT, 2 + base/3, KIND_LOG,  1, 3, 48, 110);
    setup_lane(6, ROW_RIVER_2, DIR_LEFT,  1 + base/3, KIND_PADS, 1, 3, 32, 100);
    setup_lane(7, ROW_RIVER_3, DIR_RIGHT, 2 + base/2, KIND_LOG,  1, 2, 64, 170);
    setup_lane(8, ROW_RIVER_4, DIR_LEFT,  2 + base/3, KIND_PADS, 1, 3, 32, 110);
    setup_lane(9, ROW_RIVER_5, DIR_RIGHT, 2 + base/2, KIND_LOG,  1, 2, 48, 160);
}

/* lanes_tick() is in lanes_asm.s. The C it implements: */
#if 0
void lanes_tick(void)
{
    uint8_t i;
    for (i = 0; i < NUM_LANES; i++) {
        if (lane_dir[i] == DIR_RIGHT) {
            lane_shift[i] += lane_speed[i];
            if (lane_shift[i] >= lane_period[i]) lane_shift[i] -= lane_period[i];
        } else {
            lane_shift[i] -= lane_speed[i];
            if (lane_shift[i] < 0) lane_shift[i] += lane_period[i];
        }
    }
}
#endif

/* the object's x as the original keeps it: -width..335 going right,
   -16-width..319 going left */
int16_t lane_obj_x(uint8_t lane, uint8_t j)
{
    int16_t per = lane_period[lane];
    int16_t x = lane_base[lane * MAX_LANE_OBJECTS + j] + lane_shift[lane];
    if (x >= per) x -= per;
    if (x >= (lane_dir[lane] == DIR_RIGHT ? SCREEN_W + 16 : SCREEN_W)) x -= per;
    return x;
}

/* port (speed): the lane is looked up by row instead of searched for */
uint8_t lanes_check_car(int16_t px, uint8_t row)
{
    uint8_t i = row_lane[row], j, w;
    int16_t ox;

    if (i == 0xFF || lane_river[i]) return 0;
    w = lane_width[i];
    for (j = 0; j < lane_num[i]; j++) {
        ox = lane_obj_x(i, j);
        /* AABB overlap with small inset for forgiving collisions */
        if (px + TILE_W - 5 > ox + 2 && px + 5 < ox + w - 2) return 1;
    }
    return 0;
}

uint8_t lanes_check_river(int16_t px, uint8_t row, int8_t *ride_dx)
{
    uint8_t i, j, w;
    int16_t ox;

    *ride_dx = 0;
    if (row < ROW_RIVER_5 || row > ROW_RIVER_1) return 1;
    i = row_lane[row];
    w = lane_width[i];
    for (j = 0; j < lane_num[i]; j++) {
        ox = lane_obj_x(i, j);
        /* port: the original skipped objects partly off screen (it didn't
           draw them); ANTIC scrolls them in, so what you see holds you */
        if (px + TILE_W - 4 > ox && px + 4 < ox + w) {
            *ride_dx = lane_dir[i] == DIR_RIGHT ? (int8_t)lane_speed[i] : -(int8_t)lane_speed[i];
            return 1;
        }
    }
    return 0;
}

int8_t lanes_check_home(int16_t px)
{
    uint8_t slot;
    for (slot = 0; slot < NUM_HOMES; slot++) {
        int16_t cx = home_cx[slot];
        if (px + TILE_W / 2 >= cx - 16 && px + TILE_W / 2 <= cx + 16) {
            if (home_filled[slot]) return -1;
            return (int8_t)slot;
        }
    }
    return -1;
}

void lanes_fill_home(int8_t slot)
{
    if (slot >= 0 && slot < NUM_HOMES) home_filled[slot] = 1;
}

uint8_t lanes_all_home(void)
{
    uint8_t i;
    for (i = 0; i < NUM_HOMES; i++) if (!home_filled[i]) return 0;
    return 1;
}

void lanes_reset_home(void)
{
    memset(home_filled, 0, sizeof(home_filled));
}
