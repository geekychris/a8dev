/*
 * a8gfx.h - display lists, graphics modes, colours, plotting and text.
 *
 * gfx_init(mode, text_rows) builds a display list for one ANTIC mode at the
 * fixed screen area $A000-$BFFF (sdk/cfg/a8.cfg), optionally with
 * `text_rows` lines of ANTIC 2 text (40 columns) below it, and shows it:
 *
 *   GFX_TEXT   ANTIC 2   40x24 text, 1 colour + luminance (OS GRAPHICS 0)
 *   GFX_TEXT4  ANTIC 4   40x24 4-colour characters (custom charset)
 *   GFX_160x96 ANTIC D   160x96,  4 colours, 40 bytes a line (GRAPHICS 7)
 *   GFX_160    ANTIC E   160x192, 4 colours, 40 bytes a line (GRAPHICS 15)
 *   GFX_320    ANTIC F   320x192, 1 colour + luminance (GRAPHICS 8)
 *
 * The bitmap starts at $A010 so the 4K boundary that ANTIC's counter cannot
 * cross falls exactly between two lines; the display list reloads the address
 * there. Bitmap lines are reduced by the text rows (8 scan lines each).
 * Colours: COLOR0..COLOR4 shadows (gfx_color). In 4-colour modes pixel value
 * 0 = COLOR4 (background), 1..3 = COLOR0..COLOR2. In GFX_320 / text the
 * pixels/characters take COLOR1's luminance on COLOR2.
 *
 * After gfx_init the OS screen editor is gone: don't use printf/puts/conio.
 */
#ifndef A8GFX_H
#define A8GFX_H
#include <stdint.h>
#include "a8hw.h"

#define GFX_TEXT    2
#define GFX_TEXT4   4
#define GFX_160x96  0x0D
#define GFX_160     0x0E
#define GFX_320     0x0F

extern void gfx_init(uint8_t mode, uint8_t text_rows);
extern void gfx_off(void);                 /* screen DMA off (fast loading), gfx_on() back */
extern void gfx_on(void);
extern void gfx_clear(void);               /* bitmap/char screen and text rows to 0 */

extern uint8_t gfx_mode;
extern uint8_t *gfx_screen;                /* first byte of the main area */
extern uint16_t gfx_width;                 /* pixels (or characters) */
extern uint8_t gfx_height;                 /* lines (or character rows) */
extern uint8_t gfx_bpl;                    /* bytes a line (40) */
extern uint8_t *gfx_text;                  /* text rows below the bitmap, or 0 */
extern uint8_t gfx_text_rows;
extern uint8_t *gfx_dlist;
extern uint8_t *gfx_line_addr[192];        /* start of each bitmap line */

#define gfx_color(reg, value)  (*(volatile uint8_t *)(0x2C4 + (reg)) = (value))   /* reg 0..4 */
#define gfx_border(value)      (COLOR4 = (value))

/* bitmap modes. c = 0..3 (0..1 in GFX_320). No clipping: keep inside. */
extern void gfx_plot(uint16_t x, uint8_t y, uint8_t c);
extern uint8_t gfx_point(uint16_t x, uint8_t y);
extern void gfx_hline(uint16_t x0, uint16_t x1, uint8_t y, uint8_t c);
extern void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c);   /* clipped */
extern void gfx_rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c);      /* filled */
/* byte-wide fill: from byte column bx (0..39), w bytes, h lines, byte value v */
extern void gfx_fill_bytes(uint8_t bx, uint8_t y, uint8_t w, uint8_t h, uint8_t v);

/* text: in GFX_TEXT/GFX_TEXT4 screens and the text rows. Strings are ASCII
   (converted to screen codes); bit 7 set = inverse. */
extern void text_print_at(uint8_t x, uint8_t y, const char *s);   /* main char screen */
extern void text_row_print(uint8_t x, uint8_t row, const char *s); /* text rows below */
extern void text_num_at(uint8_t x, uint8_t y, uint16_t v, uint8_t width); /* right-aligned, main */
extern void text_row_num(uint8_t x, uint8_t row, uint16_t v, uint8_t width);
extern uint8_t a8_screen_code(char c);
/* text drawn into a bitmap with the ROM font (8x8): GFX_320 at byte column
   bx, or 4-colour modes at byte column bx with 2 bytes a character (colour c) */
extern void gfx_print(uint8_t bx, uint8_t y, const char *s, uint8_t c);

#endif
