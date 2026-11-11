#ifndef VIEW_H
#define VIEW_H
#include "td.h"
#define V_LINES   80                   /* lines of 3D (2 scan lines each) */
#define V_HORIZON 34                   /* the horizon's line */
void view_init(void);
void view_draw(void);
void view_boom(pos x, pos z);
#endif
