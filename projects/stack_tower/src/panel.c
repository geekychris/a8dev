/*
 * panel.c - the right-hand panel in the bitmap (x 112..159): the next
 * three pieces, the held one and the whole tower unrolled (16 columns x 20
 * rings, the original's minimap), each drawn in the bricks' patterns.
 * Labels in the ROM font. Drawn into the back buffer when it changed since
 * that buffer last showed it.
 */
#include <string.h>
#include "a3d.h"
#include "tower.h"

extern uint8_t a3d_ylo[96], a3d_yhi[96];
extern uint8_t a3d_bsel;
#pragma zpsym ("a3d_bsel")

#define PBX   28                    /* first byte of the panel */
#define MAPY  44                    /* the minimap's top line */

static uint8_t shown[2][24];        /* what each buffer shows: next 3, hold, board hash... */
static uint8_t board_shown[2];
uint8_t board_ver = 1;             /* bumped by game.c whenever the board changes */

static uint8_t *line(uint8_t y)
{
    return (uint8_t *)(((uint16_t)(a3d_yhi[y] | a3d_bsel) << 8) | a3d_ylo[y]);
}

/* colour c's bits for the 4 pixels of each bit pair of a font nibble */
static uint8_t expand(uint8_t nib, uint8_t c)
{
    uint8_t v = 0, i;
    for (i = 0; i < 4; ++i) {
        v <<= 2;
        if (nib & 8) v |= c;
        nib <<= 1;
    }
    return v;
}

/* ROM-font text at byte column bx, line y (8 lines), colour 1..3 */
static void text(uint8_t bx, uint8_t y, const char *s, uint8_t c)
{
    const uint8_t *f;
    uint8_t ch, r, *l;
    for (; *s; ++s, bx += 2) {
        ch = (uint8_t)*s;
        ch = ch < 0x20 ? ch + 0x40 : ch < 0x60 ? ch - 0x20 : ch;
        f = (const uint8_t *)(0xE000 + ch * 8);
        for (r = 0; r < 8; ++r) {
            l = line((uint8_t)(y + r));
            l[bx] = expand(f[r] >> 4, c);
            l[bx + 1] = expand(f[r] & 15, c);
        }
    }
}

/* a piece in a 3x3-cell box (cells 2 x 2) at pixel x, line y; t < 0: empty */
static void piece_box(uint8_t x, uint8_t y, int8_t t)
{
    uint8_t i, k, cx, cy;
    for (k = 0; k < 6; ++k) {
        a3d_pattern(0, 0);
        a3d_span((uint8_t)(y + k), x, (uint8_t)(x + 8));
    }
    if (t < 0) return;
    a3d_pattern(tw_pat[t + 1], tw_pat[t + 1]);
    for (i = 0; i < st_ncells[t]; ++i) {
        cx = (uint8_t)(x + 1 + 2 * (st_cells[t][i][0] + 1));
        cy = (uint8_t)(y + 2 * (1 - st_cells[t][i][1]));
        a3d_span(cy, cx, (uint8_t)(cx + 2));
        a3d_span((uint8_t)(cy + 1), cx, (uint8_t)(cx + 2));
    }
}

static void minimap(void)
{
    uint8_t r, c, *l, v, a, b;
    const uint8_t *row = game.board;
    for (r = 0; r < ST_ROWS; ++r, row += ST_COLS) {
        l = line((uint8_t)(MAPY + 2 * (ST_ROWS - 1 - r))) + PBX + 2;
        for (c = 0; c < ST_COLS; c += 2) {
            a = row[c] ? tw_pat[row[c]] : 0;
            b = row[c + 1] ? tw_pat[row[c + 1]] : 0;
            v = (a & 0xF0) | (b & 0x0F);
            *l++ = v;
        }
        /* a dark line under each ring keeps the rings apart */
        memset(line((uint8_t)(MAPY + 2 * (ST_ROWS - 1 - r) + 1)) + PBX + 2, 0, 8);
    }
}

void panel_invalidate(void)
{
    memset(shown, 0xFF, sizeof shown);
}

void panel_init(void)
{
    uint8_t b;
    for (b = 0; b < 2; ++b) {           /* the labels: into both buffers */
        text(PBX, 0, "NEXT", 2);
        text(PBX, 22, "HOLD", 2);
        a3d_show();
    }
    panel_invalidate();
}

void panel_draw(uint8_t show_queue)
{
    uint8_t q[3], i, *s = shown[a3d_back];
    if (show_queue) st_preview(3, q);
    else q[0] = q[1] = q[2] = 0xFF;
    for (i = 0; i < 3; ++i)
        if (s[i] != q[i]) {
            piece_box((uint8_t)(112 + 16 * i), 11, (int8_t)q[i]);
            s[i] = q[i];
        }
    if (s[3] != (uint8_t)game.hold) {
        piece_box(124, 33, game.hold);
        s[3] = (uint8_t)game.hold;
    }
    if (board_shown[a3d_back] != board_ver || s[4]) {
        minimap();
        board_shown[a3d_back] = board_ver;
        s[4] = 0;
    }
}
