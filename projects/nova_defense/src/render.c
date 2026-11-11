/*
 * Nova Defense - the Atari display.
 *
 * Scan lines are world y - 8. From the top: an ANTIC 6 status line (16-23),
 * the comet's ANTIC 4 row (28-35, fine-scrolled), the swarm, blank lines,
 * an ANTIC 6 status line (240-247).
 *
 * The swarm is 12 ANTIC 4 character rows (two an alien row) in a buffer
 * where the aliens never move: the display list puts the block where the
 * swarm is. Blank lines above it are the swarm's height (a drop adds 6),
 * each row's LMS + HSCROL its x, and CHBASE (charset A or B) its animation
 * frame, so a march step costs no drawing at all. A killed alien is
 * erased (or shows an explosion) in its cell. Display-list interrupts give
 * each alien type its body and accent colours (COLPF0/1).
 *
 * Everything else is players and missiles: the three shields are players
 * 0-2 at double width (one bit = 4 world pixels; the game keeps the
 * original's pixels), the gun is player 3, the bullets are the missiles
 * (fifth-player colour COLPF3).
 */
#pragma static-locals(on)     /* locals in static memory: much faster code from cc65 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "chars.h"
#include "render.h"

#define CHARSET_A  ((uint8_t *)0xA000)
#define CHARSET_B  ((uint8_t *)0xA400)
#define SWARM      ((uint8_t *)0xA800)    /* 12 rows x SW_STRIDE */
#define SW_STRIDE  112
#define SW_ORIGIN  168                    /* buffer pixel of the grid's x */
#define UFO_ROW    ((uint8_t *)0xAD40)    /* 128 */
#define UFO_ORIGIN 256
#define HUD_MEM    ((uint8_t *)0xADC0)    /* 2 x 20 (ANTIC 6) */
#define MSG_MEM    ((uint8_t *)0xADE8)    /* 20 */
#define DL0        ((uint8_t *)0xAE00)
#define DL1        ((uint8_t *)0xAF00)
#define TITLE_DL   ((uint8_t *)0xB000)
#define TITLE_MEM  ((uint8_t *)0xB100)

#define SCAN(y)    ((y) - 8)              /* world y -> scan line */

extern uint8_t t_hs[32], t_chb[32], t_pf0[32], t_pf1[32], t_pf3[32];
extern uint8_t dli_set, dl_pending;
extern void display_install(void);
extern uint8_t sw_lo[12], sw_hi[12];      /* display.s: the swarm rows' addresses */

/* screen pixel x of buffer pixel p = p - 4 * lms + hscrol - scroll_k */
uint8_t scroll_k = 16;

static uint8_t dl_cur;                    /* 0/1: the list being shown */
static const char *message;

const uint8_t type_body[3]   = { A8_COLOR(5, 8), A8_COLOR(12, 10), A8_COLOR(9, 10) };
const uint8_t type_accent[3] = { A8_COLOR(1, 14), 0x0E, A8_COLOR(2, 10) };
#define C_COMET  A8_COLOR(2, 6)
#define C_BULLET A8_COLOR(4, 14)
#define C_TEXT   0x0E

static uint8_t scode(char c)
{
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return c;
}

/* ------------------------------------------------------------ swarm */

/* the cell of alien (r, c): buffer pixel and so character and phase */
static const uint8_t col13[8] = { 0, 13, 26, 39, 52, 65, 78, 91 };

void render_alien_cell(uint8_t r, uint8_t c, uint8_t what)
{
    uint8_t px = SW_ORIGIN + col13[c] + ((r & 1) ? ALIEN_SHIFT / 2 : 0);   /* < 256 */
    uint8_t ph = px & 3, k, code;
    uint8_t *top = (uint8_t *)(sw_lo[r * 2] | (sw_hi[r * 2] << 8)) + (px >> 2);
    if (what == 0) code = 0;
    else if (what == 1) code = CH_ALIEN(r <= 1 ? 0 : r <= 3 ? 1 : 2, ph);
    else code = CH_BOOM(ph);
    for (k = 0; k < 3; ++k) {
        top[k] = code ? code + k : 0;
        top[SW_STRIDE + k] = code ? code + 3 + k : 0;
    }
}

