/*
 * Lunar Rider - the Atari display (the original's draw.c, redone for the
 * hardware).
 *
 * The screen (192 scan lines, PAL):
 *
 *   0-15    HUD: two ANTIC 6 rows (20 columns, 4 colours)
 *   16-55   the sky: five ANTIC 2 rows with stars (one glyph each) and messages
 *   56-83   far mountains: ANTIC D, 14 lines, own HSCROL + LMS  (1/4 speed)
 *   84-127  near mountains: ANTIC D, 22 lines, own HSCROL + LMS (1/2 speed)
 *           (ANTIC D fetches a line once for its 2 scan lines: half the DMA)
 *   128-135 a blank gap
 *   136-173 the ground: ANTIC E, 38 lines, own HSCROL + LMS    (full speed)
 *   174-191 two ANTIC 2 rows: checkpoint progress and messages
 *
 * Nothing is redrawn to scroll: the mountains are periodic, so each band
 * line is one period plus a screen's width and the LMS address walks
 * through it; the ground is a ring of 64 byte columns stored twice, so the
 * 48 bytes ANTIC fetches with fine scrolling never wrap. A new ground column
 * (8 world pixels) is drawn about every 3 to 5 frames. The DLI chain
 * (irq.s) gives each band its HSCROL and palette; the VBI moves every
 * band line's LMS at once.
 *
 * The buggy, the UFOs, the meteors and the explosions are players; bullets
 * and bombs are missiles (the fifth-player colour). The original's
 * coordinates are kept: screen x = x / 2 colour clocks, y = y * 3 / 4 lines.
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8gfx.h"
#include "a8pmg.h"
#include "game.h"
#include "view.h"

extern uint8_t dli_hs[8], dli_pf0[8], dli_pf1[8], dli_pf2[8], pend_hs[8];
extern uint8_t band_dl_lo[3], band_dl_hi[3], band_addr_lo[3], band_addr_hi[3];
extern uint8_t band_n[3], band_stride[3];
extern volatile uint8_t lms_req;
void irq_install(void);

/* ---- memory (the screen area $A000-$BFFF and a BSS block) */
#define FONT     ((uint8_t *)0xA000)   /* 1K: the ROM font + our glyphs */
#define FAR_MEM  ((uint8_t *)0xA400)   /* 28 lines x 68 */
#define HUD      ((uint8_t *)0xAB80)   /* 2 x 20 */
#define SKY      ((uint8_t *)0xABA8)   /* 5 x 20 (ANTIC 6) */
#define BOTTOM   ((uint8_t *)0xAC70)   /* 2 x 40 */
#define TTL7     ((uint8_t *)0xACC0)   /* 20: ANTIC 7 */
#define TTL6     ((uint8_t *)0xACD4)   /* 20: ANTIC 6 */
#define TTL2     ((uint8_t *)0xACE8)   /* 4 x 40 */
#define DL_GAME  ((uint8_t *)0xAD90)   /* (a display list can't cross 1K) */
#define GROUND   ((uint8_t *)0xB000)   /* 38 lines x 96: all in one 4K page */
#define DL_TITLE ((uint8_t *)0xBE40)
/* spare memory: the PMG area below the missiles */
#define GPAT     ((uint8_t (*)[38])0x9800)   /* 12 x 38 */
#define SKY_SAVE ((uint8_t *)0x99D0)         /* 100 */

#define FAR_LINES   14                 /* ANTIC D: 2 scan lines each */
#define FAR_STRIDE  68                 /* a period + the 48 bytes fetched */
#define FAR_PERIOD  20                 /* bytes: 80 colour clocks = 160 world px */
#define NEAR_LINES  22
#define NEAR_STRIDE 64
#define NEAR_PERIOD 13                 /* 52 colour clocks = 104 world px */
#define GND_LINES   38
#define GND_STRIDE  96
#define GND_W       48                 /* the ring: 48 byte columns, stored twice */
#define GND_TOP     136                /* first scan line of the ground band */
#define GND_DLI     13                 /* the line before world y 200 */
/* the ground: with HSCROL = 0, ANTIC shows the fetched line from its 5th byte; so the LMS starts GND_CAL bytes left of the scroll position and HSCROL = 3 - fine (measured), to put
   world x where the players are (measured in the emulator) */
#define GND_CAL     3
#define GND_HS_CAL  0
/* ground columns are numbered world byte + COL0, so the first ones (3 left
   of 0) aren't negative; COL0 * 8 is a multiple of the terrain ring (1024
   px) and of the ground ring (48 columns), so neither index changes */
#define COL0        768

/* the near mountains: 44 lines of 64 bytes, 64-aligned (no line crosses 4K) */
static uint8_t near_raw[NEAR_LINES * NEAR_STRIDE + 63];
static uint8_t *near_buf;
#define far_buf  FAR_MEM
#define ground   GROUND
#define sky_save SKY_SAVE
static uint8_t *dl_bands[2];            /* first band LMS low byte, per display list */

