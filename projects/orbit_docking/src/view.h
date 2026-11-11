/* view.h - Orbit Docking from the cockpit (view.c) */
#ifndef VIEW_H
#define VIEW_H
void view_init(void);
void view_draw(void);                   /* the stars, the station, markers, debris into the back buffer */
void view_instruments(void);            /* the dial, the axis box's dot */
#endif
