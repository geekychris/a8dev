/* made by tools/mkart.py */
#ifndef ART_H
#define ART_H
#include <stdint.h>
#define CH_TILE 64              /* + tile id: in play */
#define CH_EDIT 74              /* + tile id: in the editor */
extern const uint8_t tile_chars[20 * 8];
extern const uint8_t frame_gfx[10 * 8];
#define FR_STAND 0
#define FR_RUN1 1
#define FR_RUN2 2
#define FR_CLIMB1 3
#define FR_CLIMB2 4
#define FR_HANG 5
#define FR_DIG 6
#define FR_FALL 7
#define FR_TRAPPED 8
#define FR_CURSOR 9
#endif
