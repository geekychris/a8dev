/*
 * view.c - Block Breaker 3D on the screen.
 *
 * The camera is fixed (port: the original leans it a fifth of the paddle's
 * offset, which moves every pixel), so the tunnel never changes. From the
 * camera every line of it is the side walls' row (the same on every line)
 * with one run of floor, ceiling or back wall in the middle
 * (tools/gen_view.py), so any part of it is redrawn from 80 bytes and three
 * a line. The bricks are drawn into a clean box (13 bytes x 31 lines) as
 * rectangles (every front face is one; the sides are 1-2 pixel strips),
 * only when one changes; the box is then copied into each buffer.
 *
 * What moves is hardware: the paddle's frame (players 0 and 1, double or
 * quadruple width), the ball (player 2), a capsule (player 3: with two or
 * more they take turns), the ring's sides on the walls (missiles 0 and 1
 * in the fifth player's colour). The ring's floor and ceiling lines and the
 * ball's shadow are spans drawn into the back buffer each frame and wiped
 * (the tunnel's line, the clean box) the next time that buffer is drawn.
 *
 * Colours: COLBK and COLPF0 the tunnel's two purples (and steel bricks);
 * on each brick row's band of lines a DLI sets COLPF1/COLPF2 to that row's
 * colours for the level (gen_view.py chooses them, and a pattern for each
 * kind of brick: a colour or a dither of two).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a3d.h"
#include "a3d_int.h"
#include "bb.h"
#include "view.h"
#include "view_tab.h"

extern void bb_dli(void);
extern uint8_t dli_c1[6], dli_c2[6], dli_n;
extern uint8_t fr_x0, fr_x1, fr_y0, fr_y1, fr_pe, fr_po, fr_tgt, cr_b0, cr_b1;
extern void fill_rect(void);
extern void copy_rect(void);

#define LINES 88
#define BOXW  (BOX_B1 - BOX_B0 + 1)

#pragma bss-name ("LOWRAM")

static uint8_t cbox[(BOX_Y1 - BOX_Y0) * BOXW];   /* the bricks and the tunnel behind them */
uint8_t cl_lo[BOX_Y1], cl_hi[BOX_Y1];           /* line y of the clean box, less BOX_B0 */
/* by (z + 320 - 256) / 4 (z in units, -64..800): pixels and scan lines a unit, * 256 */
uint8_t kx_tab[217], ky_tab[217];
/* last frame's spans in each buffer: line, first byte, last byte */
#define MAXSP 12
static uint8_t sp_y[2 * MAXSP], sp_b0[2 * MAXSP], sp_b1[2 * MAXSP], sp_n[2];
static uint8_t cap_turn;
uint8_t view_hide_ball;


/* ------------------------------------------------------------ the clean picture */

/* the tunnel on line y, bytes b0..b1, into the target (fr_tgt) (fast.s) */
extern uint8_t tl_y, tl_b0, tl_b1;
extern void tun_line_(void);
#define tun_line(y, b0, b1) (tl_y = (y), tl_b0 = (b0), tl_b1 = (b1), tun_line_())

/* line y, bytes b0..b1 of the back buffer as they are without what moves */
static void restore(uint8_t y, uint8_t b0, uint8_t b1)
{
    fr_tgt = 1;
    tun_line(y, b0, b1);
    if (y >= BOX_Y0 && y < BOX_Y1 && b1 >= BOX_B0 && b0 <= BOX_B1) {
        fr_y0 = y; fr_y1 = y + 1;
        cr_b0 = b0 < BOX_B0 ? BOX_B0 : b0;
        cr_b1 = b1 > BOX_B1 ? BOX_B1 : b1;
        copy_rect();
    }
}

extern uint8_t fr_cx0, fr_cx1, fr_cy0, fr_cy1;
#define CLIP(x0, x1, y0, y1) (fr_cx0 = (x0), fr_cx1 = (x1), fr_cy0 = (y0), fr_cy1 = (y1))
#define NOCLIP() CLIP(0, 160, 0, LINES)
#define RECT(x0, x1, y0, y1, pe, po) (fr_x0 = (x0), fr_x1 = (x1), fr_y0 = (y0), fr_y1 = (y1), fr_pe = (pe), fr_po = (po), fill_rect())

/* the box's lines still to copy into each buffer (b0 > b1: none) */
static uint8_t pb_y0[2], pb_y1[2], pb_b0[2] = { 0xFF, 0xFF }, pb_b1[2];

