/*
 * Bullion Dash - the Atari screen (replaces the original's render.c/gfx.c)
 *
 * The 28 x 16 grid is ANTIC 4 characters, one a tile (columns 6..33); the
 * runner and the guards are players and missiles placed at the original's
 * pixel positions scaled by 4/10 across and 8/14 down.
 */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

void render_init(void);
void render_game_screen(void);   /* show the play screen (after render_room) */
/* the play screen: the whole grid (editor = the editor's tile look) */
void render_room(uint8_t editor);
/* a tile changed (level.c's tile_set) */
void render_tile_changed(uint8_t x, uint8_t y);
/* the sprites from the game state; entities = 0 hides them (the death blink) */
void render_frame(uint8_t entities);
/* the status line (only what changed, unless force) */
void render_status(uint8_t force);
/* the two message lines under the grid (0 = blank) */
void render_msg(const char *l1, const char *l2);
void render_msg_num(uint8_t line, uint8_t col, uint32_t v, uint8_t digits);
void render_msg_text(uint8_t line, uint8_t col, const char *s);
/* the editor's cursor */
void render_cursor(uint8_t x, uint8_t y, uint8_t on);
/* the title screen's text (ANTIC 6/7 lines) */
void render_title(void);
void title_print(uint8_t row, uint8_t col, const char *s, uint8_t colour);
void title_clear(void);
extern uint8_t render_editor_look;
#endif
