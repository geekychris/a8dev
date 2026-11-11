#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_events(uint8_t ev);    /* once a frame: the events of its steps */
void snd_quiet(void);
#endif