static void pend(uint8_t y0, uint8_t y1, uint8_t b0, uint8_t b1)
{
    uint8_t k;
    for (k = 0; k < 2; ++k) {
        if (pb_b0[k] > pb_b1[k]) { pb_y0[k] = y0; pb_y1[k] = y1; pb_b0[k] = b0; pb_b1[k] = b1; continue; }
        if (y0 < pb_y0[k]) pb_y0[k] = y0;
        if (y1 > pb_y1[k]) pb_y1[k] = y1;
        if (b0 < pb_b0[k]) pb_b0[k] = b0;
        if (b1 > pb_b1[k]) pb_b1[k] = b1;
    }
}

/* the bricks in columns c0..c1, rows r0..r1 (all layers, far to near) into
   the clean box, clipped to the window (fast.s) */
extern uint8_t dc_c0, dc_c1, dc_r0, dc_r1;
extern const void *dc_cell, *dc_hits, *dc_pat;
extern void draw_cells_(void);
static void draw_cells(uint8_t c0, uint8_t c1, uint8_t r0, uint8_t r1)
{
    dc_c0 = c0; dc_c1 = c1; dc_r0 = r0; dc_r1 = r1;
    dc_cell = G.cell; dc_hits = G.hits; dc_pat = lv_pat[G.level - 1];
    draw_cells_();
}

/* the tunnel behind the bricks in the window (byte-aligned) */
static void clear_window(void)
{
    uint8_t y, b0 = fr_cx0 >> 2, b1 = (fr_cx1 - 1) >> 2;
    fr_tgt = 0;
    for (y = fr_cy0; y < fr_cy1; ++y) tun_line(y, b0, b1);
}

void view_bricks(void)
{
    CLIP(BOX_B0 * 4, BOX_B1 * 4 + 4, BOX_Y0, BOX_Y1);
    clear_window();
    draw_cells(0, BB_COLS - 1, 0, BB_ROWS - 1);
    NOCLIP();
    pend(BOX_Y0, BOX_Y1, BOX_B0, BOX_B1);
    bb_dirty = 0;
}

/* brick i changed: the screen round its column and row at every depth */
void view_cell(uint8_t i)
{
    uint8_t c = i, r = 0, k, x0 = 160, x1 = 0, y0 = LINES, y1 = 0, v;
    while (c >= 24) c -= 24;
    while (c >= BB_COLS) { c -= BB_COLS; ++r; }
    for (k = 0; k < 4; ++k) {
        v = bk_sx[k][c]; if (v < x0) x0 = v;
        v = bk_sx[k][c + 1]; if (v > x1) x1 = v;
        v = bk_sy[k][r + 1]; if (v < y0) y0 = v;
        v = bk_sy[k][r]; if (v > y1) y1 = v;
    }
    x0 &= 0xFC;
    x1 = (x1 + 3) & 0xFC;
    CLIP(x0, x1, y0, y1);
    clear_window();
    draw_cells(c ? c - 1 : 0, c < BB_COLS - 1 ? c + 1 : c, r ? r - 1 : 0, r < BB_ROWS - 1 ? r + 1 : r);
    NOCLIP();
    pend(y0, y1, x0 >> 2, (x1 - 1) >> 2);
}

void view_level(void)
{
    uint8_t r;
    for (r = 0; r < 4; ++r) {
        dli_c1[3 - r] = lv_pf1[G.level - 1][r];
        dli_c2[3 - r] = lv_pf2[G.level - 1][r];
    }
    view_bricks();
}

