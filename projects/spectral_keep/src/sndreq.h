#ifndef SNDREQ_H
#define SNDREQ_H
#include <stdint.h>
extern uint16_t sfx_req;               /* a bit per SFX_* asked for since the front end looked */
extern uint8_t music_req, music_on_req; /* T_* (0xFF: no change), music on */
extern int16_t duck_req;
#endif
