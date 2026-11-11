/* What the rules (bank 0) ask of the sound: requests the front end's
   sound.c plays (it lives at $4000-$7FFF, out of sight while bank 0 is
   mapped, so the rules can't call it). In main memory below $4000. */
#include "keep.h"
#include "sndreq.h"

uint16_t sfx_req;
uint8_t music_req = 0xFF, music_on_req = 1;
int16_t duck_req;

void snd_play(uint8_t sfx) { sfx_req |= (uint16_t)1 << sfx; }
void snd_music(uint8_t track) { music_req = track; }
void snd_duck(int16_t ticks) { duck_req = ticks; }
void snd_music_enable(uint8_t on) { music_on_req = on; }
