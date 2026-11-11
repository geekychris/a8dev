#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { SFX_THUD, SFX_CLICK, SFX_FLIP, SFX_SOLVED, SFX_TICK, SFX_OFF };
void snd_play(uint8_t which);
void snd_music(uint8_t on);
#endif
