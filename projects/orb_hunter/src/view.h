/* Orb Hunter - the Atari display (the original's draw.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>

void view_init(void);
void view_game(void);                   /* the game screen, the current room */
void view_room(void);                   /* draw the current room */
void view_frame(void);                  /* sprites, shots, sparks, lava, HUD */
void view_hide(void);
void view_hud_reset(void);
void view_message(const char *s);       /* in the HUD row (0: the HUD again) */
void view_tile_changed(uint8_t tx, uint8_t ty);
void view_title(const char (*names)[4], const uint32_t *scores);
void view_title_frame(uint8_t t);

#endif
