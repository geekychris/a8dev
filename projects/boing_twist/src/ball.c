/*
 * ball.c - the ball's texture, the screen's display list and colours, the
 * shadow and the markers (players).
 *
 * The texture is 64 x 32 texels round the ball (longitude east from 0,
 * latitude from the north pole), made from the puzzle's state: texel (u,
 * v) is in slot (band, seg); the piece there is home tile (hb, hs) turned
 * or not, so the texel is the picture's at the same place in tile (hb,
 * hs), read backwards if it is turned. Slots and tiles are cut at
 * round(s * 64 / S) (Medium's 12 segments are 5 or 6 texels wide), so a
 * texel's place in its tile is kept in 16ths. The first texel column and
 * row of each slot are the groove (colour 0: the room shows through).
 *
 * port: the original wraps a 1024 x 512 picture on a mesh lit per vertex;
 * here 64 x 32 texels in three colours, shaded a row at a time (the DLI).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8xe.h"
#include "bt.h"
#include "bt_core.h"
#include "ball.h"

/* the grid's tables (texture.s reads them) */
uint8_t tb_colslot[TEX_W], tb_fracu[TEX_W], tb_groove_u[TEX_W];
uint8_t tb_rowslot[TEX_H], tb_fracv[TEX_H], tb_groove_v[TEX_H];
uint8_t tb_srcu[BT_MAX_S * 16], tb_srcv[BT_MAX_B * 16];
uint8_t tb_home_hb16[BT_MAX_T], tb_home_hs16[BT_MAX_T], tb_band_base[BT_MAX_B];
#define colslot tb_colslot
#define fracu tb_fracu
#define groove_u tb_groove_u
#define rowslot tb_rowslot
#define fracv tb_fracv
#define groove_v tb_groove_v
#define srcu tb_srcu
#define srcv tb_srcv
extern uint8_t tx_v0, tx_v1, tx_ident, tx_S;
extern const void *tx_slots;
extern void tex_rows(void);
static uint8_t cur_S, cur_B, cur_pic = 0xFF;
uint8_t ball_room;
static uint8_t pic_col[3];
static const uint8_t shift_of[4] = { 6, 4, 2, 0 };

/* where segment s and band b start, in texels */
static uint8_t col0(uint8_t s) { return (uint8_t)(((uint16_t)s * TEX_W * 2 / cur_S + 1) >> 1); }
static uint8_t row0(uint8_t b) { return (uint8_t)(((uint16_t)b * TEX_H * 2 / cur_B + 1) >> 1); }

static void grid_tables(uint8_t B, uint8_t S)
{
    uint8_t s, b, u, v, f, w, a, h;
    if (S == cur_S && B == cur_B) return;     /* made for this grid already */
    cur_S = S;
    cur_B = B;
    for (b = 0, h = 0; b < B; ++b) {
        tb_band_base[b] = (uint8_t)(b * S * 2);
        for (s = 0; s < S; ++s, ++h) {
            tb_home_hb16[h] = (uint8_t)(b * 16);
            tb_home_hs16[h] = (uint8_t)(s * 16);
        }
    }
    for (s = 0; s < S; ++s) {
        a = col0(s);
        w = (uint8_t)(col0(s + 1) - a);
        for (u = a; u < a + w; ++u) {
            colslot[u] = s;
            fracu[u] = (uint8_t)(((uint16_t)(u - a) * 32 + 16) / w >> 1);    /* the texel's middle, 16ths */
            groove_u[u] = u == a && w > 4;           /* port: no grooves on Hard's 4-texel tiles */
        }
        for (f = 0; f < 16; ++f) srcu[s * 16 + f] = (uint8_t)(a + ((uint16_t)f * w >> 4));
    }
    for (b = 0; b < B; ++b) {
        a = row0(b);
        w = (uint8_t)(row0(b + 1) - a);
        for (v = a; v < a + w; ++v) {
            rowslot[v] = b;
            fracv[v] = (uint8_t)(((uint16_t)(v - a) * 32 + 16) / w >> 1);
            groove_v[v] = v == a && w > 4;
        }
        for (f = 0; f < 16; ++f) srcv[b * 16 + f] = (uint8_t)(a + ((uint16_t)f * w >> 4));
    }
}

static uint8_t pic_texel(uint8_t x, uint8_t y)
{
    return (uint8_t)((PIC_BUF[(uint16_t)y * 16 + (x >> 2)] >> shift_of[x & 3]) & 3);
}

