/* game.c: Boing Twist's game shell */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "bt_core.h"

enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_SOLVED, SC_ENTRY };
extern BtPuzzle P;
extern uint8_t screen, busy, sel_band, sel_half, paused, peeking;
extern int16_t anim_move;
extern uint16_t secs, peeks, score, draws, draw_frames;
uint8_t pic_allowed(uint8_t i, uint8_t level);
void game_init(void);
void game_frame(uint8_t frames);
#endif
