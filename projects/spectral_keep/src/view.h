/* the screen's memory (view.c, draw.s, disp.s, sprites.s, spectral_keep.cfg) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#define LINES    208
#define LINE_LO  ((uint8_t *)0xA100)       /* the lines' addresses: $8010 + L * 40, */
#define LINE_HI  ((uint8_t *)0xA1D0)       /*   lines 204-207 at $A000 (4K boundaries) */
#define DL       ((uint8_t *)0xA400)
#define HUD      ((uint8_t *)0xA500)       /* 2 x 40, ANTIC 2 */
#define PM_TOP   24                        /* the player row of line 0 */
void view_init(void);
void view_room(void);
void view_sprites(uint8_t snap);
void view_hud(uint8_t force);
void view_step(void);
#endif
