/* Uranus Lander - the Atari display (the original's draw.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>

void view_init(void);                  /* font, display lists, DLIs, PMG */
void view_game(void);                  /* the game screen for gs's level */
void view_world(void);                 /* draw the level's terrain and pads */
void view_frame(void);                 /* the lander, stars, sparks, one HUD instrument */
void view_hud_reset(void);
void view_msg(const char *a, const char *b, uint8_t pf);   /* 0, 0: none */
void view_autopilot(uint8_t on);
void view_title(const char *const *names, const uint32_t *scores);
void view_title_frame(uint8_t t);

#endif
