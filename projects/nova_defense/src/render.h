/* Nova Defense - the Atari display */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
void render_init(void);
void render_game_screen(void);
void render_title_screen(void);
void render_frame(void);             /* everything, once a frame while playing */
void render_swarm_all(void);         /* a new wave: draw every alien */
void render_alien_cell(uint8_t r, uint8_t c, uint8_t what);   /* 0 gone, 1 alien, 2 explosion */
void hud_print(uint8_t line, uint8_t x, const char *s);
void hud_num(uint8_t line, uint8_t x, uint32_t v, uint8_t digits);
void hud_clear(uint8_t line);
void render_message(const char *s);  /* in the comet's row (0: none) */
void title_print(uint8_t row, uint8_t col, const char *s);
void title_clear(void);
void title_anim(uint8_t frame);
extern uint8_t scroll_k;
#endif
