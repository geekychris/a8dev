#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);
void view_game_screen(void);
void view_room(void);
void view_frame(void);
void view_hud_msg(const char *s);
void view_text_screen(uint8_t bar);
void view_text(uint8_t row, const char *a);
void fmt_score(uint16_t v, char *b);    /* 6 digits and a 0 */
#endif
