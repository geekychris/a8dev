/* made by tools/mkart.py */
#ifndef ART_H
#define ART_H
#include <stdint.h>
#define CH_TILES 64             /* tile t: CH_TILES + 2t, item k: CH_TILES + 2 * (16 + k) */
#define ART_ITEMS 16
extern const uint8_t tile_chars[416];
extern const uint8_t tile_inverse[26];
extern const uint8_t hero_gfx[12 * 28];   /* [character][frame][pose] as the Spectrum's gfx_player */
extern const uint8_t foe_gfx[8 * 16];    /* [type][frame] */
#endif