/* the packed picture: the Boing ball made for the grid, or one from bank 3 */
static void load_picture(uint8_t pic)
{
    uint8_t x, y, c;
    if (pic == 0) {
        /* the checkerboard: row parity 0 and 1, made once and copied */
        static uint8_t rowpat[2][16];
        uint8_t *d = PIC_BUF;
        for (c = 0; c < 2; ++c)
            for (x = 0; x < TEX_W; x += 4) {
                uint8_t byte = 0, k;
                for (k = 0; k < 4; ++k)
                    byte = (uint8_t)((byte << 2) | (((colslot[x + k] + c) & 1) ? 1 : 2));
                rowpat[c][x >> 2] = byte;
            }
        for (y = 0; y < TEX_H; ++y, d += 16) memcpy(d, rowpat[rowslot[y] & 1], 16);
    } else
        xe_bank_read(3, pics[pic].addr - 0x4000, PIC_BUF, 512);
    memcpy(pic_col, pics[pic].col, 3);
}

/* what tile h looks like, upright and turned: its picture sampled 4 x 4
   (the four samples of a row make a byte; turned, the samples run
   backwards) */
static void signatures(BtPuzzle *p)
{
    uint8_t h, i, j, hb, hs, x, y, c, ub[4], tb[4];
    for (h = 0; h < (uint8_t)(p->bands * p->segs); ++h) {
        hb = tb_home_hb16[h];
        hs = tb_home_hs16[h];
        for (j = 0; j < 4; ++j) {
            uint8_t u8 = 0, t8 = 0;
            y = srcv[hb + j * 4 + 2];
            for (i = 0; i < 4; ++i) {
                x = srcu[hs + i * 4 + 2];
                c = pic_texel(x, y);
                u8 = (uint8_t)((u8 << 2) | c);
                t8 = (uint8_t)((t8 >> 2) | (c << 6));
            }
            ub[j] = u8;
            tb[3 - j] = t8;
        }
        bt_set_sig(p, h, *(uint32_t *)ub + 1, *(uint32_t *)tb + 1);
    }
}

void ball_picture(uint8_t pic, BtPuzzle *p, uint8_t sigs)
{
    grid_tables((uint8_t)p->bands, (uint8_t)p->segs);
    if (pic != cur_pic || pic == 0) load_picture(pic);
    cur_pic = pic;
    if (sigs) signatures(p);
}

/* rows v0..v1-1 of the texture from the state (ident: as solved, the
   peek): texture.s */
void ball_texture_rows(const BtPuzzle *p, uint8_t v0, uint8_t v1, uint8_t ident)
{
    tx_v0 = v0;
    tx_v1 = v1;
    tx_ident = ident;
    tx_slots = p->slot;
    tx_S = (uint8_t)p->segs;
    tex_rows();
}

void ball_texture(const BtPuzzle *p, uint8_t ident) { ball_texture_rows(p, 0, TEX_H, ident); }

/* a band's rows, for its twist */
static uint8_t tw_v0, tw_n;
void ball_twist_begin(uint8_t band)
{
    uint8_t v;
    tw_v0 = row0(band);
    tw_n = (uint8_t)(row0(band + 1) - tw_v0);
    for (v = 0; v < tw_n; ++v) memcpy(SAVED_BAND + v * 64, TEX_A + (uint16_t)(tw_v0 + v) * TEX_ROW, 64);
}

void ball_twist_shift(int8_t texels)
{
    rot_v0 = tw_v0;
    rot_n = tw_n;
    rot_shift = (uint8_t)texels;
    band_rotate();
}

uint8_t ball_seg_texels(void) { return (uint8_t)(TEX_W / cur_S); }

/* the turn (Y) that puts half k's middle in front: u_mid - 16 (gen_data.py) */
uint8_t ball_half_turn(uint8_t k)
{
    uint16_t mid2 = (uint16_t)(2 * k + cur_S / 2) * TEX_W / cur_S;   /* 2 x the middle's texel */
    return (uint8_t)(((mid2 + 1) >> 1) - 16) & 63;
}

/* ------------------------------------------------------------ the screen */
uint8_t screen_rows_ball_top;      /* the screen row of the buffer's row 0 */

static uint8_t lum_add(uint8_t c, int8_t d)
{
    int8_t l = (int8_t)((c & 0x0E) + d);
    if (l < 0) l = 0;
    if (l > 14) l = 14;
    return (uint8_t)((c & 0xF0) | l);
}

/* the ball's colours lit for each buffer row, and the room's rows with
   their shadow colour: made when the view, picture or room changes */
uint8_t vc0[BUF_ROWS], vc1[BUF_ROWS], vc2[BUF_ROWS], room_sh[SCREEN_ROWS];   /* (dlrows.s reads them) */
static uint8_t vc_view = 0xFF, vc_key[3], rs_room = 0xFF;

