/* board.h - the board and the marble on the screen (board.c). */
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>
uint8_t board_view(void);               /* the tilt's view (0..8) */
uint8_t board_step(void);               /* a slice of a new board; 1 when one was just shown */
void board_invalidate(void);
void marble_init(void);
void marble_draw(void);
#endif
