/* Frank the Frog - score, lives, level (from score.c; the HUD is in render.c) */
#include "score.h"

uint32_t score_current, score_hi;
uint8_t score_lives = 3, score_level = 1;

void score_init(void)
{
    score_current = 0;
    score_lives = 3;
    score_level = 1;
}

void score_add(uint16_t points)
{
    score_current += points;
    if (score_current > score_hi) score_hi = score_current;
}

void score_lose_life(void)
{
    if (score_lives > 0) score_lives--;
}

void score_next_level(void)
{
    score_level++;
    score_add(1000);
}
