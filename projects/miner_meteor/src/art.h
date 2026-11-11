/* made by tools/mkart.py */
#ifndef ART_H
#define ART_H
#include <stdint.h>
#define CH_TILES 64          /* the first tile character */
#define A_SOLID 0
#define A_HAZARD 1
#define A_KEY 2   /* inverse: COLPF3 */
#define A_EXIT_C 3
#define A_EXIT_O 4   /* inverse: COLPF3 */
#define A_CONV_L 5
#define A_CONV_R 6
#define A_COLLAPSE0 7
#define A_COLLAPSE1 8
#define A_COLLAPSE2 9
#define A_COLLAPSE3 10
#define A_COLLAPSE4 11
#define A_COLLAPSE5 12
#define A_COLLAPSE6 13
#define A_COLLAPSE7 14
#define A_COUNT 15
extern const uint8_t tile_chars[A_COUNT * 16];
extern const uint8_t belt_frames[16 * 16];      /* 8 frames left belt, 8 right: 16 bytes (2 chars) */
extern const uint8_t miner_gfx[4 * 16];          /* walk cycle, facing right */
extern const uint8_t guard_gfx[3 * 2 * 16];      /* colour 4, 5, 6 x 2 frames */
#endif
