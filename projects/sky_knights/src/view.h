#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);
void view_game(void);
void view_frame(void);
void view_hud_reset(void);
void view_clear_objects(void);
void view_msg(const char *a, const char *b);
void view_title(const char (*names)[4], const uint32_t *scores, uint8_t sel);
void view_title_frame(uint8_t t);
#endif
