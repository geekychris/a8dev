/* Rotoball: the screen (view.c) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>

/* the screen area: two display lists (one shown, one being made), the
   text rows, a black row, then floor rows (floor.c) */
#define DL0        ((uint8_t *)0xA000)
#define DL1        ((uint8_t *)0xA200)
#define TEXT       ((uint8_t *)0xA400)     /* 40-byte ANTIC 2 rows */
#define TROWS      14
#define BLACK      ((uint8_t *)0xA630)     /* 40 x $FF: colour 3, black */
#define FLOOR_MEM  ((uint8_t *)0xA660)
#define TROW(n)    (TEXT + (n) * 40)

void floor_build(void);
void view_init(void);
void view_frame(void);             /* the picture for the game's state */
/* text: row 2.. at pane line (33..88) of pane p (0 top, 1 bottom); 0xFF ends */
void view_overlay(const uint8_t *top, const uint8_t *bottom);
void view_text(uint8_t row, uint8_t col, const char *s);
void view_text_clear(uint8_t row);
void view_hud_reset(void);
#endif