void view_init(void)
{
    uint8_t y, *buf;
    uint16_t zz;
    a3d_init(LINES, 2);
    NOCLIP();
    for (y = 0, zz = 256; y < 217; ++y, zz += 4) {
        kx_tab[y] = (uint8_t)((KX_NUM + (zz >> 1)) / zz);
        ky_tab[y] = (uint8_t)((KY_NUM + (zz >> 1)) / zz);
    }
    COLOR4 = C_BAK;
    COLOR0 = C_PF0;
    COLOR1 = C_OUT1;
    COLOR2 = C_OUT2;
    COLOR3 = 0xBA;                      /* the ring's sides (missiles, as the fifth player) */
    dli_c1[4] = C_OUT1; dli_c2[4] = C_OUT2;
    dli_c1[5] = 0x0E; dli_c2[5] = 0x02; /* the text rows */
    /* DLIs: the line before each band, the end of the bands, the last line */
    a3d_dl_flag(band_top[3] - 1, 0x80);
    a3d_dl_flag(band_top[2] - 1, 0x80);
    a3d_dl_flag(band_top[1] - 1, 0x80);
    a3d_dl_flag(band_top[0] - 1, 0x80);
    a3d_dl_flag(BAND_END - 1, 0x80);
    dli_n = 0;
    VDSLST = (uint16_t)bb_dli;
    /* the clean box's lines */
    for (y = BOX_Y0; y < BOX_Y1; ++y) {
        uint16_t p = (uint16_t)(cbox + (y - BOX_Y0) * BOXW - BOX_B0);
        cl_lo[y] = (uint8_t)p; cl_hi[y] = (uint8_t)(p >> 8);
    }
    /* the tunnel into the back buffer, then the other */
    fr_tgt = 1;
    for (y = 0; y < LINES; ++y) tun_line(y, 0, 39);
    buf = (uint8_t *)(a3d_back ? 0xB010 : 0xA010);
    memcpy((void *)(a3d_back ? 0xA010 : 0xB010), buf, LINES * 40);
    /* players: the paddle 0 + 1, the ball 2, a capsule 3; missiles 0, 1 the ring */
    pmg_init();
    pmg_color(0, 0x9C);
    pmg_color(1, 0x9C);
    pmg_color(2, 0x0E);
    pmg_color(3, 0x5C);
    GPRIOR = 0x11;                      /* players over the playfield; missiles in COLOR3 */
}

/* ------------------------------------------------------------ each frame */
/* a point (1/32 units) on the screen: pj_sx pixels, pj_sy scan lines, pj_k (fast.s) */
extern int16_t pj_x, pj_y, pj_z;
extern uint8_t pj_k, pj_sx, pj_sy;
extern void proj(void);
#define PROJ(x, y, z) (pj_x = (x), pj_y = (y), pj_z = (z), proj())

static void span(uint8_t y, uint8_t x0, uint8_t x1, uint8_t pat)
{
    uint8_t b = a3d_back, n = sp_n[b], i = b ? MAXSP + n : n;
    if (y >= LINES || x0 >= x1 || n >= MAXSP) return;
    fr_x0 = x0; fr_x1 = x1; fr_y0 = y; fr_y1 = y + 1; fr_pe = fr_po = pat;
    fill_rect();
    sp_y[i] = y; sp_b0[i] = x0 >> 2; sp_b1[i] = (x1 - 1) >> 2;
    sp_n[b] = n + 1;
}

/* the sprites (sprites.s): slot s = the frame after the flip (0) and the one after (1) */
extern uint8_t spr_pend, sl_x[8], sl_y[8], sl_h[8], sl_lo[8], sl_hi[8], sl_size[8], sl_col[8];
extern uint8_t sl_mx0, sl_mx1, sl_my0, sl_my1;
static uint8_t st_x[8], st_y[8], st_h[8], st_lo[8], st_hi[8], st_size[8], st_col[8];
static uint8_t st_mx0, st_mx1, st_my0, st_my1;
int16_t vf_bx[2], vf_by[2], vf_bz[2], vf_px[2], vf_py[2];

static void shape(uint8_t i, uint8_t y, const uint8_t *rows, uint8_t h)
{
    st_y[i] = y; st_h[i] = h;
    st_lo[i] = (uint8_t)(uint16_t)rows; st_hi[i] = (uint8_t)((uint16_t)rows >> 8);
}