void render_swarm_all(void)
{
    uint8_t r, c;
    memset(SWARM, 0, 12 * SW_STRIDE);
    for (r = 0; r < ALIEN_ROWS; ++r)
        for (c = 0; c < ALIEN_COLS; ++c)
            if (alive_mask[r] & (1 << c)) render_alien_cell(r, c, 1);
}

/* ------------------------------------------------------------ display list */


/* lms offset and HSCROL putting buffer pixel `origin` at screen x */
static uint8_t scroll_lms, scroll_hs;
static void scroll_for(int16_t origin, int16_t x)
{
    int16_t t = origin - scroll_k - x;     /* 4 * lms - h */
    scroll_lms = (uint8_t)((t + 3) >> 2);
    scroll_hs = (uint8_t)((scroll_lms << 2) - t);
}

/* what the vertical blank patches every frame (display.s) */
extern uint8_t vb_ufo_lms, vb_ufo_hs, vb_sw_lms, vb_sw_hs, vb_chb, vb_ready;
extern uint8_t lay_n[2], lay_off[24], lay_ufo[2], lay_dufo[2], lay_dsw[6], lay_dn[2];

static int16_t built_y = -1;              /* the swarm height the lists have */
static const char *built_msg;
static uint8_t built_n;                   /* lists built for this shape (0..2) */

/* a display list for the swarm's height and the message, built in dlbuf
   with its DLI entries and the places of its scrolled parts; then put into
   the list not shown (put_game_dl), which the vertical blank shows and
   patches every frame (display.s). (cc65 makes quick code of byte indexes
   into fixed arrays, so it is built here and copied.) */
#if 0
static uint8_t dlbuf[256], dl_len;
static uint8_t b_hs[16], b_chb[16], b_pf0[16], b_pf1[16], b_pf3[16], b_n;
static uint8_t b_off[12], b_rows, b_ufo, b_dufo, b_dsw[3], b_dn;

#define BDLI(hs, chb, p0, p1, p3) \
    (b_hs[b_n] = (hs), b_chb[b_n] = (chb), b_pf0[b_n] = (p0), b_pf1[b_n] = (p1), b_pf3[b_n] = (p3), ++b_n)

static void build_game_dl(void)
{
    static uint8_t i, k, top, line, nrows, n, type;

    b_n = 0;
    b_dn = 0;
    dlbuf[0] = 0x70;                                /* 8-15 */
    dlbuf[1] = 0xC6;                                /* 16-23: status, DLI */
    dlbuf[2] = (uint8_t)(uint16_t)HUD_MEM;
    dlbuf[3] = (uint16_t)HUD_MEM >> 8;
    dlbuf[4] = 0x30;                                /* 24-27 */
    if (built_msg) {                                /* 28-35: a message */
        dlbuf[5] = 0x46;
        dlbuf[6] = (uint8_t)(uint16_t)MSG_MEM;
        dlbuf[7] = (uint16_t)MSG_MEM >> 8;
        b_ufo = 0;
        b_dufo = 0xFF;
        BDLI(0, 0xE0, C_COMET, 0x0E, C_TEXT);
    } else {                                        /* ... or the comet */
        dlbuf[5] = 0x54;
        b_ufo = 6;
        b_dufo = 0;
        BDLI(0, 0xA0, C_COMET, 0x0E, C_BULLET);
    }
    i = 8;

    /* the swarm: from scan line top, as many rows as fit above 240 */
    top = built_y - 8 > 240 ? 240 : (uint8_t)SCAN(built_y);
    nrows = (uint8_t)(240 - top) >> 3;
    if (nrows > 12) nrows = 12;
    line = 36;
    if (nrows) {
        n = top - 36;                               /* blank lines above it */
        while (n > 8) { dlbuf[i++] = 0x70; n -= 8; }
        dlbuf[i++] = ((n - 1) << 4) | 0x80;
        b_dsw[b_dn++] = b_n;
        BDLI(0, 0xA0, type_body[0], type_accent[0], C_BULLET);
        line = top;
    }
    b_rows = nrows;
    for (k = 0; k < nrows; ++k, line += 8) {
        dlbuf[i] = 0x54;
        if (k == nrows - 1 && line + 8 == 240) {
            dlbuf[i] = 0xD4;                        /* last before the status line */
            BDLI(0, 0xE0, C_TEXT, C_TEXT, C_TEXT);
        } else if ((k == 3 || k == 7) && k < nrows - 1) {
            dlbuf[i] = 0xD4;                        /* the next alien type's colours */
            type = k == 3 ? 1 : 2;
            b_dsw[b_dn++] = b_n;
            BDLI(0, 0xA0, type_body[type], type_accent[type], C_BULLET);
        }
        b_off[k] = i + 1;
        dlbuf[i + 1] = sw_lo[k];
        dlbuf[i + 2] = sw_hi[k];
        i += 3;
    }
    if (line < 240) {
        n = 240 - line;
        while (n > 8) { dlbuf[i++] = 0x70; n -= 8; }
        dlbuf[i++] = ((n - 1) << 4) | 0x80;
        BDLI(0, 0xE0, C_TEXT, C_TEXT, C_TEXT);
    }
    dlbuf[i] = 0x46;                                /* 240-247 */
    dlbuf[i + 1] = (uint8_t)(uint16_t)(HUD_MEM + 20);
    dlbuf[i + 2] = (uint16_t)(HUD_MEM + 20) >> 8;
    dlbuf[i + 3] = 0x41;
    dlbuf[i + 4] = 0;
    dl_len = i + 6;
}

