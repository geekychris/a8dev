#ifndef SCORE_H
#define SCORE_H
#include <stdint.h>
void score_init(void);
void score_add(uint16_t points);
extern uint32_t score_current, score_hi;
extern uint8_t score_lives, score_level;
void score_lose_life(void);
void score_next_level(void);
#endif
