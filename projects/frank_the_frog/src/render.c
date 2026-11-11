/*
 * Frank the Frog - the Atari display.
 *
 * Game screen (display list at $BF00): 13 rows of 16 scan lines and a status
 * line, 216 lines from scan line 24.
 *   row 0      home bank, ANTIC 4 (4-colour characters), drawn once
 *   rows 1-5   river, rows 7-11 road: ANTIC 4 with LMS + HSCROL. Each lane
 *              is a circular strip in memory (its track, then the first 48
 *              bytes again) that ANTIC scrolls: moving a lane costs two
 *              address bytes and a fine-scroll value, no drawing at all.
 *   rows 6, 12 median and start strip: an ANTIC 2 text line (messages)
 *   status     ANTIC 2: score, high score, lives, level
 * A display-list interrupt at the end of each row loads the next row's
 * colours and fine scroll (display.s). Frank is players 0 (body) and 1
 * (eyes). The character set is the ROM font's 64 letters/digits (for the
 * ANTIC 2 lines) plus 64 tiles (tools/mkfont.py).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8gfx.h"
#include "game.h"
#include "lanes.h"
#include "frank.h"
#include "render.h"
#include "tiles.h"

#define CHARSET    ((uint8_t *)0xA000)  /* 1K */
#define LANEBUF    ((uint8_t *)0xA400)  /* 20 x 100 */
#define LANE_STRIDE 100
#define HOME_MEM   ((uint8_t *)0xAC00)  /* 2 x 40, ANTIC 4 */
#define MEDIAN_MEM ((uint8_t *)0xAC50)  /* 2 x 40, ANTIC 2 */
#define START_MEM  ((uint8_t *)0xACA0)  /* 2 x 40, ANTIC 2 */
#define HUD_MEM    ((uint8_t *)0xACF0)  /* 40 */
#define TITLE_MEM  ((uint8_t *)0xB000)  /* 20 (ANTIC 7) + 16 x 40 */
#define DL_GAME    ((uint8_t *)0xBF00)
#define DL_TITLE   ((uint8_t *)0xBE00)

extern uint8_t pend_lo[20], pend_hi[20], pend_hs[13], pend_ready, dl_off[20];
extern uint8_t row_hs[13], row_bk[13], row_pf0[13], row_pf1[13], row_pf2[13], row_pf3[13];
extern uint8_t *dl_base;
extern uint8_t pend_hpos;
extern void display_install(void);

/* screen pixel x of buffer pixel p is p - 4 * lms + hscrol - scroll_k */
uint8_t scroll_k = 16;

/* per lane, for render_scroll (lanes_asm.s): track length in characters and
   in pixels (period / 2), and the address of its top line's strip */
uint8_t lane_len[NUM_LANES], lane_tpx[NUM_LANES], lane_buf_lo[NUM_LANES], lane_buf_hi[NUM_LANES];

/* ------------------------------------------------------------ colours */

#define C_WATER   A8_COLOR(8, 4)
#define C_BANK    A8_COLOR(12, 2)
#define C_STRIP   A8_COLOR(5, 4)
#define C_FRANK   A8_COLOR(13, 12)

/* player 0 has priority over player 1: the body leaves holes for the eyes */
static const uint8_t frog_body[16] = {
    0x66, 0x99, 0xBD, 0xFF, 0x7E, 0x7E, 0xFF, 0xFF,
    0x7E, 0x7E, 0x7E, 0x3C, 0xDB, 0xC3, 0xC3, 0x00 };
