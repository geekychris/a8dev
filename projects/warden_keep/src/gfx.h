/* tools/gen_gfx.py's shapes (src/gfx.c) */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>
typedef struct { int8_t top; uint8_t h; const uint8_t *dm, *m1; } Shape;   /* dm: mask, data x 4 a line; m1: 1-bit mask, 2 a line */
enum { SH_FLOOR, SH_DOOR, SH_WALL, SH_BARREL, SH_JEWEL, SH_BLOCK, SH_COUNT };
extern const Shape shapes[SH_COUNT];
#define PL_H 36
#define PL_TOP (-30)
extern const uint8_t player_pm[4][4][PL_H];
extern const uint8_t door_missiles[13];      /* lines -6..6 of the door tile */
#endif
