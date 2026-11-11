/* ball.c: the texture, the display list and colours, the shadow and markers */
#ifndef BALL_H
#define BALL_H
#include <stdint.h>
#include "bt_core.h"

extern uint8_t ball_room;
extern uint8_t screen_rows_ball_top;
void ball_picture(uint8_t pic, BtPuzzle *p);     /* grid tables, the picture, the tiles' signatures */
void ball_texture(const BtPuzzle *p, uint8_t ident);
void ball_texture_rows(const BtPuzzle *p, uint8_t v0, uint8_t v1, uint8_t ident);
void ball_twist_begin(uint8_t band);
void ball_twist_shift(int8_t texels);
uint8_t ball_seg_texels(void);
uint8_t ball_half_turn(uint8_t k);
void ball_dlist(uint8_t big, int8_t ball_row, int8_t dx, uint8_t ntext);
void ball_colours(uint8_t view, int8_t ball_row, uint8_t text_bg);
void ball_shadow(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void ball_shadow_off(void);
void ball_markers(uint8_t on, uint8_t top, uint8_t bot, uint8_t xl, uint8_t xr);
void ball_clear_buf(void);
#endif
