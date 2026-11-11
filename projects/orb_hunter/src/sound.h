#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_music(uint8_t on);
void snd_quiet(void);
void snd_tick(void);
#endif