static void prepare(uint8_t view)
{
    uint8_t r;
    const int8_t *sh = view_shade[view];
    if (view != vc_view || memcmp(vc_key, pic_col, 3)) {
        vc_view = view;
        memcpy(vc_key, pic_col, 3);
        for (r = 0; r < BUF_ROWS; ++r) {
            vc0[r] = lum_add(pic_col[0], sh[r]);
            vc1[r] = lum_add(pic_col[1], sh[r]);
            vc2[r] = lum_add(pic_col[2], sh[r]);
        }
    }
    if (ball_room != rs_room) {
        uint8_t keep = (uint8_t)(100 - rooms[ball_room].dark), c;
        const uint8_t *rr = room_rows[ball_room];
        rs_room = ball_room;
        for (r = 0; r < SCREEN_ROWS; ++r) {       /* the room's colour, its brightness times (100 - dark)% */
            c = rr[r];
            room_sh[r] = (uint8_t)((c & 0xF0) | (((c & 0x0E) * keep / 100) & 0x0E));
        }
    }
}

/* The screen: the display list and the DLI's colour tables together.
 *
 * 3 x 8 blank lines, `big` rows of ANTIC 7 (16 lines, the title's letters)
 * standing for 8 mode D rows each, mode D rows (each the ball's buffer row
 * or the row of zeros), `ntext` text rows (each beyond two takes 4 mode D
 * rows off the bottom).
 *
 * Every row has its colours: the room's (one per row: walls, floor, grid
 * lines), the ball's three lit for its row, the shadow's. A DLI waits for
 * the end of a scan line (WSYNC) to change them, which costs a line of CPU
 * time, so only the rows after which something changes get the DLI bit;
 * the tables hold the colours of each change in turn (render.s). */
uint8_t tup[5], have, tab_end, *prev;            /* (dlrows.s too) */
extern uint8_t dr_r, dr_last, dr_br, dr_first, row_lo[BUF_ROWS], row_hi[BUF_ROWS];
extern int8_t dr_dx;
extern const uint8_t *dr_rr;
extern uint8_t *dr_d;
extern void dl_rows(void);

static void change(void)
{
    /* the entry before gets the DLI; the new colours are the next table row */
    if (have == (uint8_t)(tab_end)) return;      /* full (it never is) */
    *prev |= 0x80;
    TAB_BK[have] = tup[0];
    TAB_C0[have] = tup[1];
    TAB_C1[have] = tup[2];
    TAB_C2[have] = tup[3];
    TAB_SH[have] = tup[4];
    ++have;
}

void ball_screen(uint8_t big, uint8_t view, int8_t ball_row, int8_t dx, uint8_t ntext, uint8_t text_bg)
{
    uint8_t *d, *dl, r, last = (uint8_t)(SCREEN_ROWS - (ntext - 2) * 4), bk, base;
    uint16_t a;
    const uint8_t *rr = room_rows[ball_room];
    while (show_pending) ;                       /* the last one shown first */
    /* build the set not on screen */
    dl = d = shown ? DLIST : DLIST2;
    base = shown ? 0 : TAB_N;
    prepare(view);
    have = base;
    tab_end = (uint8_t)(base + TAB_N);
    *d++ = 0x70;
    *d++ = 0x70;
    prev = d;                                    /* the third blank line: row 0's colours */
    *d++ = 0x70;
    for (r = 0; r < big; ++r) {
        bk = rr[r * 8 + 4];
        tup[0] = bk; tup[1] = 0x1E; tup[2] = 0x34; tup[3] = 0x0E; tup[4] = bk;   /* the title's letters */
        change();
        prev = d;
        *d++ = 0x47;                             /* mode 7 + LMS */
        a = (uint16_t)(BIG_ROW + r * 20);
        *d++ = (uint8_t)a;
        *d++ = (uint8_t)(a >> 8);
    }
    /* the mode D rows: dlrows.s (this is its C):
     *   br = r - ball_row; the address BALL_BUF + br * 40 + dx or ZERO_ROW;
     *   colours (rr[r], vc0..2[br] or the last, room_sh[r]); a change marks
     *   the previous entry with the DLI and adds a table row */
    if (!row_hi[0])
        for (r = 0; r < BUF_ROWS; ++r) {
            a = (uint16_t)(BALL_BUF + r * BUF_STRIDE);
            row_lo[r] = (uint8_t)a;
            row_hi[r] = (uint8_t)(a >> 8);
        }
    dr_r = (uint8_t)(big * 8);
    dr_last = last;
    dr_br = (uint8_t)(dr_r - ball_row);
    dr_dx = dx;
    dr_rr = rr;
    dr_d = d;
    dr_first = 1;
    dl_rows();
    d = dr_d;
    /* the text rows: PF2 the background, PF1 the letters' brightness */
    tup[0] = 0; tup[1] = 0; tup[2] = 0x0E; tup[3] = text_bg; tup[4] = 0;
    change();
    *d++ = 0x42;
    *d++ = (uint8_t)(uint16_t)TEXT_ROWS;
    *d++ = (uint8_t)((uint16_t)TEXT_ROWS >> 8);
    for (r = 1; r < ntext; ++r) *d++ = 0x02;
    *d++ = 0x41;
    *d++ = (uint8_t)(uint16_t)dl;
    *d = (uint8_t)((uint16_t)dl >> 8);
    screen_rows_ball_top = (uint8_t)ball_row;
    show_dl = dl;
    show_base = base;
    show_pending = 1;                            /* the VBI swaps */
}