/* dlbuf into the list not shown, with its DLI table and layout; shown at
   the next vertical blank */
static void put_game_dl(void)
{
    static uint8_t s, t, k;
    static uint8_t *dst;
    s = dl_cur ? 0 : 1;                   /* the set not shown */
    dst = s ? DL1 : DL0;
    dlbuf[dl_len - 1] = (uint16_t)dst >> 8;
    memcpy(dst, dlbuf, dl_len);
    t = s * 16;
    memcpy(t_hs + t, b_hs, b_n);
    memcpy(t_chb + t, b_chb, b_n);
    memcpy(t_pf0 + t, b_pf0, b_n);
    memcpy(t_pf1 + t, b_pf1, b_n);
    memcpy(t_pf3 + t, b_pf3, b_n);
    memcpy(lay_off + s * 12, b_off, 12);
    lay_n[s] = b_rows;
    lay_ufo[s] = b_ufo;
    lay_dufo[s] = b_dufo == 0xFF ? 0xFF : b_dufo + t;
    lay_dn[s] = b_dn;
    for (k = 0; k < b_dn; ++k) lay_dsw[s * 3 + k] = b_dsw[k] + t;
    dli_set = t;
    dl_pending = (uint16_t)dst >> 8;
    dl_cur ^= 1;
}
#endif

/* display.s builds it (build_game_dl above is the same in C) */
extern uint8_t bd_top, bd_msg, bd_set;
extern void build_dl(void);

static void new_dl(void)
{
    bd_set = dl_cur ? 0 : 1;              /* the set not shown */
    bd_top = built_y - 8 > 240 ? 240 : (uint8_t)SCAN(built_y);
    bd_msg = built_msg != 0;
    build_dl();
    dli_set = bd_set * 16;
    dl_pending = 0xAE + bd_set;
    dl_cur ^= 1;
}

/* this frame's scroll positions and charset, for the vertical blank; a new
   list when the swarm dropped or a message came or went */
static void update_display(void)
{
    if (grid_y != built_y || message != built_msg) {
        built_y = grid_y;
        built_msg = message;
        built_n = 0;
    }
    if (built_n < 2 && !dl_pending) {     /* both lists, in turn */
        new_dl();
        ++built_n;
    }
    vb_ready = 0;
    scroll_for(UFO_ORIGIN, ufo_active ? ufo_x >> 1 : -40);
    vb_ufo_lms = scroll_lms;
    vb_ufo_hs = scroll_hs;
    scroll_for(SW_ORIGIN, grid_x >> 1);
    vb_sw_lms = scroll_lms;
    vb_sw_hs = scroll_hs;
    vb_chb = anim_frame ? 0xA4 : 0xA0;
    vb_ready = 1;
}

