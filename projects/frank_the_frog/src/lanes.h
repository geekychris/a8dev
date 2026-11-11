/* Frank the Frog - lane objects (traffic + river). Port of lanes.h. */
#ifndef LANES_H
#define LANES_H
#include "game.h"

/* port: a lane is a circular track of `period` pixels (the original's
   screen + margins + object width): every object of the lane moves with
   `shift`, and an object leaving one side comes back on the other exactly a
   period later (the original reset it to -width, dropping 0..speed-1 pixels
   of overshoot). That makes each lane a fixed pattern that ANTIC scrolls.
   port (speed): the lanes are arrays, one entry a lane (lanes_tick is in
   lanes_asm.s). */
#define NUM_LANES 10
extern uint8_t lane_row[NUM_LANES];      /* screen row */
extern int8_t  lane_dir[NUM_LANES];      /* DIR_LEFT or DIR_RIGHT */
extern uint8_t lane_speed[NUM_LANES];    /* pixels a frame (the original's) */
extern uint8_t lane_kind[NUM_LANES];     /* KIND_* (drawing) */
extern uint8_t lane_river[NUM_LANES];
extern uint8_t lane_num[NUM_LANES];      /* objects */
extern uint8_t lane_width[NUM_LANES];    /* object width */
extern int16_t lane_period[NUM_LANES];   /* SCREEN_W + 16 + width */
extern int16_t lane_shift[NUM_LANES];    /* 0..period-1 */
extern int16_t lane_base[NUM_LANES * MAX_LANE_OBJECTS];   /* positions at shift 0 */
extern uint8_t row_lane[GRID_ROWS];      /* lane of a row, 0xFF = none */

enum { KIND_CAR_RED, KIND_CAR_YELLOW, KIND_CAR_BLUE, KIND_BUS, KIND_LOG, KIND_PADS };

extern uint8_t home_filled[NUM_HOMES];
extern const int16_t home_cx[NUM_HOMES];

void lanes_init(uint8_t level);
void lanes_tick(void);                   /* lanes_asm.s */
int16_t lane_obj_x(uint8_t lane, uint8_t j);
uint8_t lanes_check_car(int16_t px, uint8_t row);
uint8_t lanes_check_river(int16_t px, uint8_t row, int8_t *ride_dx);
int8_t lanes_check_home(int16_t px);
void lanes_fill_home(int8_t slot);
uint8_t lanes_all_home(void);
void lanes_reset_home(void);
#endif
