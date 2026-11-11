#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_events(uint8_t ev);    /* game.h's EV_* */
void snd_tick(uint8_t low);     /* once a frame; low: the lantern under a quarter */
void snd_quiet(void);
#endif