static void slot(uint8_t s)
{
    uint8_t i = s << 2, x, y, h, n, k, wide, col;
    int16_t bz = vf_bz[s];

    /* the paddle: players 0 and 1 */
    PROJ(vf_px[s], vf_py[s], 0);
    x = pj_sx;
    y = pj_sy;
    wide = G.wide_t != 0;
    h = wide ? WIDE_H : PAD_H;
    y -= h >> 1;
    col = G.plough_t && (G.tick & 2) ? 0x26 : 0x9C;
    shape(i, y, wide ? wide_l : pad_l, h);
    shape(i + 1, y, wide ? wide_r : pad_r, h);
    st_size[i] = st_size[i + 1] = wide ? 3 : 1;
    st_x[i] = 48 + x - (wide ? 28 : 16);
    st_x[i + 1] = 48 + x;
    st_col[i] = st_col[i + 1] = col;

    /* the ball: player 2 */
    if (view_hide_ball) shape(i + 2, 0, 0, 0);
    else {
        PROJ(vf_bx[s], vf_by[s], bz);
        n = ball_of[pj_k >> 3];                         /* (z + 320) / 32 - 8 */
        h = ball_h[n];
        x = pj_sx;
        shape(i + 2, pj_sy - (h >> 1), ball_rows + ball_off[n], h);
        st_size[i + 2] = ball_wide[n];
        st_x[i + 2] = 48 + x - (ball_wide[n] ? 8 : 4);
        st_col[i + 2] = G.plough_t ? 0x26 : 0x0E;
    }

    /* a capsule: player 3; with more than one, they take turns */
    for (k = 0, n = 0xFF; k < BB_MAX_CAPS; ++k) {
        x = (cap_turn + k) & 3;
        if (G.cactive[x]) { n = x; break; }
    }
    ++cap_turn;
    if (n == 0xFF || G.between) shape(i + 3, 0, 0, 0);
    else {
        bz = G.cz[n] + (s ? 0 : 7 * BB_FX / 2);         /* halfway: it moves 7 a tick */
        PROJ(G.cx[n], G.cy[n], bz);
        k = cap_of[pj_k >> 3][G.ctype[n]];
        h = cap_h[k];
        shape(i + 3, pj_sy - (h >> 1), cap_rows + cap_off[k], h);
        st_size[i + 3] = cap_wide[k];
        st_x[i + 3] = 48 + pj_sx - (cap_w[k] >> 1);
        st_col[i + 3] = 0x5C;
    }
}

void view_wait(void)
{
    while (spr_pend == 1)
        ;
}

void view_frame(void)
{
    uint8_t b, kb, x, i, n, y0, y1, hw;

    b = a3d_back;
    /* wipe the spans this buffer had; the bricks' box if it changed */
    fr_tgt = 1;
    for (i = b ? MAXSP : 0, n = i + sp_n[b]; i < n; ++i) restore(sp_y[i], sp_b0[i], sp_b1[i]);
    sp_n[b] = 0;
    if (pb_b0[b] <= pb_b1[b]) {
        fr_y0 = pb_y0[b]; fr_y1 = pb_y1[b]; cr_b0 = pb_b0[b]; cr_b1 = pb_b1[b];
        copy_rect();
        pb_b0[b] = 0xFF; pb_b1[b] = 0;
    }
    slot(0);
    slot(1);

    /* the ring round the walls at the ball's depth (shown with slot 0): its
       sides are missiles, the floor and ceiling lines spans; the shadow */
    if (view_hide_ball) st_my0 = st_my1 = 0;
    else {
        PROJ(0, -118 * BB_FX, vf_bz[0] - 9 * BB_FX);   /* the floor under the ball's near and far edges */
        y0 = (pj_sy - 32) >> 1;
        PROJ(0, -118 * BB_FX, vf_bz[0] + 9 * BB_FX);
        y1 = (pj_sy - 32) >> 1;
        kb = pj_k - 2;
        hw = (uint8_t)((168u * kx_tab[kb]) >> 8);
        n = (uint8_t)((120u * ky_tab[kb]) >> 8);        /* scan lines from the middle */
        st_my0 = 120 - n;
        st_my1 = 120 + n;
        st_mx0 = 48 + 80 - hw;
        st_mx1 = 48 + 80 + hw - 1;
        x = (st_my0 - 32) >> 1;
        if (x < band_top[3]) span(x, 80 - hw, 80 + hw, 0xAA);
        x = (st_my1 - 32) >> 1;
        if (x >= BAND_END) span(x, 80 - hw, 80 + hw, 0xAA);
        /* the shadow on the floor: 18 units across, from z - 9 to z + 9 */
        x = st_x[2] + (st_size[2] ? 8 : 4) - 48;
        hw = (uint8_t)((9u * kx_tab[kb]) >> 8) + 1;
        for (; y1 <= y0; ++y1)
            if (y1 >= BAND_END) span(y1, x - hw, x + hw, y1 & 1 ? 0x33 : 0xCC);
    }

    /* hand the slots to the VBI once it has shown the last two, and flip */
    while (spr_pend)
        ;
    memcpy(sl_x, st_x, 8); memcpy(sl_y, st_y, 8); memcpy(sl_h, st_h, 8);
    memcpy(sl_lo, st_lo, 8); memcpy(sl_hi, st_hi, 8); memcpy(sl_size, st_size, 8); memcpy(sl_col, st_col, 8);
    sl_mx0 = st_mx0; sl_mx1 = st_mx1; sl_my0 = st_my0; sl_my1 = st_my1;
    a3d_show();
    spr_pend = 1;
}