/* ---- colours */
#define C_FAR       A8_COLOR(5, 2)
#define C_FAR_RIDGE A8_COLOR(5, 6)
#define C_NEAR      A8_COLOR(6, 4)
#define C_NEAR_RIDGE A8_COLOR(6, 8)
#define C_BROWN     A8_COLOR(2, 6)
#define C_DKBROWN   A8_COLOR(2, 2)
#define C_GRAY      A8_COLOR(0, 6)
#define C_RED       A8_COLOR(4, 6)
#define C_WHITE     A8_COLOR(0, 14)
#define C_YELLOW    A8_COLOR(1, 12)
#define C_GREEN     A8_COLOR(12, 10)
#define C_ORANGE    A8_COLOR(2, 10)
#define C_CYAN      A8_COLOR(10, 12)
#define C_PINK      A8_COLOR(4, 10)
#define C_BOTTOM_BG A8_COLOR(8, 2)
#define C_SKYBLUE   A8_COLOR(9, 12)

/* glyphs we add to the font (internal codes) */
#define G_LIFE  0x3B                   /* '[': a buggy, for the lives */
#define G_BAR   0x40                   /* 0x40..0x48: a bar 0..8 pixels full */
/* stars, one glyph each: ANTIC 6 reaches glyphs 0..63 only, so these are
   punctuation no text uses */
static const uint8_t star_glyph[19] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x1C, 0x1D, 0x1E, 0x1F, 0x3C, 0x3D, 0x3E, 0x3F,
};

/* the original's triangle sine */
static int16_t approx_sin(uint8_t phase)
{
    if (phase < 64) return (int16_t)(phase * 4);
    if (phase < 192) return (int16_t)(255 - (phase - 64) * 2);
    return (int16_t)(-255 + (phase - 192) * 4);
}

/* one band: `period` bytes of pixels (4 a byte) from the tops, the rest of
   each line repeats them. Pixel value: below the top `below`, at it
   `ridge`, above it `above`. */
static void band_fill(uint8_t *buf, uint8_t lines, uint8_t stride, uint8_t period,
                      const uint8_t *tops, uint8_t above, uint8_t ridge, uint8_t below)
{
    uint8_t y, b, p, v, t, i;
    for (y = 0; y < lines; ++y, buf += stride) {
        const uint8_t *tp = tops;
        for (b = 0; b < period; ++b) {
            v = 0;
            for (p = 0; p < 4; ++p) {
                t = *tp++;
                v = (uint8_t)((v << 2) | (y > t ? below : y == t ? ridge : above));
            }
            buf[b] = v;
        }
        for (i = period; i < stride; ++i) buf[i] = buf[i - period];
    }
}

/* ---- the mountains: drawn once, the original's shapes (a triangle wave,
   the far range at 1/4 speed, the near at 1/2) */
static void draw_mountains(void)
{
    static uint8_t tops[FAR_PERIOD * 4];
    uint8_t x;
    int16_t h;
    /* far: h = 140 - peak * 40 / 255 in 90..140 (the original's), on 26 lines */
    for (x = 0; x < FAR_PERIOD * 4; ++x) {
        h = 140 - approx_sin((uint8_t)((x * 16) / 5)) * 40 / 255;
        if (h < 90) h = 90;
        if (h > 140) h = 140;
        tops[x] = (uint8_t)((h - 90) / 4);
    }
    band_fill(far_buf, FAR_LINES, FAR_STRIDE, FAR_PERIOD, tops, 0, 3, 1);
    /* near: h = 130 - peak * 60 / 255 in 80..160, on 42 lines */
    for (x = 0; x < NEAR_PERIOD * 4; ++x) {
        h = 130 - approx_sin((uint8_t)((x * 64) / 13)) * 60 / 255;
        if (h < 80) h = 80;
        if (h > 160) h = 160;
        tops[x] = (uint8_t)((h - 80) / 4);
    }
    band_fill(near_buf, NEAR_LINES, NEAR_STRIDE, NEAR_PERIOD, tops, 1, 3, 2);
}

/* ---- the ground: 12 kinds of pixel column, a pattern of 38 lines each */
#define K_CRATER 9
#define K_ROCK   10
#define K_MINE   11
#define gpat GPAT

static void ground_patterns(void)
{
    uint8_t k, l, v;
    int16_t oy, th;
    for (k = 0; k < 12; ++k) {
        th = k < 9 ? 192 + k : GROUND_Y;
        for (l = 0; l < GND_LINES; ++l) {
            oy = ((GND_TOP + l) * 4) / 3;           /* the original's y */
            if (k == K_CRATER) {
                v = oy < GROUND_Y + 16 ? 0 : 2;
            } else if (oy < th) {
                v = 0;
                if (k == K_ROCK && oy >= th - 10) v = 2;          /* grey above y 200 */
                if (k == K_MINE && oy >= th - 4) v = 3;           /* red */
            } else if (oy <= th + 2) {
                v = 1;                                            /* the surface */
            } else {
                v = oy < GROUND_Y ? 1 : 2;                        /* a hill / the regolith */
            }
            gpat[k][l] = (uint8_t)(v * 0x55);
        }
    }
}