/* ------------------------------------------------------------ players */

#define PL(n)   ((uint8_t *)(0x9C00 + ((n) << 8)))
#define MISSILE ((uint8_t *)0x9B00)

/* a shield row's 32 pixels -> 8 bits (a bit: any of 4 pixels left): display.s */
extern uint8_t __fastcall__ row_bits(const uint8_t *p);

static void shields_draw(void)
{
    static uint8_t s, y;
    static uint16_t bit;
    if (!shield_dirty) return;
    bit = 1;
    for (y = 0; y < SHIELD_H; ++y, bit <<= 1) {
        if (!(shield_dirty & bit)) continue;
        for (s = 0; s < SHIELD_COUNT; ++s)
            PL(s)[SCAN(SHIELD_Y) + y] = row_bits(shield_px[s][y]);
    }
    shield_dirty = 0;
}

static uint8_t m_y[4], m_h[4];            /* missile rows drawn */
static const uint8_t m_mask[4] = { 0x03, 0x0C, 0x30, 0xC0 };

static void missile(uint8_t n, uint8_t on, int16_t x, int16_t y, uint8_t h, uint8_t pattern)
{
    uint8_t i, *m, keep = ~m_mask[n];
    m = MISSILE + m_y[n];
    for (i = m_h[n]; i; --i, ++m) *m &= keep;
    m_h[n] = 0;
    if (!on || y < 32 || y > 247 + 8) return;
    m_y[n] = (uint8_t)SCAN(y);
    m_h[n] = h;
    pmg_missile_x(n, (uint8_t)(PMG_X0 + (x >> 1)));
    m = MISSILE + m_y[n];
    for (i = 0; i < h; ++i, ++m) {
        uint8_t b = ((pattern >> (i & 3)) & 1) ? 0xFF : 0xAA;     /* 2 or 1 colour clocks */
        *m |= b & m_mask[n];
    }
}

static uint8_t ship_shown, ship_look;
static const uint8_t boom_shape[8] = { 0x24, 0x81, 0x5A, 0x3C, 0xFF, 0x3C, 0x5A, 0x99 };

static void ship_draw(void)
{
    uint8_t look, show;
    show = state == STATE_PLAYING || (state == STATE_DYING && (state_timer & 4)) ||
           state == STATE_WAVE_CLEAR;
    look = state == STATE_DYING ? 2 : 1;
    pmg_x(3, (uint8_t)(PMG_X0 + (player_x >> 1)));
    if (show && ship_shown && look == ship_look) return;
    memset(PL(3) + SCAN(PLAYER_Y), 0, 8);
    ship_shown = 0;
    if (!show) return;
    memcpy(PL(3) + SCAN(PLAYER_Y), look == 2 ? boom_shape : ship_shape, 8);
    pmg_color(3, look == 2 ? A8_COLOR(1, 14) : A8_COLOR(13, 12));
    ship_shown = 1;
    ship_look = look;
}

/* ------------------------------------------------------------ explosions */

static uint8_t exd_on[MAX_EXPLOSIONS];   /* drawn in the swarm buffer */

static void explosions_draw(void)
{
    uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; ++i) {
        if (ex_kind[i] != EXP_ALIEN) continue;
        if (ex_active[i] && !exd_on[i]) {
            render_alien_cell(ex_row[i], ex_col[i], 2);
            exd_on[i] = 1;
        } else if (!ex_active[i] && exd_on[i]) {
            if (!(alive_mask[ex_row[i]] & (1 << ex_col[i]))) render_alien_cell(ex_row[i], ex_col[i], 0);
            exd_on[i] = 0;
        }
    }
}

/* ------------------------------------------------------------ the frame */

void render_frame(void)
{
    uint8_t i;
    /* the comet: its two characters, or an explosion there */
    UFO_ROW[UFO_ORIGIN / 4] = ufo_active ? CH_COMET : 0;
    UFO_ROW[UFO_ORIGIN / 4 + 1] = ufo_active ? CH_COMET + 1 : 0;
    explosions_draw();
    shields_draw();
    ship_draw();
    update_display();
    missile(0, player_bullet.active, player_bullet.x, player_bullet.y, 6, 0x0F);
    for (i = 0; i < MAX_ALIEN_BULLETS; ++i)
        missile(1 + i, alien_bullets[i].active, alien_bullets[i].x - 1, alien_bullets[i].y,
                3, (alien_bullets[i].y & 4) ? 0x02 : 0x05);
}

