/*
 * tower.c - the tower: a 16-sided prism seen from the front, its rings
 * drawn by tower_row.s. The camera is the original's (640 from the axis,
 * the core radius 140) with the vertical perspective left out (port: rings
 * are horizontal bands of 4 lines); each column's edges come from the
 * horizontal perspective, so the side columns narrow towards the limbs.
 * The view turns in sixteenths of a column (port: the original turns
 * smoothly).
 *
 * Each buffer remembers what it shows (the cell codes and the view), so a
 * frame redraws only the rings that changed: a falling piece costs a few
 * rings, a turn the whole tower (~1.3 frames).
 */
#include <string.h>
#include "a3d.h"
#include "tower.h"

#define R_CORE   140
#define CAM_D    640
#define FOCAL    196

uint8_t tw_shown_view[2] = { 0xFF, 0xFF };
static uint8_t tw_same;              /* tw_disp didn't change this frame */
static uint8_t prev[TW_ROWS * 16];      /* the cells last frame */
static uint8_t dirty[2][TW_ROWS];       /* rings each buffer still has to redraw */

#include "tower_geom.h"           /* per view step: the segments (tools/gen_tower.py) */

void tower_init(void)
{
    /* patterns: c1 (COLOR0) red, c2 (COLOR1) gold, c3 (COLOR2) blue,
       0 = the core (players behind the playfield) */
    static const uint8_t pats[12] = { 0, 0x55, 0xAA, 0xFF, 0x66, 0xBB, 0x77, 0x45, 0xCF, 0x88, 0x20, 0x00 };
    memcpy(tw_pat, pats, sizeof pats);
}

void tower_invalidate(void) { memset(dirty, 1, sizeof dirty); }

void tower_build(uint8_t ghost, uint8_t piece)
{
    static Piece key_p;
    static uint8_t key_v, key_f = 0xFF;
    int8_t cells[5][2];
    uint8_t n, i, r;
    Piece p;
    uint8_t f = (uint8_t)(ghost | (piece << 1) | (game.over << 2));
    /* nothing changed: tw_disp is still right */
    if (f == key_f && key_v == board_ver && !memcmp(&key_p, &game.piece, sizeof key_p)) {
        tw_same = 1;
        return;
    }
    tw_same = 0;
    key_f = f;
    key_v = board_ver;
    key_p = game.piece;
    memcpy(tw_disp, game.board, TW_ROWS * 16);
    if (!piece || game.over) return;
    if (ghost) {
        static Piece gp_key;
        static uint8_t g_ver, g_row;
        if (memcmp(&gp_key, &game.piece, sizeof gp_key) || g_ver != board_ver) {
            gp_key = game.piece;
            g_ver = board_ver;
            g_row = (uint8_t)st_ghost_row();      /* cached: ~20 collision tests */
        }
        p = game.piece;
        p.row = (int8_t)g_row;
        n = st_cells_of(&p, cells);
        for (i = 0; i < n; ++i) {
            r = (uint8_t)cells[i][1];
            if (r < TW_ROWS && !tw_disp[r * 16 + cells[i][0]]) tw_disp[r * 16 + cells[i][0]] = TC_GHOST;
        }
    }
    n = st_cells_of(&game.piece, cells);
    for (i = 0; i < n; ++i) {
        r = (uint8_t)cells[i][1];
        if (r < TW_ROWS) tw_disp[r * 16 + cells[i][0]] = (uint8_t)(game.piece.type + 1);
    }
}

uint8_t tower_draw(uint16_t view)
{
    uint8_t b = a3d_back, step = (uint8_t)view & (TW_STEPS - 1), col = (uint8_t)(view >> 4) & 15;
    uint8_t r, i, drawn = 0, any = 0;
    uint8_t *pp = prev, *dp = tw_disp, *d = dirty[b];

    /* rings that changed since last frame: both buffers must redraw them */
    if (!tw_same) for (r = 0; r < TW_ROWS; ++r, pp += 16, dp += 16)
        if (memcmp(pp, dp, 16)) {
            memcpy(pp, dp, 16);
            dirty[0][r] = dirty[1][r] = 1;
        }
    if (tw_shown_view[b] != (uint8_t)view) memset(d, 1, TW_ROWS);
    for (r = 0; r < TW_ROWS; ++r) any |= d[r];
    if (!any) return 0;
    tw_nseg = seg_n[step];
    for (i = 0; i < tw_nseg; ++i) {
        tw_col[i] = (uint8_t)(col + seg_rel[step][i]) & 15;
        tw_x0[i] = seg_a[step][i];
        tw_x1[i] = seg_b[step][i];
        tw_dim[i] = seg_d[step][i];
    }
    for (r = 0; r < TW_ROWS; ++r) {
        if (d[r]) {
            d[r] = 0;
            tw_row_(r);
            ++drawn;
            if (r == ST_ROWS) {             /* the red line under the hidden rings */
                a3d_pattern(0x55, 0x55);
                a3d_span(84 - 4 * ST_ROWS, TW_CX - TW_HALF, TW_CX + TW_HALF);
            }
        }
    }
    tw_shown_view[b] = (uint8_t)view;
    return drawn;
}