static uint16_t next_col;              /* next ground byte column to draw */

/* ground.s: one byte column (the pixel kinds and the 38 lines) */
extern uint16_t dc_wx;
extern uint8_t dc_slot, dc_first, dc_end;
void draw_col(void);
void draw_col_kinds(void);
void draw_col_lines(void);

static uint8_t next_slot;               /* next_col % GND_W */

static void draw_next_column(void)
{
    dc_wx = next_col << 3;
    dc_slot = next_slot;
    draw_col();
    ++next_col;
    if (++next_slot == GND_W) next_slot = 0;
}

static void draw_column(uint16_t wb)
{
    dc_wx = wb << 3;
    dc_slot = (uint8_t)(wb % GND_W);
    draw_col();
}

/* the lines every kind of column has the same: sky above y 190, the
   regolith from 216 (ground.s draws lines 7..25) */
static void ground_fixed(void)
{
    uint8_t l;
    for (l = 0; l < GND_LINES; ++l)
        if (l < 7 || l > 25) memset(ground + l * GND_STRIDE, gpat[0][l], GND_STRIDE);
}


static uint8_t column_ready(uint16_t wb)
{
    /* generated to 3 pixels beyond (the wide rocks) */
    return (int16_t)(gen_x - (uint16_t)(((wb - COL0) << 3) + 11)) > 0;
}

static uint8_t col_half;                /* the second half of next_col is due */

void view_ground(void)
{
    uint16_t coarse = (uint16_t)(gs.scroll_px >> 3) + (COL0 - GND_CAL);
    uint8_t n, started = col_half;
    /* a column is drawn over two frames: kinds and lines 7..16, then 17..25 */
    if (col_half) {
        dc_first = 17;
        dc_end = 26;
        draw_col_lines();
        col_half = 0;
        ++next_col;
        if (++next_slot == GND_W) next_slot = 0;
    }
    while (gs.n_dirty) {
        uint16_t wb = (uint16_t)(gs.dirty[--gs.n_dirty] >> 3) + COL0;
        for (n = 0; n < 3; ++n, ++wb)
            if ((int16_t)(wb - (coarse - 1)) >= 0 && (int16_t)(next_col - wb) > 0)
                draw_column(wb - 1);
    }
    /* one a frame (the ground needs one every 2.7 frames at full speed) */
    if (!started && (int16_t)(coarse + GND_W - next_col) > 0 && column_ready(next_col)) {
        dc_wx = next_col << 3;
        dc_slot = next_slot;
        draw_col_kinds();
        dc_first = 7;
        dc_end = 17;
        draw_col_lines();
        col_half = 1;
    }
}

static void ground_all(void)
{
    uint16_t coarse = (uint16_t)(gs.scroll_px >> 3) + (COL0 - GND_CAL);
    next_col = coarse;
    next_slot = (uint8_t)(coarse % GND_W);
    col_half = 0;
    while ((int16_t)(coarse + GND_W - next_col) > 0)
        draw_next_column();
}

/* ---- display lists */
static uint8_t *dl_put(uint8_t *d, uint8_t op, const void *addr)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)addr;
    *d++ = (uint8_t)((uint16_t)addr >> 8);
    return d;
}

static uint8_t *dl_bands_build(uint8_t *d, uint8_t which)
{
    uint8_t i;
    dl_bands[which] = d + 1;
    for (i = 0; i < FAR_LINES; ++i)
        d = dl_put(d, i == FAR_LINES - 1 ? 0xDD : 0x5D, far_buf + i * FAR_STRIDE);
    for (i = 0; i < NEAR_LINES; ++i)
        d = dl_put(d, 0x5D, near_buf + i * NEAR_STRIDE);
    *d++ = 0xF0;                                  /* 8 blank lines, DLI D */
    for (i = 0; i < GND_LINES; ++i)
        d = dl_put(d, (i == GND_DLI || i == GND_LINES - 1) ? 0xDE : 0x5E,
                   ground + i * GND_STRIDE);
    *d++ = 0x10;                                  /* 2 blank lines */
    d = dl_put(d, 0x42, BOTTOM);
    *d++ = 0x02;
    return d;
}