void render_message(const char *s)
{
    uint8_t n;
    message = s;
    memset(MSG_MEM, 0, 20);
    if (!s) return;
    n = (uint8_t)strlen(s);
    s += 0;
    {
        uint8_t *d = MSG_MEM + ((20 - n) >> 1);
        while (*s) *d++ = scode(*s++) | 0xC0;
    }
}

/* ------------------------------------------------------------ status */

void hud_clear(uint8_t line) { memset(HUD_MEM + line * 20, 0, 20); }

void hud_print(uint8_t line, uint8_t x, const char *s)
{
    uint8_t *d = HUD_MEM + line * 20 + x;
    while (*s) *d++ = scode(*s++) | 0xC0;
}

static const uint16_t p10[4] = { 1000, 100, 10, 1 };

void hud_num(uint8_t line, uint8_t x, uint32_t v, uint8_t digits)
{
    uint8_t *d = HUD_MEM + line * 20 + x, i, c;
    uint16_t w;
    char buf[6];
    c = '0'; while (v >= 100000UL) { v -= 100000UL; ++c; } buf[0] = c;
    c = '0'; while (v >= 10000UL) { v -= 10000UL; ++c; } buf[1] = c;
    w = (uint16_t)v;
    for (i = 0; i < 4; ++i) { c = '0'; while (w >= p10[i]) { w -= p10[i]; ++c; } buf[2 + i] = c; }
    for (i = 6 - digits; i < 6; ++i) *d++ = scode(buf[i]) | 0xC0;
}

/* ------------------------------------------------------------ setup */

void render_init(void)
{
    uint8_t k;
    uint16_t a;
    for (k = 0; k < 12; ++k) {
        a = (uint16_t)SWARM + k * SW_STRIDE;
        sw_lo[k] = (uint8_t)a;
        sw_hi[k] = a >> 8;
    }
    memcpy(CHARSET_A, charset_a, 1024);
    memcpy(CHARSET_B, charset_b, 1024);
    memset(SWARM, 0, 0x1000 - 0x800);
    pmg_init();
    GPRIOR = 0x11;                        /* players over playfield, fifth player */
    display_install();
}

static void show(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x1C;
}

void render_game_screen(void)
{
    uint8_t i;
    memset((void *)0x9B00, 0, 0x500);     /* players and missiles */
    memset(m_h, 0, sizeof(m_h));
    memset(exd_on, 0, sizeof(exd_on));
    ship_shown = 0;
    for (i = 0; i < 3; ++i) {
        pmg_size(i, 1);
        pmg_color(i, A8_COLOR(10, 10));
        pmg_x(i, (uint8_t)(PMG_X0 + (shield_x[i] >> 1)));
    }
    pmg_size(3, 0);
    SIZEM = 0;
    COLOR4 = 0;
    COLOR0 = C_TEXT;
    COLOR1 = C_TEXT;
    COLOR2 = 0x0E;                        /* explosions */
    COLOR3 = C_TEXT;                      /* the status text */
    CHBAS = 0xE0;
    shield_dirty = 0x3FFF;
    render_swarm_all();
    message = 0;
    vb_ready = 0;
    built_y = -1;
    dl_cur = 1;                           /* build into set 0 ... */
    built_y = grid_y;
    built_msg = 0;
    new_dl();                             /* ... (dl_cur is 0 after) */
    built_n = 1;
    dl_pending = 0;
    show(DL0);
}

/* ------------------------------------------------------------ title */

/* The title: the name (ANTIC 7), then for each alien type and the comet a
   character row of the game's charset in its colours (display-list
   interrupts switch the charset and colours) over a line of text, then the
   high scores and the controls (ANTIC 6). Charsets A/B alternate: the
   aliens beat their wings. */
#define T_TEXT   ((uint8_t *)0xB100)      /* 20 lines x 20 */
#define T_ALIENS ((uint8_t *)0xB300)      /* 7 rows x 40 */

