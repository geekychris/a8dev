/* view.h - Block Breaker 3D on the screen (view.c) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>

void view_init(void);
void view_level(void);                  /* the level's colours and bricks */
void view_bricks(void);                 /* all the bricks again */
void view_cell(uint8_t i);              /* brick i changed */
/* the ball and the paddle (1/32 units) for the next two frames: halfway
   between the last two ticks [0], then at the tick [1] */
extern int16_t vf_bx[2], vf_by[2], vf_bz[2], vf_px[2], vf_py[2];
/* wait until the VBI has shown the first of the last two frames (the
   back buffer is then off the screen) */
void view_wait(void);
/* the moving things into the back buffer and the sprites' slots, then flip */
void view_frame(void);
extern uint8_t view_hide_ball;

#endif