static void dl_build(void)
{
    uint8_t *d = DL_GAME;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_put(d, 0x46, HUD);                     /* HUD: 2 x ANTIC 6 */
    *d++ = 0x86;                                  /* DLI A */
    d = dl_put(d, 0x46, SKY);                     /* sky: 5 x ANTIC 6 (half */
    *d++ = 0x06; *d++ = 0x06; *d++ = 0x06;        /* the DMA of ANTIC 2) */
    *d++ = 0x86;                                  /* DLI B */
    d = dl_bands_build(d, 0);
    d = dl_put(d, 0x41, DL_GAME);

    d = DL_TITLE;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_put(d, 0x47, TTL7);                    /* the title: ANTIC 7 */
    *d++ = 0x86;                                  /* the subtitle: ANTIC 6, DLI A */
    *d++ = 0x02; *d++ = 0x02; *d++ = 0x02;        /* 4 x ANTIC 2 (contiguous) */
    *d++ = 0x82;                                  /* DLI B */
    d = dl_bands_build(d, 1);
    d = dl_put(d, 0x41, DL_TITLE);
}

static void dli_tables(void)
{
    /*            hs  pf0       pf1        pf2 */
    static const uint8_t t[6][3] = {
        {C_YELLOW, C_WHITE,  C_SKYBLUE},          /* A: the sky's stars and text */
        {C_FAR,   C_NEAR,    C_FAR_RIDGE},        /* B: far mountains */
        {C_FAR,   C_NEAR,    C_NEAR_RIDGE},       /* C: near mountains */
        {C_BROWN, C_GRAY,    C_RED},              /* D: the ground, above y 200 */
        {C_BROWN, C_DKBROWN, C_RED},              /* E: the ground, below */
        {C_BROWN, C_WHITE,   C_BOTTOM_BG},        /* F: the bottom text */
    };
    uint8_t i;
    band_n[0] = FAR_LINES;  band_stride[0] = FAR_STRIDE;
    band_n[1] = NEAR_LINES; band_stride[1] = NEAR_STRIDE;
    band_n[2] = GND_LINES;  band_stride[2] = GND_STRIDE;
    for (i = 0; i < 6; ++i) {
        dli_pf0[i] = t[i][0];
        dli_pf1[i] = t[i][1];
        dli_pf2[i] = t[i][2];
    }
}

static void show_dl(uint8_t which)
{
    uint8_t *b = dl_bands[which];
    uint8_t *dl = which ? DL_TITLE : DL_GAME;
    lms_req = 0;
    band_dl_lo[0] = (uint8_t)(uint16_t)b;
    band_dl_hi[0] = (uint8_t)((uint16_t)b >> 8);
    b += FAR_LINES * 3;
    band_dl_lo[1] = (uint8_t)(uint16_t)b;
    band_dl_hi[1] = (uint8_t)((uint16_t)b >> 8);
    b += NEAR_LINES * 3 + 1;
    band_dl_lo[2] = (uint8_t)(uint16_t)b;
    band_dl_hi[2] = (uint8_t)((uint16_t)b >> 8);
    SDLSTL = (uint16_t)dl;
    lms_req = 0x87;                    /* every band's LMS in the new list */
}

/* ---- scrolling: the bands move 1 colour clock for 8 (far), 4 (near) and 2
   (ground) world pixels; their LMS byte offsets are kept here and step by
   one (the game moves at most 3 pixels a frame), so no division a frame */
static uint16_t sc_px, sc_gpx;          /* the last positions (low 16 bits) */
static uint8_t sc_valid, far_off, near_off, gnd_off;

static void band_addr(uint8_t b, const uint8_t *a)
{
    band_addr_lo[b] = (uint8_t)(uint16_t)a;
    band_addr_hi[b] = (uint8_t)((uint16_t)a >> 8);
}

void view_scroll(uint32_t px32, uint32_t gpx32)
{
    uint16_t px = (uint16_t)px32, gpx = (uint16_t)gpx32;
    uint8_t req = 0, pend, hs, p8, s8;

    if (!sc_valid || (uint16_t)(px - sc_px) > 8 || (uint16_t)(gpx - sc_gpx) > 8) {
        far_off = (uint8_t)((uint16_t)(px32 >> 5) % FAR_PERIOD);
        near_off = (uint8_t)((uint16_t)(px32 >> 4) % NEAR_PERIOD);
        gnd_off = (uint8_t)(((uint16_t)(gpx32 >> 3) + (COL0 - GND_CAL)) % GND_W);
        req = 7;
        sc_valid = 1;
    } else {
        /* a step of a few pixels: the low bytes tell which boundaries it crossed */
        if (px == sc_px && gpx == sc_gpx) return;
        p8 = (uint8_t)px;
        s8 = (uint8_t)sc_px;
        if ((p8 ^ s8) & 0xE0) { if (++far_off == FAR_PERIOD) far_off = 0; req |= 1; }
        if ((p8 ^ s8) & 0xF0) { if (++near_off == NEAR_PERIOD) near_off = 0; req |= 2; }
        if (((uint8_t)gpx ^ (uint8_t)sc_gpx) & 0xF8) { if (++gnd_off == GND_W) gnd_off = 0; req |= 4; }
    }
    sc_px = px;
    sc_gpx = gpx;

    pend = lms_req & 7;                 /* (a request the VBI hasn't taken yet) */
    lms_req = 0;
    p8 = (uint8_t)px;
    pend_hs[1] = (uint8_t)(3 - ((p8 >> 3) & 3));
    pend_hs[2] = (uint8_t)(3 - ((p8 >> 2) & 3));
    hs = (uint8_t)(3 - (((uint8_t)gpx >> 1) & 3) + GND_HS_CAL);
    pend_hs[3] = pend_hs[4] = pend_hs[5] = hs;
    if (req & 1) band_addr(0, far_buf + far_off);
    if (req & 2) band_addr(1, near_buf + near_off);
    if (req & 4) band_addr(2, ground + gnd_off);
    lms_req = (uint8_t)(0x80 | req | pend);
}

