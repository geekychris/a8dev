/* td_int.h - what td_core.c shares with td_ai.c */
#ifndef TD_INT_H
#define TD_INT_H
#include "td.h"

#define SHIELD_ARC 6370                 /* the bulwark's shield: 35 degrees each side of ahead */
extern const pos vmax[TK_COUNT];
extern const uint16_t turn[TK_COUNT];
#define iabs(a) ((a) < 0 ? -(a) : (a))
int16_t adiff(uint16_t a, uint16_t b);  /* a - b, the short way round */
uint8_t td_near(pos dx, pos dz, pos r);    /* |(dx, dz)| < r */
uint8_t td_beyond(pos dx, pos dz, pos r);  /* |(dx, dz)| > r */
void drive(Tank *t, uint16_t want, pos speed);
void lob(uint8_t tank);
void think(uint8_t k);                  /* an enemy's mind (td_ai.c) */
void event(uint8_t type, uint8_t a, int16_t b, pos x, pos z);

/* obs.s: the next obstacle from oq_from whose box widened by oq_extra
   units holds (oq_x, oq_z), or 0xFF; obs_base / obs_n: G.ob, G.nobs */
extern pos oq_x, oq_z;
extern uint8_t oq_extra, oq_from, obs_n;
extern const Obstacle *obs_base;
uint8_t obs_next(void);
extern uint8_t obs_grid[64][4];        /* per 256-unit cell: the obstacles that reach it, $FF ends */
#endif
