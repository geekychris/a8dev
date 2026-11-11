#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_events(uint8_t ev);    /* game.h's EV_* of the steps since the last call */
void snd_tick(void);            /* once a frame */
void snd_quiet(void);
#endif