/* ---- text */
static uint8_t scode(char c)
{
    uint8_t a = (uint8_t)c;
    if (a < 0x20) return (uint8_t)(a + 0x40);
    if (a < 0x60) return (uint8_t)(a - 0x20);
    return a;
}
static void put2(uint8_t *dst, const char *s)
{
    while (*s) *dst++ = scode(*s++);
}

/* ANTIC 6/7: 20 columns, colour pf 0..3 */
static void put6(uint8_t *dst, const char *s, uint8_t pf)
{
    while (*s) *dst++ = (uint8_t)((scode(*s++) & 0x3F) | (pf << 6));
}

static void centre2(uint8_t *row, const char *s)
{
    memset(row, 0, 40);
    put2(row + ((40 - strlen(s)) >> 1), s);
}

void sky_msg(uint8_t row, const char *s, uint8_t pf)
{
    uint8_t *r = SKY + row * 20;
    memcpy(r, sky_save + row * 20, 20);
    if (s) put6(r + ((20 - strlen(s)) >> 1), s, pf);
}

void sky_clear(void)
{
    memcpy(SKY, sky_save, 100);
}

void title_row(uint8_t row, const char *s)
{
    centre2(TTL2 + row * 40, s);
}

void bottom_row(uint8_t row, const char *s)
{
    centre2(BOTTOM + row * 40, s);
}

void bar_draw(uint8_t px)
{
    uint8_t i, f;
    uint8_t *d = BOTTOM + 21;
    for (i = 0; i < 10; ++i) {
        f = px > i * 8 ? px - i * 8 : 0;
        if (f > 8) f = 8;
        d[i] = (uint8_t)(G_BAR + f);
    }
}

/* ---- the font: the OS's, and our glyphs */
static void font_init(void)
{
    uint8_t i, r;
    static const uint8_t life[8] = {0x00, 0x18, 0x3C, 0x7E, 0xFF, 0x66, 0x66, 0x00};
    memcpy(FONT, (const void *)0xE000, 1024);
    memcpy(FONT + G_LIFE * 8, life, 8);
    for (i = 0; i <= 8; ++i) {
        uint8_t *g = FONT + (G_BAR + i) * 8;
        uint8_t m = (uint8_t)(0xFF00 >> i);
        g[0] = 0; g[7] = 0;
        g[1] = 0xFF; g[6] = 0xFF;
        for (r = 2; r < 6; ++r) g[r] = (uint8_t)(m | 0x81);
    }
    for (i = 0; i < sizeof star_glyph; ++i) memset(FONT + star_glyph[i] * 8, 0, 8);
}

/* the original's 30 stars (its generator, seed 42: x = (seed >> 16) mod 320,
   y = (seed >> 8) mod 80, worked out beforehand), at y * 3 / 4; the ones
   behind the HUD are left out. One glyph each (two in a cell share one). */
static const uint16_t star_xy[16][2] = {{201, 16}, {37, 29}, {309, 35}, {68, 39}, {225, 11}, {53, 17}, {112, 37}, {261, 11}, {11, 33}, {142, 14}, {112, 38}, {302, 2}, {111, 25}, {105, 35}, {183, 15}, {149, 34}};

static void stars_init(void)
{
    uint8_t i, next = 0;
    memset(sky_save, 0, 100);
    for (i = 0; i < sizeof star_xy / sizeof star_xy[0]; ++i) {
        uint16_t sx = star_xy[i][0];
        uint8_t line = (uint8_t)star_xy[i][1];
        uint8_t cell = (uint8_t)((line >> 3) * 20 + (sx >> 4));
        uint8_t *g;
        if (!sky_save[cell]) {
            if (next >= sizeof star_glyph) continue;
            /* colours: white, yellow, pale blue (PF1, PF0, PF2) */
            sky_save[cell] = (uint8_t)(star_glyph[next] | ((0x9A >> ((next % 3) << 1)) & 3) << 6);
            ++next;
        }
        g = FONT + (sky_save[cell] & 0x3F) * 8;
        g[line & 7] |= (uint8_t)(0x80 >> ((sx >> 1) & 7));
    }
    memcpy(SKY, sky_save, 100);
}

