#ifndef AUTOPILOT_H
#define AUTOPILOT_H
#include "game.h"
void autopilot_reset(void);
void autopilot_fly(InputState *in);   /* sets in->left/right/thrust */
#endif