static const uint8_t frog_eyes[16] = {
    0x00, 0x66, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t frog_splat[16] = {
    0x81, 0x42, 0x24, 0x99, 0x5A, 0x3C, 0xFF, 0x3C,
    0x3C, 0xFF, 0x3C, 0x5A, 0x99, 0x24, 0x42, 0x81 };

static void put_text(uint8_t *dst, const char *s)
{
    while (*s) *dst++ = a8_screen_code(*s++);
}

/* ------------------------------------------------------------ setup */

static void build_game_dl(void)
{
    uint8_t *p = DL_GAME, row, r, j;
    uint16_t a;

    *p++ = 0x70;
    *p++ = 0x70;
    for (row = 0; row < GRID_ROWS; ++row) {
        if (row == ROW_HOME) {
            *p++ = 0x44; *p++ = (uint8_t)(uint16_t)HOME_MEM; *p++ = (uint16_t)HOME_MEM >> 8;
            *p++ = 0x84;
        } else if (row == ROW_MEDIAN || row == ROW_START) {
            /* a blank line, a text line, 7 blank lines: the DLI's colour
               writes land on blank lines (a text line's first scan line has
               almost no CPU cycles left: ANTIC is fetching) */
            a = (uint16_t)(row == ROW_MEDIAN ? MEDIAN_MEM : START_MEM);
            *p++ = 0x00;
            *p++ = 0x42; *p++ = (uint8_t)a; *p++ = a >> 8;
            *p++ = 0xE0;
        } else {
            j = row_lane[row];
            for (r = 0; r < 2; ++r) {
                a = (uint16_t)(LANEBUF + (j * 2 + r) * LANE_STRIDE);
                *p++ = r ? 0xD4 : 0x54;              /* (DLI) LMS HSCROL mode 4 */
                dl_off[j * 2 + r] = (uint8_t)(p - DL_GAME);
                *p++ = (uint8_t)a; *p++ = a >> 8;
            }
        }
    }
    *p++ = 0x42; *p++ = (uint8_t)(uint16_t)HUD_MEM; *p++ = (uint16_t)HUD_MEM >> 8;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_GAME; *p = (uint16_t)DL_GAME >> 8;
}

static void build_title_dl(void)
{
    uint8_t *p = DL_TITLE, i;
    uint16_t a = (uint16_t)TITLE_MEM;
    for (i = 0; i < 7; ++i) *p++ = 0x70;
    *p++ = 0x47; *p++ = (uint8_t)a; *p++ = a >> 8;       /* the name, ANTIC 7 */
    *p++ = 0x70;
    a += 20;
    *p++ = 0x42; *p++ = (uint8_t)a; *p++ = a >> 8;
    for (i = 1; i < 16; ++i) *p++ = 0x02;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_TITLE; *p = (uint16_t)DL_TITLE >> 8;
}

/* the DLI colours of rows 1..12 and the status line (index = row - 1) */
static void set_row_colours(void)
{
    static const uint8_t car_col[5] = {
        A8_COLOR(4, 6), A8_COLOR(1, 12), A8_COLOR(8, 10), A8_COLOR(5, 8), A8_COLOR(4, 6) };
    uint8_t row, k, j;

    for (row = 1; row <= GRID_ROWS; ++row) {
        k = row - 1;
        row_hs[k] = pend_hs[k] = 0;
        row_pf3[k] = 0;
        if (row == ROW_MEDIAN || row == ROW_START) {
            row_bk[k] = row_pf2[k] = C_STRIP;
            row_pf1[k] = 0x0E;
            row_pf0[k] = 0;
            continue;
        }
        if (row == GRID_ROWS) {                       /* status line */
            row_bk[k] = row_pf2[k] = 0;
            row_pf1[k] = 0x0C;
            row_pf0[k] = 0;
            continue;
        }
        j = row_lane[row];
        if (lane_river[j]) {
            row_bk[k] = C_WATER;
            if (lane_kind[j] == KIND_LOG) {
                row_pf0[k] = A8_COLOR(2, 4);
                row_pf1[k] = A8_COLOR(2, 10);
                row_pf2[k] = A8_COLOR(2, 2);
            } else {
                row_pf0[k] = A8_COLOR(11, 6);
                row_pf1[k] = A8_COLOR(11, 2);
                row_pf2[k] = A8_COLOR(4, 12);
            }
        } else {
            row_bk[k] = (row & 1) ? A8_COLOR(0, 2) : A8_COLOR(0, 4);
            row_pf0[k] = car_col[j];
            row_pf1[k] = A8_COLOR(9, 12);
            row_pf2[k] = 0;
        }
    }
}

void render_init(void)
{
    /* the ROM font's codes 0..63 (punctuation, digits, capitals) + our tiles */
    memcpy(CHARSET, (void *)0xE000, 512);
    memcpy(CHARSET + 512, tile_font, sizeof(tile_font));
    memset(CHARSET + 512 + sizeof(tile_font), 0, 512 - sizeof(tile_font));
    memset((void *)0xA400, 0, 0x1B00);              /* lanes .. title */
    CHBAS = 0xA0;
    lanes_init(1);
    build_game_dl();
    build_title_dl();
    set_row_colours();
    dl_base = DL_GAME;
    pmg_init();
    GPRIOR = 1;
    display_install();
}

static void show_dl(uint8_t *dl)
{
    SDMCTL = 0;                    /* DMA off for a frame while the list changes */
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x1C;          /* normal playfield + players/missiles */
}

void render_game_screen(void)
{
    COLOR4 = C_BANK;
    COLOR0 = A8_COLOR(12, 6);
    COLOR1 = C_WATER;
    COLOR2 = A8_COLOR(12, 10);
    COLOR3 = 0;
    pmg_color(0, C_FRANK);
    pmg_color(1, 0x0E);
    pmg_size(0, 0);
    pmg_size(1, 0);
    render_hide_frank();
    show_dl(DL_GAME);
}

void render_title_screen(void)
{
    COLOR4 = 0;
    COLOR0 = A8_COLOR(12, 10);       /* ANTIC 7: the name */
    COLOR1 = 0x0C;                   /* ANTIC 2 text */
    COLOR2 = 0;
    COLOR3 = A8_COLOR(1, 12);
    pmg_color(0, A8_COLOR(12, 10));
    pmg_color(1, 0x0E);
    render_hide_frank();
    show_dl(DL_TITLE);
}

/* ------------------------------------------------------------ lanes */

void render_lanes_build(void)
{
    uint8_t j, k, m, n, w, len, c;
    const uint8_t *t;
    uint8_t *top, *bot;
    int8_t dir;

    set_row_colours();
    for (j = 0; j < NUM_LANES; ++j) {
        dir = lane_dir[j];
        len = (uint8_t)(lane_period[j] >> 3);
        lane_len[j] = len;
        lane_tpx[j] = (uint8_t)(lane_period[j] >> 1);
        switch (lane_kind[j]) {
        case KIND_BUS:  t = dir > 0 ? t_bus_r : t_bus_l; w = T_BUS_R_W; break;
        case KIND_LOG:  if (lane_width[j] == 64) { t = t_log32; w = T_LOG32_W; }
                        else { t = t_log24; w = T_LOG24_W; } break;
        case KIND_PADS: t = t_pads; w = T_PADS_W; break;
        default:        t = dir > 0 ? t_car_r : t_car_l; w = T_CAR_R_W; break;
        }
        top = LANEBUF + j * 2 * LANE_STRIDE;
        bot = top + LANE_STRIDE;
        lane_buf_lo[j] = (uint8_t)(uint16_t)top;
        lane_buf_hi[j] = (uint16_t)top >> 8;
        memset(top, 0, len);
        memset(bot, 0, len);
        for (k = 0; k < lane_num[j]; ++k) {
            c = (uint8_t)(lane_base[j * MAX_LANE_OBJECTS + k] >> 3);
            for (m = 0; m < w; ++m) {
                n = c + m;
                if (n >= len) n -= len;
                top[n] = t[m];
                bot[n] = t[w + m];
            }
        }
        memcpy(top + len, top, 48);
        memcpy(bot + len, bot, 48);
    }
    pend_ready = 0;
    render_scroll();
    pend_ready = 1;
}

/* render_scroll() is in lanes_asm.s. The C it implements (call it with
   pend_ready = 0, then set it):

    for (j = 0; j < NUM_LANES; ++j) {
        q = -(lane_shift[j] >> 1) - scroll_k;
        while (q < 0) q += lane_tpx[j];
        h = (uint8_t)(-q) & 3;
        i = (uint8_t)((q + h) >> 2);
        if (i >= lane_len[j]) i -= lane_len[j];
        a = lane_buf[j] + i;
        pend_lo/hi[j * 2] = a;  pend_lo/hi[j * 2 + 1] = a + LANE_STRIDE;
        pend_hs[lane_row[j] - 1] = h;
    }
*/

/* ------------------------------------------------------------ home bank */

void render_home(void)
{
    uint8_t s, c, x;
    for (x = 0; x < 40; ++x) {
        HOME_MEM[x] = t_grass[0];
        HOME_MEM[40 + x] = t_grass[1];
    }
    for (s = 0; s < NUM_HOMES; ++s) {
        c = (uint8_t)((home_cx[s] >> 1) - 8) >> 2;
        for (x = 0; x < 4; ++x) {
            HOME_MEM[c + x] = t_water[0];
            HOME_MEM[40 + c + x] = t_water[1];
        }
        if (home_filled[s]) {
            HOME_MEM[c + 1] = t_frog[0];
            HOME_MEM[c + 2] = t_frog[1];
            HOME_MEM[40 + c + 1] = t_frog[2];
            HOME_MEM[40 + c + 2] = t_frog[3];
        }
    }
}

/* ------------------------------------------------------------ Frank */

static uint8_t frank_shown;

void render_hide_frank(void)
{
    if (!frank_shown) return;
    pmg_clear(0);
    pmg_clear(1);
    frank_shown = 0;
}

static uint8_t frank_y, frank_look;

void render_frank(uint8_t state)
{
    uint8_t x = (uint8_t)(PMG_X0 + ((frog_px + 64) >> 1) - 32);
    uint8_t y = (uint8_t)(SCAN_TOP + frog_row * 16);
    uint8_t look = state == STATE_DYING ? 1 + ((frank_death_timer >> 2) & 1) : 0;
    pend_hpos = x;               /* the VBI moves it, with the lanes */
    if (frank_shown && y == frank_y && look == frank_look) return;
    frank_y = y;
    frank_look = look;
    if (state == STATE_DYING) {
        pmg_color(0, (frank_death_timer & 4) ? A8_COLOR(4, 8) : A8_COLOR(1, 14));
        pmg_set(0, y, frog_splat, 16);
        pmg_clear(1);
    } else {
        pmg_color(0, C_FRANK);
        pmg_set(0, y, frog_body, 16);
        pmg_set(1, y, frog_eyes, 16);
    }
    frank_shown = 1;
}

/* ------------------------------------------------------------ text */

/* port (speed): digits by subtracting powers of ten, in 16 bits below
   10000 (cc65's 32-bit arithmetic is slow) */
static const uint16_t pow10[4] = { 1000, 100, 10, 1 };

static void num6(uint8_t *dst, uint32_t v)
{
    uint8_t i, d;
    uint16_t w;
    d = 0x10;
    while (v >= 100000UL) { v -= 100000UL; ++d; }
    dst[0] = d;
    d = 0x10;
    while (v >= 10000UL) { v -= 10000UL; ++d; }
    dst[1] = d;
    w = (uint16_t)v;
    for (i = 0; i < 4; ++i) {
        d = 0x10;
        while (w >= pow10[i]) { w -= pow10[i]; ++d; }
        dst[i + 2] = d;
    }
}

void render_hud(uint32_t score, uint32_t hi, uint8_t lives, uint8_t level)
{
    static uint32_t last_hi = 0xFFFFFFFFUL;
    if (!HUD_MEM[1]) {
        put_text(HUD_MEM + 1, "SC");
        put_text(HUD_MEM + 12, "HI");
        put_text(HUD_MEM + 23, "FROGS");
        put_text(HUD_MEM + 32, "LEVEL");
    }
    num6(HUD_MEM + 4, score);
    if (hi != last_hi) {
        num6(HUD_MEM + 15, hi);
        last_hi = hi;
    }
    HUD_MEM[29] = 0x10 + lives;
    HUD_MEM[38] = 0x10 + level / 10;
    HUD_MEM[39] = 0x10 + level % 10;
}

static void centre(uint8_t *dst, const char *s)
{
    memset(dst, 0, 40);
    put_text(dst + ((40 - strlen(s)) >> 1), s);
}

void render_message(const char *s) { centre(MEDIAN_MEM, s); }
void render_start_message(const char *s) { centre(START_MEM, s); }

void title_print(uint8_t row, uint8_t col, const char *s)
{
    put_text(TITLE_MEM + 20 + row * 40 + col, s);
}

void title_clear_row(uint8_t row)
{
    memset(TITLE_MEM + 20 + row * 40, 0, 40);
}

static uint8_t face_body[32], face_eyes[32];

/* the title's big Frank: players 0/1 at four times the width, lines doubled */
void title_face(uint8_t blink)
{
    uint8_t i;
    if (!face_body[2]) {
        for (i = 0; i < 16; ++i) {
            face_body[i * 2] = face_body[i * 2 + 1] = frog_body[i];
            face_eyes[i * 2] = face_eyes[i * 2 + 1] = frog_eyes[i];
        }
        TITLE_MEM[3] = 0;
        put_text(TITLE_MEM + 3, "FRANK THE FROG");
    }
    pmg_size(0, 3);
    pmg_size(1, 3);
    pmg_x(0, PMG_X0 + 80 - 16);
    pmg_x(1, PMG_X0 + 80 - 16);
    pmg_set(0, 26, face_body, 32);
    if (blink) pmg_clear(1);
    else pmg_set(1, 26, face_eyes, 32);
    frank_shown = 1;
}