/* ------------------------------------------------------------ players */
/* the shadow: players 2 and 3, four times wide, side by side (64 colour
   clocks); the markers: players 0 and 1, yellow bars beside the band */
static uint8_t sh_y, sh_h, mk_y, mk_h;
static uint8_t shape2[128], shape3[128], shape_w, shape_h;
static const uint8_t circ64[64] = {
    64, 64, 64, 64, 64, 64, 64, 64, 63, 63, 63, 63, 63, 63, 62, 62, 62, 62, 61, 61, 61, 60, 60, 60, 59, 59, 58, 58, 57, 57, 56, 56, 55, 55, 54, 53, 53, 52, 51, 50, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 39, 38, 37, 35, 34, 32, 30, 28, 26, 24, 21, 18, 14, 8
};   /* 64 * sqrt(1 - ((k + 0.5) / 64)^2) */

void ball_shadow(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t *p2 = pmg_player_mem(2), *p3 = pmg_player_mem(3), i;
    if (h > 128) h = 128;
    if (w != shape_w || h != shape_h) {
        shape_w = w;
        shape_h = h;
        for (i = 0; i < h; ++i) {
            /* an ellipse: its half-width at line i, in colour clocks */
            uint8_t dy = (uint8_t)(2 * i + 1 < h ? h - 1 - 2 * i : 2 * i + 1 - h);   /* |2i + 1 - h| */
            uint8_t kk = (uint8_t)(((uint16_t)dy << 6) / (uint16_t)(h + 1));
            uint8_t hw = (uint8_t)(((uint16_t)w * circ64[kk]) >> 7);
            /* quad pixels k (colour clocks 4k..4k+3) whose middle 4k + 2 is
               inside [32 - hw, 32 + hw): k from (l + 1) / 4 to (r + 1) / 4 */
            uint8_t kl = (uint8_t)((32 - hw + 1) >> 2), kr = (uint8_t)((32 + hw + 1) >> 2);
            uint16_t m = (uint16_t)(0xFFFFu >> kl) & (uint16_t)~(0xFFFFu >> kr);
            uint8_t b2 = (uint8_t)(m >> 8), b3 = (uint8_t)m;
            shape2[i] = b2;
            shape3[i] = b3;
        }
    }
    if (y != sh_y || h != sh_h) {
        if (sh_h) { memset(p2 + sh_y, 0, sh_h); memset(p3 + sh_y, 0, sh_h); }
        sh_y = y;
        sh_h = h;
        memcpy(p2 + y, shape2, h);
        memcpy(p3 + y, shape3, h);
    }
    pmg_x(2, (uint8_t)(x - 32));
    pmg_x(3, x);
}

void ball_shadow_off(void)
{
    ball_shadow(0, 0, 0, 0);
    pmg_x(2, 0);
    pmg_x(3, 0);
}

void ball_markers(uint8_t on, uint8_t top, uint8_t bot, uint8_t xl, uint8_t xr)
{
    static const uint8_t bar = 0x3C;
    uint8_t *p0 = pmg_player_mem(0), *p1 = pmg_player_mem(1), i;
    if (mk_h) { memset(p0 + mk_y, 0, mk_h); memset(p1 + mk_y, 0, mk_h); }
    mk_h = 0;
    if (!on) return;
    mk_y = top;
    mk_h = (uint8_t)(bot - top);
    for (i = 0; i < mk_h; ++i) {
        uint8_t v = i == 0 || i == mk_h - 1 ? 0x18 : bar;
        p0[(uint8_t)(top + i)] = v;
        p1[(uint8_t)(top + i)] = v;
    }
    pmg_x(0, xl);
    pmg_x(1, xr);
}

void ball_clear_buf(void) { memset(BALL_BUF, 0, BUF_ROWS * BUF_STRIDE); }
