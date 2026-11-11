/* a8game.h: frame timer on the OS vertical blank (RTCLOK2). */
#include "a8game.h"

uint16_t frame_count;
uint16_t frame_overruns;
void (*a8_frame_sound)(void);          /* set by sound_init(): sound_tick */
static uint8_t last;

void frame_init(void)
{
    last = RTCLOK2;
    frame_count = 0;
    frame_overruns = 0;
    ATRACT = 0;
}

uint8_t frame_wait(void)
{
    uint8_t n, i;
    while ((uint8_t)(RTCLOK2 - last) == 0)
        ;
    n = (uint8_t)(RTCLOK2 - last);
    last += n;
    frame_count += n;
    if (n > 1) frame_overruns += n - 1;
    ATRACT = 0;
    if (a8_frame_sound)
        for (i = n; i; --i) a8_frame_sound();
    return n;
}