/* ---- HUD */
static int32_t hud_score = -1, hud_hi = -1;
static int16_t hud_zone = -1, hud_lives = -1;
static int16_t hud_bar = -1;
uint32_t view_hiscore;
/* the score's digits, kept in decimal: the score only ever grows by
   hundreds, so a change is a few digit increments instead of six 32-bit
   divisions (~8,000 cycles) */
uint8_t score_dig[6];

void view_hud_reset(void)
{
    hud_score = hud_hi = -1;
    hud_zone = hud_lives = hud_bar = -1;
}

static void num6(uint8_t *dst, uint32_t v, uint8_t digits, uint8_t pf)
{
    uint8_t i;
    for (i = digits; i; --i) {
        dst[i - 1] = (uint8_t)((0x10 + (uint8_t)(v % 10)) | (pf << 6));
        v /= 10;
    }
}

static void dig_inc(uint8_t pos)
{
    for (;;) {
        if (++score_dig[pos] < 10) return;
        score_dig[pos] = 0;
        if (!pos) return;
        --pos;
    }
}

static void score_digits(void)
{
    uint8_t i;
    int32_t d32 = gs.score - hud_score;
    uint16_t d;
    if (hud_score < 0 || d32 < 0 || d32 > 30000) goto full;
    d = (uint16_t)d32;
    while (d >= 1000) { dig_inc(2); d -= 1000; }
    while (d >= 100) { dig_inc(3); d -= 100; }
    if (d) goto full;
    goto show;
full:
    {
        uint32_t v = (uint32_t)gs.score;
        for (i = 6; i; --i) { score_dig[i - 1] = (uint8_t)(v % 10); v /= 10; }
    }
show:
    for (i = 0; i < 6; ++i) HUD[6 + i] = (uint8_t)(0x50 + score_dig[i]);   /* PF1 digits */
    hud_score = gs.score;
}

void view_hud(void)
{
    int16_t bar;
    if ((uint8_t)gs.score != (uint8_t)hud_score || gs.score != hud_score) score_digits();
    if ((int32_t)view_hiscore != hud_hi) {
        hud_hi = (int32_t)view_hiscore;
        if (hud_hi == hud_score) memcpy(HUD + 20 + 3, HUD + 6, 6);
        else num6(HUD + 20 + 3, view_hiscore, 6, 1);
    }
    if (gs.checkpoint_cur != hud_zone) {
        hud_zone = gs.checkpoint_cur;
        num6(HUD + 18, (uint32_t)(gs.checkpoint_cur + 1), 2, 1);
    }
    if (gs.lives != hud_lives) {
        uint8_t i;
        hud_lives = gs.lives;
        for (i = 0; i < 4; ++i)
            HUD[20 + 16 + i] = i < gs.lives ? G_LIFE : 0;
    }
    if ((gs.frame & 3) && hud_bar >= 0) return;
    bar = (CHECKPOINT_DIST - gs.checkpoint_dist) * 4 / 25;      /* 0..80 */
    if (bar < 0) bar = 0;
    if (bar > 80) bar = 80;
    if (bar != hud_bar) {
        hud_bar = bar;
        bar_draw((uint8_t)bar);
    }
}

/* ---- sprites */
#define PM      ((uint8_t *)0x9800)
#define MISSILES (PM + 0x300)
#define PLAYER(n) (PM + 0x400 + ((uint16_t)(n) << 8))
/* (HPOSPn and SIZEPn have no OS shadow) */
#define HPOSP_(n, x) (*(volatile uint8_t *)(0xD000 + (n)) = (x))
#define HPOSM_(n, x) (*(volatile uint8_t *)(0xD004 + (n)) = (x))
#define SIZEP_(n, s) (*(volatile uint8_t *)(0xD008 + (n)) = (s))
#define PCOLR_(n, c) (*(uint8_t *)(0x2C0 + (n)) = (c))

/* sprites.s: players and missiles. ply_draw() takes its arguments in
   pd_*; a player's memory is only rewritten when its line, height or shape
   key changes */
extern uint8_t pd_n, pd_h, pd_key, mis_bomb;
extern int16_t pd_x, pd_y;
extern const uint8_t *pd_shape;
extern uint8_t p_y[4], p_h[4], p_key[4];
void ply_draw_asm(void);
void ply_clear_asm(void);
void missiles_draw(void);

#define ply_draw(n, x, y, shape, h, key) do { pd_n = (n); pd_x = (x); pd_y = (y); \
    pd_shape = (shape); pd_h = (h); pd_key = (key); ply_draw_asm(); } while (0)

static void ply_clear(uint8_t n)
{
    pd_n = n;
    ply_clear_asm();
}

