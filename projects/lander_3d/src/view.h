/* view.h - Lander 3D's picture (view.c): the ground, the pads, the sky,
   the lander, its flame and shadow (players). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#define V_LINES   44                    /* 3D lines (each shown twice) */
#define V_HORIZON 7                     /* the horizon's line */
void view_init(void);
void view_world(uint8_t world);         /* the world's colours */
void view_draw(void);                   /* the ground into the back buffer */
void view_sprites(uint8_t flame);       /* the lander, its flame, its shadow */
extern uint16_t view_cycles;            /* the last frame's drawing time (ticks) */
#endif
