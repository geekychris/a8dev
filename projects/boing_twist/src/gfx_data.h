/* made by tools/gen_data.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

#define TEX_W 64
#define TEX_H 32
#define TEX_ROW 96
#define FT_W 64
#define FT_H 16
#define TEX_A_ADDR 0xD800
#define FT_A_ADDR 0xB000
#define BUF_ADDR 0xA040
#define BUF_ROWS 64
#define BUF_STRIDE 40
#define SCREEN_ROWS 88
#define FLIPTAB_ADDR 0x4000

typedef struct { uint8_t bank; uint16_t entry; uint8_t top, bottom, radius; } View;
enum { V_PLAY, V_FLIP, V_TITLE, NVIEWS };
extern const View views[NVIEWS];
extern const int8_t view_shade[NVIEWS][BUF_ROWS];
typedef struct { const char *title; uint8_t level, room; uint16_t addr; uint8_t col[3]; } Pic;
#define NPICS 18   /* 0: the Boing ball, made by the game */
extern const Pic pics[NPICS];
typedef struct { const char *name; uint8_t wall, dark; } Room;
#define NROOMS 10
extern const Room rooms[NROOMS];
extern const uint8_t room_rows[NROOMS][SCREEN_ROWS];
/* the bands' rows down the play ball's middle, per grid (4, 6, 8 bands) */
extern const uint8_t band_top[3][8], band_bot[3][8];

#endif
