/* tools/gen_gfx.py's tables (src/gfx.c) */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#define FROWS 31                /* the floor's rows: ANTIC D, lines 1+2 .. 61+62 */
extern const uint8_t row_p8[], row_t8[], row_dlo[], row_dhi[], row_len[FROWS];
extern const uint16_t row_w16[FROWS];
#define BALL_R0 3
#define BALL_N 30
extern const uint16_t ball_ofs[BALL_N];
extern const uint8_t ball_h[BALL_N], ball_w[BALL_N], ball_c[BALL_N], ball_data[];
#define SKIM_S0 6
#define SKIM_N 35
extern const uint16_t skim_ofs[SKIM_N];
extern const uint8_t skim_h[SKIM_N], skim_w[SKIM_N], skim_g[SKIM_N], skim_data[];
#endif