/* the buggy: 16 colour clocks wide, players 0 (back) and 1 (front) */
static const uint16_t buggy_rows[2][12] = {
    {0x0600, 0x0600, 0x1F80, 0x1F80, 0x7FE0, 0x7FF8, 0x7FE0, 0x7FE0,
     0x70E0, 0x50A0, 0x70E0, 0x2040},
    {0x0600, 0x0600, 0x1F80, 0x1F80, 0x7FE0, 0x7FF8, 0x7FE0, 0x7FE0,
     0x70E0, 0x70E0, 0x50A0, 0x2040},
};
#define buggy_shape ((uint8_t (*)[2][12])0xAF00)   /* 48: free screen memory */

static const uint8_t ufo_sm[5]  = {0x30, 0x78, 0xFC, 0xFC, 0x78};
static const uint8_t ufo_lg[8]  = {0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0xFF, 0xFF, 0xA5};
static const uint8_t meteor[7]  = {0x18, 0x18, 0x78, 0xF0, 0xF0, 0xF0, 0x60};
#define KEY_BUGGY  1                    /* +frame */
#define KEY_UFO_SM 3
#define KEY_UFO_LG 4
#define KEY_METEOR 5
#define KEY_EXPL   16                   /* +radius */

static int16_t drawn_by = -1000;
static uint8_t drawn_frame;

static void draw_buggy(int16_t bx, int16_t by, uint8_t frame)
{
    if (by == drawn_by && p_key[0] == KEY_BUGGY && p_key[1] == KEY_BUGGY) {
        if (frame != drawn_frame) {
            /* only the wheels' rows 9 and 10 differ between the frames */
            uint8_t *p0 = PLAYER(0) + p_y[0] + 9;
            uint8_t *p1 = PLAYER(1) + p_y[1] + 9;
            p0[0] = buggy_shape[frame][0][9];
            p0[1] = buggy_shape[frame][0][10];
            p1[0] = buggy_shape[frame][1][9];
            p1[1] = buggy_shape[frame][1][10];
            drawn_frame = frame;
        }
        return;
    }
    drawn_by = by;
    drawn_frame = frame;
    ply_draw(0, bx, by - 5, buggy_shape[frame][0], 12, KEY_BUGGY);
    ply_draw(1, bx + 16, by - 5, buggy_shape[frame][1], 12, KEY_BUGGY);
    PCOLR_(0, C_YELLOW);
    PCOLR_(1, C_YELLOW);
    SIZEP_(0, 0);
    SIZEP_(1, 0);
}

void title_buggy(uint8_t x)
{
    draw_buggy(x, BUGGY_GROUND_Y, (x >> 1) & 1);
}

static void draw_enemy(uint8_t n, uint8_t i)
{
    uint8_t c = (en_flash[i] & 8) ? C_WHITE : C_CYAN;
    SIZEP_(n, 0);
    switch (en_type[i]) {
    case ENEMY_UFO_SM: ply_draw(n, en_x[i], en_y[i], ufo_sm, 5, KEY_UFO_SM); break;
    case ENEMY_UFO_LG: ply_draw(n, en_x[i] + 2, en_y[i], ufo_lg, 8, KEY_UFO_LG); break;
    default:           ply_draw(n, en_x[i], en_y[i] - 2, meteor, 7, KEY_METEOR); c = C_PINK; break;
    }
    PCOLR_(n, c);
}

/* explosions: the radius is 2, 4 .. 12 (game.c); a square of side 2r + 1
   (double-width player pixels, 4 world px each), its corners rounded once it
   is big; built once (EXPL_SHAPES, 63 bytes of free screen memory) */
#define EXPL_SHAPES ((uint8_t *)0xAF90)
static uint8_t expl_off[7], expl_h[7];

static void expl_init(void)
{
    uint8_t k, j, w, h, m, off = 0;
    for (k = 1; k <= 6; ++k) {
        uint8_t r = (uint8_t)(k * 2);
        uint8_t *e = EXPL_SHAPES + off;
        w = (uint8_t)((2 * r + 4) >> 2);
        h = (uint8_t)(((2 * r + 1) * 3) >> 2);
        m = (uint8_t)(0xFF00 >> w);
        for (j = 0; j < h; ++j) e[j] = m;
        if (h > 4 && w > 2) e[0] = e[h - 1] = (uint8_t)(m & 0x7E);
        expl_off[k] = off;
        expl_h[k] = h;
        off += h;
    }
}

static void draw_explosion(uint8_t n, uint8_t i)
{
    static uint8_t r, k, c;
    static int8_t life;
    r = ex_r[i];
    k = (uint8_t)(r >> 1);
    if (k < 1) k = 1;
    if (k > 6) k = 6;
    life = ex_life[i];
    SIZEP_(n, 1);
    ply_draw(n, ex_x[i] - r, ex_y[i] - r, EXPL_SHAPES + expl_off[k], expl_h[k], KEY_EXPL + k);
    c = life > 8 ? C_WHITE : life > 6 ? C_YELLOW : life > 3 ? C_ORANGE : C_RED;
    PCOLR_(n, c);
}

