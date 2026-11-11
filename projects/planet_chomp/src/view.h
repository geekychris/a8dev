/* view.c: the maze round the chomper, laid flat */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);
void view_frame(void);
void view_cell_changed(uint16_t c);
void view_level(void);
void view_text(uint8_t x, const char *s);
void view_clear_text(void);
uint8_t view_screen_dir(uint16_t cell, uint8_t exit);
int8_t view_exit_for(uint16_t cell, uint8_t screen_dir);
extern uint8_t view_hscrol, view_vscrol;
#endif
