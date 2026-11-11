/* tower.h - drawing the tower (tower.c, tower_row.s) and the panel (panel.c). */
#ifndef TOWER_H
#define TOWER_H
#include <stdint.h>
#include "st.h"

#define TW_ROWS    22               /* rings shown: 20 + 2 of the hidden ones */
#define TW_CX      56               /* the tower's centre (pixels) */
#define TW_HALF    44               /* half its width */
#define TW_STEPS   16               /* view positions a column */

/* cell codes in tw_disp */
#define TC_EMPTY   0                /* 1..8 pieces, 9 rubble */
#define TC_GHOST   10
#define TC_GUIDE   11

extern uint8_t tw_nseg, tw_x0[12], tw_x1[12], tw_col[12], tw_dim[12], tw_pat[16];
extern uint8_t tw_disp[TW_ROWS * 16];
extern void __fastcall__ tw_row_(uint8_t r);

void tower_init(void);
/* build tw_disp from the game (piece, ghost, drop guide) */
void tower_build(uint8_t ghost, uint8_t piece);
/* draw what differs in the back buffer; view = facing column * 16 + step.
   Returns the rings drawn. */
uint8_t tower_draw(uint16_t view);
void tower_invalidate(void);       /* both buffers: draw everything again */

/* panel.c */
void panel_init(void);
void panel_draw(uint8_t show_queue);   /* into the back buffer, when it changed */
void panel_invalidate(void);
extern uint8_t board_ver;
#endif
