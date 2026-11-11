/* Frank the Frog - Atari display: lanes scrolled by ANTIC, Frank on players */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

#define SCAN_TOP 24              /* first scan line of the game rows */

void render_init(void);          /* charset, display lists, interrupts */
void render_game_screen(void);   /* show the game screen */
void render_title_screen(void);  /* show the title screen */
void render_lanes_build(void);   /* draw every lane's track (new level) */
void render_scroll(void);        /* lanes' scroll positions for the next frame */
void render_home(void);          /* ponds, filled or not */
void render_frank(uint8_t state);
void render_hide_frank(void);
void render_hud(uint32_t score, uint32_t hi, uint8_t lives, uint8_t level);
void render_message(const char *s);         /* the median's text line, centred */
void render_start_message(const char *s);   /* the start strip's */
void title_print(uint8_t row, uint8_t col, const char *s);   /* ANTIC 2 rows 0..15 */
void title_clear_row(uint8_t row);
void title_face(uint8_t blink);
extern uint8_t scroll_k;
extern uint8_t pend_ready;     /* 1: the VBI takes the new positions */
#endif
