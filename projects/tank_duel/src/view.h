/* view.h - Tank Duel from the turret (view.c) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);
void view_draw(void);                   /* the view into the back buffer */
void view_gun(uint8_t kick);            /* the gun (players), kicked back after a shot */
void view_blast(int16_t x, int16_t z, uint8_t size);   /* an explosion at (x, z) (1/16 units) */
#endif