static uint8_t t_frame;

void title_clear(void)
{
    uint8_t t, k;
    static const uint8_t comet[2] = { CH_COMET, CH_COMET + 1 };
    memset(T_TEXT, 0, 20 * 20);
    memset(T_ALIENS, 0, 7 * 40);
    for (t = 0; t < 3; ++t)
        for (k = 0; k < 3; ++k) {
            T_ALIENS[t * 80 + 9 + k] = CH_ALIEN(t, 0) + k;
            T_ALIENS[t * 80 + 40 + 9 + k] = CH_ALIEN(t, 0) + 3 + k;
        }
    T_ALIENS[240 + 9] = comet[0];
    T_ALIENS[240 + 10] = comet[1];
}

/* text line n (0 = the name, ANTIC 7; 1-4 under the aliens and the comet;
   5.. the rest), ANTIC 6 (20 columns, white) */
void title_print(uint8_t row, uint8_t col, const char *s)
{
    uint8_t *d = T_TEXT + row * 20 + col;
    while (*s) *d++ = scode(*s++) | (row ? 0xC0 : 0x40);
}

static void tlms(uint8_t **pp, uint8_t mode, const uint8_t *a)
{
    uint8_t *p = *pp;
    p[0] = mode;
    p[1] = (uint8_t)(uint16_t)a;
    p[2] = (uint16_t)a >> 8;
    *pp = p + 3;
}

void render_title_screen(void)
{
    uint8_t *p = TITLE_DL, i, t;
    vb_ready = 0;                         /* nothing to patch here */
    memset((void *)0x9B00, 0, 0x500);
    for (i = 0; i < 4; ++i) pmg_size(i, 0);
    *p++ = 0x70; *p++ = 0x70;             /* 8-23 */
    tlms(&p, 0x47, T_TEXT);               /* 24-39: the name */
    *p++ = 0xF0;                          /* 40-47, DLI: the first alien */
    i = 0;
    for (t = 0; t < 4; ++t) {
        /* DLI entries: the alien's charset and colours, then back to text */
        t_hs[i] = 0; t_chb[i] = 0xA0;
        t_pf0[i] = t < 3 ? type_body[t] : C_COMET;
        t_pf1[i] = t < 3 ? type_accent[t] : 0x0E;
        t_pf3[i] = C_TEXT;
        ++i;
        t_hs[i] = 0; t_chb[i] = 0xE0; t_pf0[i] = C_TEXT; t_pf1[i] = C_TEXT; t_pf3[i] = C_TEXT;
        ++i;
        if (t < 3) {
            tlms(&p, 0x44, T_ALIENS + t * 80);
            *p++ = 0x84;                  /* its second row, DLI: text */
        } else {
            tlms(&p, 0xC4, T_ALIENS + 240);   /* the comet: one row, DLI: text */
        }
        tlms(&p, t < 3 ? 0xC6 : 0x46, T_TEXT + (1 + t) * 20);   /* DLI: the next */
    }
    *p++ = 0x70;
    tlms(&p, 0x46, T_TEXT + 5 * 20);
    for (i = 6; i < 16; ++i) *p++ = 0x06;
    *p++ = 0x41; *p++ = 0; *p = (uint16_t)TITLE_DL >> 8;
    COLOR4 = 0;
    COLOR0 = A8_COLOR(12, 10);
    COLOR1 = A8_COLOR(5, 10);
    COLOR2 = 0x0E;
    COLOR3 = C_TEXT;
    CHBAS = 0xE0;
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)TITLE_DL;          /* (the VBI writes only the high byte) */
    dli_set = 0;
    dl_pending = (uint16_t)TITLE_DL >> 8;     /* the VBI shows it, table 0 */
    frame_wait();
    frame_wait();
    SDMCTL = 0x22 | 0x1C;
    dl_cur = 0;
}

/* the aliens beat their wings: charset A or B */
void title_anim(uint8_t frame)
{
    uint8_t i, c = (frame & 16) ? 0xA4 : 0xA0;
    if (c == t_frame) return;
    t_frame = c;
    for (i = 0; i < 8; i += 2) t_chb[i] = c;
}
