#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_frame(uint8_t thrust, uint8_t crash, uint8_t land, uint8_t low_fuel);
void snd_music(uint8_t on);
void snd_quiet(void);
#endif
