/* Emberkeep: the screen (view.c) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
extern uint8_t view_banks;                  /* the chamber's image in a 130XE bank */
void view_init(void);
void view_level(void);                      /* (re)draw the chamber's image, dark screen */
void view_frame(void);                      /* the lit square, the sprites, the HUD */
void view_hud_msg(const char *s);           /* a line in the HUD (0: the normal one) */
void view_title(const char (*names)[4], const uint32_t *scores, uint8_t blink);
void view_end(uint8_t win, uint32_t score, uint8_t blink);
void view_text(uint8_t row, const char *a);
void view_game_screen(void);
#endif
