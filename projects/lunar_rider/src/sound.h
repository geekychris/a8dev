#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_shoot(void);
void snd_explode(void);
void snd_jump(void);
void snd_checkpoint(void);
void snd_death(void);
void snd_init(void);             /* POKEY: AUDCTL 0, all quiet */
void snd_music(uint8_t on);
void snd_silence(void);
void snd_tick(void);             /* once a frame */
#endif
