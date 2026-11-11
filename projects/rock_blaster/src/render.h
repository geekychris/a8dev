/* Rock Blaster - the Atari display */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

#define SCAN_TOP 24             /* scan line of the world's line 0 */

void render_init(void);
void render_game_screen(void);  /* clear the bitmap, show the game */
void render_title_screen(void);
void render_world(void);        /* rocks, bullets, particles, ship: catch up */
void render_hide_ship(void);
void hud_print(uint8_t line, uint8_t x, const char *s);   /* line 0 top, 1 bottom */
void hud_clear(uint8_t line);
void hud_num(uint8_t line, uint8_t x, uint32_t v, uint8_t digits);
void title_print(uint8_t row, uint8_t col, const char *s);
void title_clear(void);
void title_ship(uint8_t angle);
extern uint8_t rocks_pending;   /* rocks not redrawn yet (time ran out) */
#endif
