/* made by tools/gen_gfx.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

#define TROWS 48                  /* the table's character rows */
#define NFONTS 7
#define FONT_BYTES 7168
#define FLIP_CELLS 32             /* each flipper's cells */
#define FLIP_FRAMES 8
#define VIEW_KX_Q12 78    /* mode 4 pixels a P (1/16 mm), Q12 */
#define VIEW_KY_Q12 84    /* scan lines a P, Q12 */
#define VIEW_KZ_Q12 1024     /* scan lines a mm of height, Q12 */
#define VIEW_OX 2                 /* mode 4 pixel of x = 0 */
#define VIEW_OY 356               /* scan line of y = 0 */
extern const uint8_t row_col[TROWS][5];   /* COLBK, PF0..PF3 */
extern const uint8_t row_font[TROWS];     /* which font */
extern const uint8_t lamp_n[29];          /* each lamp's cells */
extern const uint16_t lamp_cell[29][4];   /* their offsets in the map */
extern const uint8_t drop_n[8];
extern const uint16_t drop_cell[8][6];
extern const uint8_t drop_up[8][6], drop_down[8][6];   /* the characters standing / down */
extern const uint16_t flip_cell[2][FLIP_CELLS];        /* the map offset of each flipper cell */

#endif
