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

static uint8_t colslot[TEX_W], fracu[TEX_W], groove_u[TEX_W];
static uint8_t rowslot[TEX_H], fracv[TEX_H], groove_v[TEX_H];
static uint8_t srcu[BT_MAX_S * 16], srcv[BT_MAX_B * 16];
static uint8_t cur_S, cur_B, cur_pic;
uint8_t ball_room;
static uint8_t pic_col[3];
static const uint8_t shift_of[4] = { 6, 4, 2, 0 };

/* where segment s and band b start, in texels */
static uint8_t col0(uint8_t s) { return (uint8_t)(((uint16_t)s * TEX_W * 2 / cur_S + 1) >> 1); }
static uint8_t row0(uint8_t b) { return (uint8_t)(((uint16_t)b * TEX_H * 2 / cur_B + 1) >> 1); }

static void grid_tables(uint8_t B, uint8_t S)
{
    uint8_t s, b, u, v, f, w, a;
    cur_S = S;
    cur_B = B;
    for (s = 0; s < S; ++s) {
        a = col0(s);
        w = (uint8_t)(col0(s + 1) - a);
        for (u = a; u < a + w; ++u) {
            colslot[u] = s;
            fracu[u] = (uint8_t)(((uint16_t)(u - a) * 32 + 16) / w >> 1);    /* the texel's middle, 16ths */
            groove_u[u] = u == a;
        }
        for (f = 0; f < 16; ++f) srcu[s * 16 + f] = (uint8_t)(a + ((uint16_t)f * w >> 4));
    }
    for (b = 0; b < B; ++b) {
        a = row0(b);
        w = (uint8_t)(row0(b + 1) - a);
        for (v = a; v < a + w; ++v) {
            rowslot[v] = b;
            fracv[v] = (uint8_t)(((uint16_t)(v - a) * 32 + 16) / w >> 1);
            groove_v[v] = v == a;
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
        for (y = 0; y < TEX_H; ++y)
            for (x = 0; x < TEX_W; x += 4) {
                uint8_t byte = 0, k;
                for (k = 0; k < 4; ++k) {
                    c = (uint8_t)(((colslot[x + k] + rowslot[y]) & 1) ? 1 : 2);
                    byte |= (uint8_t)(c << shift_of[k]);
                }
                PIC_BUF[(uint16_t)y * 16 + (x >> 2)] = byte;
            }
    } else
        xe_bank_read(3, pics[pic].addr - 0x4000, PIC_BUF, 512);
    memcpy(pic_col, pics[pic].col, 3);
}

/* what tile h looks like, upright and turned: its picture sampled 4 x 4 */
static void signatures(BtPuzzle *p)
{
    uint8_t h, i, j, hb, hs;
    uint32_t up, tu;
    static uint8_t smp[16];
    for (h = 0; h < (uint8_t)(p->bands * p->segs); ++h) {
        hb = h / (uint8_t)p->segs;
        hs = h % (uint8_t)p->segs;
        for (j = 0; j < 4; ++j)
            for (i = 0; i < 4; ++i)
                smp[j * 4 + i] = pic_texel(srcu[hs * 16 + i * 4 + 2], srcv[hb * 16 + j * 4 + 2]);
        up = tu = 0;
        for (i = 0; i < 16; ++i) {
            up = (up << 2) | smp[i];
            tu = (tu << 2) | smp[15 - i];
        }
        bt_set_sig(p, h, up + 1, tu + 1);
    }
}

void ball_picture(uint8_t pic, BtPuzzle *p)
{
    grid_tables((uint8_t)p->bands, (uint8_t)p->segs);
    cur_pic = pic;
    load_picture(pic);
    signatures(p);
}

/* rows v0..v1-1 of the texture from the state (ident: as solved, the peek) */
void ball_texture_rows(const BtPuzzle *p, uint8_t v0, uint8_t v1, uint8_t ident)
{
    static uint8_t prow[BT_MAX_S], ou[BT_MAX_S], hsx[BT_MAX_S];
    uint8_t v, u, s, b, fv, fu, x, c, S = (uint8_t)p->segs, gv;
    uint8_t *ta;
    const BtPiece *q;
    for (v = v0; v < v1; ++v) {
        b = rowslot[v];
        fv = fracv[v];
        gv = groove_v[v];
        for (s = 0; s < S; ++s) {
            uint8_t home, hb, o;
            if (ident) { home = (uint8_t)(b * S + s); o = 0; }
            else { q = &p->slot[b * S + s]; home = q->home; o = q->orient; }
            hb = home / S;
            ou[s] = o;
            hsx[s] = (uint8_t)((home % S) * 16);
            prow[s] = srcv[hb * 16 + (o ? 15 - fv : fv)];
        }
        ta = TEX_A + (uint16_t)v * TEX_ROW;
        for (u = 0; u < TEX_W; ++u) {
            s = colslot[u];
            if (gv || groove_u[u]) c = 0;
            else {
                fu = fracu[u];
                if (ou[s]) fu = 15 - fu;
                x = srcu[hsx[s] + fu];
                c = pic_texel(x, prow[s]);
            }
            c = (uint8_t)(c << 6);
            ta[u] = c;
            if (u < 32) ta[u + 64] = c;
        }
    }
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
static uint8_t dl_big, dl_nd;      /* the list's big rows and mode D rows */

/* display list: 3 x 8 blank lines (the last with the DLI for row 0), then
   `big` rows of ANTIC 7 (16 lines, the title's letters) standing for 8 mode
   D rows each, mode D rows, `ntext` text rows (each beyond two takes 4
   mode D rows off the bottom) */
void ball_dlist(uint8_t big, int8_t ball_row, int8_t dx, uint8_t ntext)
{
    uint8_t *d = DLIST, r, br, last = (uint8_t)(SCREEN_ROWS - (ntext - 2) * 4);
    uint16_t a;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0xF0;                                 /* DLI: row 0's colours */
    for (r = 0; r < big; ++r) {
        *d++ = 0xC7;                             /* mode 7 + LMS + DLI */
        a = (uint16_t)(TEXT_ROWS + 80 + r * 20);
        *d++ = (uint8_t)a;
        *d++ = (uint8_t)(a >> 8);
    }
    dl_big = big;
    dl_nd = (uint8_t)(last - big * 8);
    for (r = (uint8_t)(big * 8); r < last; ++r) {
        br = (uint8_t)(r - ball_row);
        if (br < BUF_ROWS) a = (uint16_t)(BALL_BUF + br * BUF_STRIDE + dx);
        else a = (uint16_t)ZERO_ROW;
        *d++ = 0xCD;                             /* mode D + LMS + DLI */
        *d++ = (uint8_t)a;
        *d++ = (uint8_t)(a >> 8);
    }
    *d++ = 0x42;                                 /* text */
    *d++ = (uint8_t)(uint16_t)TEXT_ROWS;
    *d++ = (uint8_t)((uint16_t)TEXT_ROWS >> 8);
    for (r = 1; r < ntext; ++r) *d++ = 0x02;
    *d++ = 0x41;
    *d++ = (uint8_t)(uint16_t)DLIST;
    *d = (uint8_t)((uint16_t)DLIST >> 8);
    screen_rows_ball_top = (uint8_t)ball_row;
}

static uint8_t lum_add(uint8_t c, int8_t d)
{
    int8_t l = (int8_t)((c & 0x0E) + d);
    if (l < 0) l = 0;
    if (l > 14) l = 14;
    return (uint8_t)((c & 0xF0) | l);
}

/* each DLI entry's colours: the room's row, the ball's colours lit for its
   row, the shadow (the room's colour darker). Entry e sets row e's
   colours (the big rows take one entry each). */
void ball_colours(uint8_t view, int8_t ball_row, uint8_t text_bg)
{
    uint8_t big = dl_big, e, r, ne = (uint8_t)(dl_big + dl_nd), bk, br;
    const uint8_t *rr = room_rows[ball_room];
    const int8_t *sh = view_shade[view];
    int8_t d;
    for (e = 0; e < ne; ++e) {
        if (e < big) {
            r = (uint8_t)(e * 8 + 4);
            bk = rr[r];
            TAB_BK[e] = bk;
            TAB_C0[e] = 0x1E;                     /* the title's letters */
            TAB_C1[e] = 0x46;
            TAB_C2[e] = 0x0E;
            TAB_SH[e] = bk;
            continue;
        }
        r = (uint8_t)(e - big + big * 8);
        bk = rr[r];
        br = (uint8_t)(r - ball_row);
        d = br < BUF_ROWS ? sh[br] : 0;
        TAB_BK[e] = bk;
        TAB_C0[e] = lum_add(pic_col[0], d);
        TAB_C1[e] = lum_add(pic_col[1], d);
        TAB_C2[e] = lum_add(pic_col[2], d);
        TAB_SH[e] = lum_add(bk, -(int8_t)((rooms[ball_room].dark + 12) / 13 * 2));
    }
    /* the text rows: PF2 the background, PF1 the letters' brightness */
    TAB_BK[e] = 0x00;
    TAB_C0[e] = 0x00;
    TAB_C1[e] = 0x0E;
    TAB_C2[e] = text_bg;
    TAB_SH[e] = 0x00;
}

/* ------------------------------------------------------------ players */
/* the shadow: players 2 and 3, four times wide, side by side (64 colour
   clocks); the markers: players 0 and 1, yellow bars beside the band */
static uint8_t sh_y, sh_h, mk_y, mk_h;

void ball_shadow(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t *p2 = pmg_player_mem(2), *p3 = pmg_player_mem(3), i;
    if (sh_h) { memset(p2 + sh_y, 0, sh_h); memset(p3 + sh_y, 0, sh_h); }
    sh_y = y;
    sh_h = h;
    for (i = 0; i < h; ++i) {
        /* an ellipse: the half-width at line i, in pixels of a 64-clock pair */
        int16_t dy = (int16_t)(2 * i + 1 - h);
        uint16_t hw2 = (uint16_t)((int32_t)w * w * ((int32_t)h * h - (int32_t)dy * dy) / ((int32_t)h * h)) ;
        uint8_t hw = (uint8_t)isqrt16(hw2) / 2;           /* half width in colour clocks */
        uint8_t l = (uint8_t)(32 - hw), r = (uint8_t)(32 + hw), b2 = 0, b3 = 0, k;
        for (k = 0; k < 8; ++k) {
            uint8_t c = (uint8_t)(k * 4 + 2);                 /* a quad pixel's middle */
            if (c >= l && c < r) b2 |= (uint8_t)(0x80 >> k);
            c = (uint8_t)(32 + k * 4 + 2);
            if (c >= l && c < r) b3 |= (uint8_t)(0x80 >> k);
        }
        p2[(uint8_t)(y + i)] = b2;
        p3[(uint8_t)(y + i)] = b3;
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
