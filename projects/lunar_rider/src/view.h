/* Lunar Rider - the Atari display (the original's draw.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>

void view_init(void);                 /* font, bands, display lists, PMG, DLIs */
void view_title(void);                /* the title display list (bands + title text) */
void view_game(void);                 /* the game display list, ground drawn for gs */
void view_scroll(uint32_t px, uint32_t gpx); /* mountains for world px, ground for gpx (applied in the VBI) */
void view_ground(void);               /* draw the ground columns coming into view + dirty ones */
void view_sprites(void);              /* buggy, enemies, explosions, bullets, bombs */
void view_hide_sprites(void);
void view_hud(void);                  /* score / zone / hi / lives / progress, when changed */
void view_hud_reset(void);            /* force the HUD to redraw */
extern uint32_t view_hiscore;         /* the HI shown */

/* text in the sky rows (0..4) of the game screen; centred; 0 = clear the row */
void sky_msg(uint8_t row, const char *s, uint8_t pf);   /* ANTIC 6: 20 columns, colour 0..3 */
void sky_clear(void);
/* title screen text: rows 0..3 (ANTIC 2, 40 columns) */
void title_row(uint8_t row, const char *s);
/* bottom rows (ANTIC 2, 40 columns): 0 = progress bar, 1 = message */
void bottom_row(uint8_t row, const char *s);
void bar_draw(uint8_t px);            /* progress bar 0..80 pixels */
void title_buggy(uint8_t x);          /* the buggy on the title screen's ground */

#endif
