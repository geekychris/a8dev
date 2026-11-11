#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_title(void);                   /* the tune (the title) */
void snd_events(uint8_t ev, uint8_t seconds);
void snd_quiet(void);
#endif