/* missiles: M0, M1 the forward bullets, M2 the upward ones (they share the
   buggy's x), M3 the bombs (one a frame when there are several) */
static void missiles(void)
{
    uint8_t i, j;
    mis_bomb = 0xFF;
    if (n_bm) {
        j = (uint8_t)(gs.frame % n_bm);
        for (i = 0; i < MAX_BOMBS; ++i)
            if (bm_on[i] && !j--) {
                mis_bomb = i;
                break;
            }
    }
    missiles_draw();
}

void view_sprites(void)
{
    uint8_t obj[MAX_ENEMIES + MAX_EXPLOSIONS];
    uint8_t nobj = 0, first, ns, s, i;

    if (gs.buggy.alive) {
        draw_buggy(gs.buggy.x, gs.buggy.y, gs.buggy.wheel_frame);
        first = 2;
    } else {
        first = 0;
    }
    ns = 4 - first;
    if (n_en) for (i = 0; i < MAX_ENEMIES; ++i)
        if (en_on[i]) obj[nobj++] = i;
    if (n_ex) for (i = 0; i < MAX_EXPLOSIONS; ++i)
        if (ex_on[i]) obj[nobj++] = (uint8_t)(0x80 | i);
    /* more objects than players: take turns (flicker) */
    i = nobj > ns ? (uint8_t)(gs.frame % nobj) : 0;
    for (s = 0; s < ns; ++s) {
        uint8_t n = (uint8_t)(first + s);
        if (s < nobj) {
            uint8_t o = obj[i];
            if (o & 0x80) draw_explosion(n, (uint8_t)(o & 0x7F));
            else draw_enemy(n, o);
            if (++i >= nobj) i = 0;
        } else if (p_h[n]) {
            ply_clear(n);
        }
    }
    missiles();
}

void view_hide_sprites(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) ply_clear(i);
    memset(fb_on, 0, sizeof fb_on);
    memset(ub_on, 0, sizeof ub_on);
    memset(bm_on, 0, sizeof bm_on);
    n_fb = n_ub = n_bm = 0;
    missiles();
}

/* ---- screens */
static void hud_labels(void)
{
    memset(HUD, 0, 40);
    put6(HUD, "SCORE", 0);
    put6(HUD + 13, "ZONE", 2);
    put6(HUD + 20, "HI", 3);
    put6(HUD + 20 + 10, "LIVES", 0);
    view_hud_reset();
}

void view_init(void)
{
    uint8_t i, f, r, b;
    near_buf = (uint8_t *)(((uint16_t)near_raw + 63) & 0xFFC0);
    memset(near_buf, 0, NEAR_LINES * NEAR_STRIDE);
    memset((void *)0xA400, 0, 0xBD00 - 0xA400);
    font_init();
    stars_init();
    draw_mountains();
    ground_patterns();
    ground_fixed();
    expl_init();
    for (f = 0; f < 2; ++f)
        for (r = 0; r < 12; ++r)
            for (b = 0; b < 2; ++b)
                buggy_shape[f][b][r] = (uint8_t)(b ? buggy_rows[f][r] : buggy_rows[f][r] >> 8);
    dl_build();
    dli_tables();
    hud_labels();

    COLOR0 = C_YELLOW;
    COLOR1 = C_WHITE;
    COLOR2 = C_GREEN;
    COLOR3 = C_ORANGE;                 /* missiles (fifth player) and HUD PF3 */
    COLOR4 = 0;
    CHBAS = 0xA0;

    pmg_init();
    GPRIOR = 0x11;                     /* players over playfield, missiles = fifth player */
    for (i = 0; i < 4; ++i) p_h[i] = p_key[i] = 0;
    irq_install();
}

void view_title(void)
{
    view_hide_sprites();
    memset(TTL7, 0, 40);
    put6(TTL7 + 4, "LUNAR RIDER", 3);
    put6(TTL6 + 0, "LUNAR DEFENSE FORCE", 1);
    COLOR1 = C_NEAR_RIDGE;             /* the subtitle (the original's purple) */
    dli_pf2[0] = 0;                    /* DLI A: the title's ANTIC 2 rows on black */
    COLOR3 = C_CYAN;                   /* the title */
    ground_all();
    sc_valid = 0;
    view_scroll(gs.scroll_px, gs.scroll_px);
    show_dl(1);
}

void view_game(void)
{
    view_hide_sprites();
    COLOR1 = C_WHITE;
    COLOR3 = C_ORANGE;
    dli_pf2[0] = C_SKYBLUE;            /* DLI A: the sky's (ANTIC 6) third colour */
    hud_labels();
    sky_clear();
    memset(BOTTOM, 0, 80);
    put2(BOTTOM + 10, "CHECKPOINT");
    ground_all();
    sc_valid = 0;
    view_scroll(gs.scroll_px, gs.scroll_px);
    show_dl(0);
}
