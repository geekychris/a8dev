/*
 * Uranus Lander - the Atari display (the original's draw.c, redone for the
 * hardware).
 *
 * The game screen (192 lines, PAL):
 *
 *   0-7     HUD: score, fuel gauge, level (ANTIC 6: 20 columns, 4 colours)
 *   8-183   the world: ANTIC E, 160 x 176, 4 colours; world (x, y) is pixel
 *           (x / 2, y * 3 / 4 - 8). Sky (the background colour), terrain,
 *           the surface line and the stars (colour 2), pads and sparks (3)
 *   184-191 HUD: lives, altitude, sideways and vertical speed (ANTIC 6)
 *
 * A DLI every 8 lines (irq.s) sets the background and the ground colour:
 * a sky that turns from black to Uranus's blue-green at the horizon, and a
 * ground in strata. The messages (LANDED, CRASHED, GAME OVER) are two ANTIC
 * 6 rows a second display list shows in place of 16 lines of sky, so the
 * picture under them is never touched.
 *
 * The lander is player 0 (32 directions, tables.c), its flame player 1.
 * Stars and sparks are XORed into the bitmap (fx.s), so moving one never
 * needs the picture behind it.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "view.h"

/* ---- memory (the screen area $A000-$BFFF) */
#define BITMAP   ((uint8_t *)0xA010)   /* 176 lines x 40: line 102 is $B000 */
#define HUD_TOP  ((uint8_t *)0xBB90)   /* 20 */
#define HUD_BOT  ((uint8_t *)0xBBA4)   /* 20 */
#define MSG      ((uint8_t *)0xBBB8)   /* 2 x 20 */
#define FONT     ((uint8_t *)0xBC00)   /* 512: ANTIC 6 reaches 64 glyphs */
#define DL_GAME  ((uint8_t *)0xBE00)
#define DL_MSG   ((uint8_t *)0xBF00)   /* the same with the message rows */
#define TITLE    ((uint8_t *)0xA010)   /* the title's rows (the bitmap is free then) */
#define DL_TITLE ((uint8_t *)0xA800)
#define LINES    176
#define MSG_LINE 24                    /* the message rows cover lines 24..39 */

/* glyphs we put in place of punctuation (ANTIC 6 internal codes) */
#define G_BAR    0x01                  /* 0x01..0x09: a gauge cell 0..8 full */
#define G_LIFE   0x0A                  /* a lander, for the lives */

extern uint8_t dli_bk[64], dli_pf0[64], dli_pf1[64], dli_pf2[64], dli_pf3[64];

/* planet.c: the title's picture of Uranus (tools/gen_planet.py) */
extern const uint8_t planet_bits[54 * 10];
extern const uint8_t planet_pal[54][3];
#define PLANET   ((uint8_t *)0xB000)   /* 54 lines x 40 (the bitmap is free then) */
#define PLANET_LINES 54
void irq_install(void);
void __fastcall__ irq_title(uint8_t on);

/* fx.s */
extern uint8_t tline[160];             /* terrain top line of each pixel column */
extern uint8_t st_px[MAX_STARS], st_line[MAX_STARS];
extern uint8_t pt_px[MAX_PARTICLES], pt_line[MAX_PARTICLES];
void terrain_fill(void);
void stars_step(void);
void stars_erase(void);
void particles_draw(void);
void particles_erase(void);
void ply_put(void);                    /* player pp_n at pp_x, pp_line, shape pp_shape */
extern uint8_t pp_n, pp_x, pp_line, pp_h, pp_key;
extern const uint8_t *pp_shape;
extern uint8_t p_line[4], p_h[4], p_key[4];

/* tables.c */
extern const uint8_t ship_shape[32][16], flame_shape[32][16], flame_big_shape[32][16];

/* ---- colours */
#define C_YELLOW   A8_COLOR(1, 14)
#define C_WHITE    A8_COLOR(0, 14)
#define C_SLATE    A8_COLOR(9, 6)
#define C_GREEN    A8_COLOR(12, 10)
#define C_ORANGE   A8_COLOR(2, 10)
#define C_RED      A8_COLOR(4, 6)
#define C_CYAN     A8_COLOR(10, 12)
#define C_LTGRAY   A8_COLOR(0, 10)
#define C_PAD      A8_COLOR(1, 14)
#define C_MAGENTA  A8_COLOR(5, 10)

static uint8_t scode(char c)
{
    uint8_t a = (uint8_t)c;
    if (a < 0x20) return (uint8_t)(a + 0x40);
    if (a < 0x60) return (uint8_t)(a - 0x20);
    return a;
}

/* ANTIC 6: 20 columns, colour pf 0..3 in the top bits */
static void put6(uint8_t *dst, const char *s, uint8_t pf)
{
    while (*s) *dst++ = (uint8_t)((scode(*s++) & 0x3F) | (pf << 6));
}

static void num6(uint8_t *dst, uint32_t v, uint8_t digits, uint8_t pf)
{
    uint8_t i;
    for (i = digits; i; --i) {
        dst[i - 1] = (uint8_t)((0x10 + (uint8_t)(v % 10)) | (pf << 6));
        v /= 10;
    }
}

/* the same for numbers under 1000, by subtraction (cc65's 16-bit divide is
   ~400 cycles a digit; the HUD redraws numbers every frame) */
static void num6s(uint8_t *dst, uint16_t v, uint8_t digits, uint8_t pf)
{
    uint8_t h = 0, t = 0;
    pf = (uint8_t)((pf << 6) | 0x10);
    while (v >= 100) { v -= 100; ++h; }
    while (v >= 10) { v -= 10; ++t; }
    if (digits == 3) *dst++ = (uint8_t)(pf | h);
    *dst++ = (uint8_t)(pf | t);
    *dst = (uint8_t)(pf | (uint8_t)v);
}

/* ---- the font: the OS's first 64 glyphs and ours */
static void font_init(void)
{
    static const uint8_t life[8] = {0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0x24, 0x42, 0x00};
    uint8_t i, r;
    memcpy(FONT, (const void *)0xE000, 512);
    for (i = 0; i <= 8; ++i) {
        uint8_t *g = FONT + (G_BAR + i) * 8;
        uint8_t m = (uint8_t)(0xFF00 >> i);
        g[0] = 0xFF;
        g[7] = 0xFF;
        for (r = 1; r < 7; ++r) g[r] = (uint8_t)(m | 0x01);
    }
    memcpy(FONT + G_LIFE * 8, life, 8);
}

/* ---- display lists */
static uint8_t *dl_lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

/* the world's lines from..to-1; a DLI on every 8th */
static uint8_t *dl_world(uint8_t *d, uint8_t from, uint8_t to)
{
    uint8_t l, op;
    for (l = from; l < to; ++l) {
        op = (uint8_t)(0x0E | ((l & 7) == 7 ? 0x80 : 0));
        if (l == from || l == 102) d = dl_lms(d, (uint8_t)(op | 0x40), BITMAP + l * 40);
        else *d++ = op;
    }
    return d;
}

static void dl_build(void)
{
    uint8_t *d = DL_GAME;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_lms(d, 0xC6, HUD_TOP);           /* DLI 0: the world's colours */
    d = dl_world(d, 0, LINES);              /* DLIs 1..22 */
    d = dl_lms(d, 0x46, HUD_BOT);
    d = dl_lms(d, 0x41, DL_GAME);

    d = DL_MSG;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_lms(d, 0xC6, HUD_TOP);
    d = dl_world(d, 0, MSG_LINE);
    d = dl_lms(d, 0xC6, MSG);               /* 2 rows for 16 lines: */
    *d++ = 0x86;                            /* the same two DLIs */
    d = dl_world(d, MSG_LINE + 16, LINES);
    d = dl_lms(d, 0x46, HUD_BOT);
    d = dl_lms(d, 0x41, DL_MSG);
}

/* the DLI colours: DLI 0 starts the world, DLI k (1..21) is at the end of
   line 8k - 1, DLI 22 the bottom HUD */
static void dli_colours(void)
{
    /* the sky: black at the top, Uranus's blue-green at the horizon
       (lines ~100-180 are where the ground is, so the ground's strata come
       from the same DLIs: PF0 darkens with depth) */
    static const uint8_t sky[23] = {
        0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x82, 0x82, 0x92, 0x92, 0x84,
        0x94, 0x94, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x00,
    };
    /* the ground: grey (the original's), darker with depth */
    static const uint8_t ground[23] = {
        0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
        0x08, 0x08, 0x08, 0x06, 0x06, 0x06, 0x04, 0x04, 0x04, 0x02, 0x02, 0x00,
    };
    uint8_t k;
    for (k = 0; k < 23; ++k) {
        dli_bk[k] = sky[k];
        dli_pf0[k] = ground[k];             /* terrain body */
        dli_pf1[k] = C_LTGRAY;              /* surface line, stars */
        dli_pf2[k] = C_PAD;                 /* pads, labels, sparks */
        dli_pf3[k] = C_RED;                 /* (not in ANTIC E: the message rows) */
    }
    /* the bottom HUD: lives / altitude cyan, speeds green, yellow, red */
    dli_bk[22] = 0;
    dli_pf0[22] = C_GREEN;
    dli_pf1[22] = C_YELLOW;
    dli_pf2[22] = C_RED;
    dli_pf3[22] = C_CYAN;
}

static uint8_t msg_shown;

void view_msg(const char *a, const char *b, uint8_t pf)
{
    memset(MSG, 0, 40);
    if (a) put6(MSG + ((20 - strlen(a)) >> 1), a, pf);
    if (b) put6(MSG + 20 + ((20 - strlen(b)) >> 1), b, pf);
    SDLSTL = (uint16_t)(a || b ? DL_MSG : DL_GAME);
    msg_shown = a || b;
}

/* ---- the world */
/* the pad's multiplier under it, e.g. X2: the ROM font's glyphs in colour
   3 on the ground (1), byte-aligned: each font row is 2 bytes (a nibble a
   byte through a table) */
static const uint8_t nib3[16] = {      /* 4 bits -> 4 pixels: 1 = colour 3, 0 = 1 */
    0x55, 0x57, 0x5D, 0x5F, 0x75, 0x77, 0x7D, 0x7F,
    0xD5, 0xD7, 0xDD, 0xDF, 0xF5, 0xF7, 0xFD, 0xFF,
};

static void label(uint8_t px, uint8_t line, const char *s)
{
    uint8_t *row = BITMAP + line * 40 + (px >> 2);
    for (; *s; ++s, row += 2) {
        const uint8_t *g = (const uint8_t *)0xE000 + scode(*s) * 8;
        uint8_t r;
        uint8_t *p = row;
        for (r = 1; r < 7 && line + r < LINES; ++r) {
            p += 40;
            if (row + 1 - BITMAP < 40 * LINES) {
                p[0] = nib3[g[r] >> 4];
                p[1] = nib3[g[r] & 15];
            }
        }
    }
}

static const uint8_t or3[4] = {0xC0, 0x30, 0x0C, 0x03};

/* a horizontal line of colour 3 */
static void hline3(uint8_t x0, uint8_t x1, uint8_t line)
{
    uint8_t *p = BITMAP + line * 40;
    for (; x0 <= x1; ++x0) p[x0 >> 2] |= or3[x0 & 3];
}

static uint8_t wline(WORD y)            /* world y -> bitmap line (clipped) */
{
    WORD l = (WORD)((y + (y << 1)) >> 2) - 8;
    return (uint8_t)(l < 0 ? 0 : l > LINES - 1 ? LINES - 1 : l);
}

void view_world(void)
{
    uint8_t px, i;
    /* the top line of each pixel column: the higher of its two world columns */
    for (px = 0; px < 160; ++px) {
        WORD a = gs.terrain_y[px * 2], b = gs.terrain_y[px * 2 + 1];
        tline[px] = wline(a < b ? a : b);
    }
    memset(st_px, 0xFF, MAX_STARS);         /* no star drawn */
    for (i = 0; i < MAX_STARS; ++i) {       /* the stars' lines (they never change) */
        WORD l = (WORD)(((WORD)st_y[i] + (st_y[i] << 1)) >> 2) - 8;
        st_line[i] = (uint8_t)(l < 0 ? 0xFF : l);
    }
    memset(pt_px, 0xFF, MAX_PARTICLES);
    terrain_fill();                         /* sky, surface, highlight, ground */
    for (i = 0; i < (uint8_t)gs.num_pads; ++i) {
        LandingPad *p = &gs.pads[i];
        uint8_t x0 = (uint8_t)(p->x >> 1), x1 = (uint8_t)((p->x + p->width - 1) >> 1);
        uint8_t l = wline(p->y);
        char s[3];
        hline3(x0, x1, (uint8_t)(l - 1));   /* the pad: a yellow band, legs */
        hline3(x0, x1, l);
        hline3(x0, x0, (uint8_t)(l + 1));
        hline3(x0, x0, (uint8_t)(l + 2));
        hline3(x1, x1, (uint8_t)(l + 1));
        hline3(x1, x1, (uint8_t)(l + 2));
        s[0] = 'X';
        s[1] = (char)('0' + p->multiplier);
        s[2] = 0;
        label((uint8_t)(((x0 + x1) >> 1) - 8), (uint8_t)(l + 4), s);
    }
}

/* ---- HUD */
static uint8_t hud_turn;
static int32_t hud_score = -1;

void view_hud_reset(void)
{
    uint8_t i;
    memset(HUD_TOP, 0, 20);
    memset(HUD_BOT, 0, 20);
    put6(HUD_TOP + 8, "F", 2);
    put6(HUD_TOP + 17, "L", 1);
    put6(HUD_BOT + 4, "A", 3);
    for (i = 0; i < 7; ++i) HUD_TOP[9 + i] = (uint8_t)(0xC0 | G_BAR);
    hud_score = -1;
    hud_turn = 0;
}

/* the 16.16 values' integer (rounded, the original's FIX_INT) and Q8 parts,
   read as bytes: cc65's 32-bit shifts cost hundreds of cycles */
static WORD round16(const Fixed *v)
{
    const uint8_t *b = (const uint8_t *)v;
    WORD w = *(const int16_t *)(b + 2);
    return b[1] & 0x80 ? w + 1 : w;
}
#define Q8(v) (*(const int16_t *)((const uint8_t *)&(v) + 1))   /* v >> 8 */

/* sideways / vertical speed as the original shows it (v >> 10), its colour
   by the landing limits: safe (green), up to 1.5 x (yellow), more (red)
   (compared in 1/256 pixels: the colour only) */
#define SAFE_VX8 ((WORD)(SAFE_VX_DEFAULT >> 8))
#define SAFE_VY8 ((WORD)(SAFE_VY_DEFAULT >> 8))

static void speed6(uint8_t *dst, WORD d, WORD sf, char label)
{
    WORD a = d < 0 ? -d : d;
    uint8_t pf = a < sf ? 0 : a < sf + (sf >> 1) ? 1 : 2;
    d >>= 2;                                             /* the original's v >> 10 */
    dst[-1] = (uint8_t)((scode(label) & 0x3F) | (pf << 6));
    dst[0] = (uint8_t)((d < 0 ? 0x0D : 0x0B) | (pf << 6));      /* - or + */
    if (d < 0) d = -d;
    if (d > 99) d = 99;
    num6s(dst + 1, (uint16_t)d, 2, pf);
}

/* one instrument a frame (only what changed is redrawn) */
static uint16_t hud_fm;
static uint8_t hud_k;                   /* 56 * 256 / fuel_max: gauge pixels per fuel, Q8 */

void view_hud(void)
{
    static uint8_t i, px, c;
    static WORD f, sx, sy, alt;
    switch (hud_turn++ & 3) {
    case 0:
        if (gs.score != hud_score) {
            hud_score = gs.score;
            num6(HUD_TOP, (uint32_t)gs.score, 7, 0);
        }
        num6s(HUD_TOP + 18, (uint16_t)gs.level, 2, 1);
        break;
    case 1:
        /* the fuel gauge: 7 cells of 8, coloured by what is left (the
           original: green over 50%, yellow over 25%, then red) */
        if (hud_fm != (uint16_t)g_tune.fuel_max) {
            hud_fm = (uint16_t)g_tune.fuel_max;
            hud_k = (uint8_t)((56U * 256U + hud_fm - 1) / hud_fm);   /* (rounded up: full is 56) */
        }
        f = gs.ship.fuel > 0 ? gs.ship.fuel : 0;
        px = (uint8_t)(((uint16_t)f * hud_k) >> 8);    /* (fuel <= 600: 16 bits) */
        COLOR3 = (uint16_t)f * 2 > hud_fm ? C_GREEN : (uint16_t)f * 4 > hud_fm ? C_YELLOW : C_RED;
        for (i = 0; i < 7; ++i) {
            c = px > i * 8 ? (uint8_t)(px - i * 8) : 0;
            HUD_TOP[9 + i] = (uint8_t)(0xC0 | (G_BAR + (c > 8 ? 8 : c)));
        }
        break;
    case 2:
        sx = round16(&gs.ship.x);
        sy = round16(&gs.ship.y);
        alt = 0;
        for (i = 0; i < 3; ++i)
            HUD_BOT[i] = (uint8_t)(i < gs.lives ? (0xC0 | G_LIFE) : 0);
        if (sx >= 0 && sx < TERRAIN_W) alt = gs.terrain_y[sx] - sy - 6;
        if (alt < 0) alt = 0;
        if (alt > 999) alt = 999;
        num6s(HUD_BOT + 5, (uint16_t)alt, 3, 3);
        break;
    default:
        speed6(HUD_BOT + 10, Q8(gs.ship.vx), SAFE_VX8, 'H');
        speed6(HUD_BOT + 15, Q8(gs.ship.vy), SAFE_VY8, 'V');
        break;
    }
}

void view_autopilot(uint8_t on)
{
    HUD_BOT[19] = on ? (uint8_t)((scode('P') & 0x3F) | 0xC0) : 0;
}

/* ---- the lander */
#define PCOLR_(n, c) (*(uint8_t *)(0x2C0 + (n)) = (c))

static void hide(uint8_t n)
{
    pp_n = n;
    pp_x = 0;
    pp_h = 0;
    ply_put();
}

void view_ship(void)
{
    WORD sx, sy;
    uint8_t d, line;
    if (!gs.ship.alive ||
        (gs.state == STATE_PLAYING && gs.state_timer > 0 && (gs.frame & 4))) {
        hide(0);
        hide(1);
        return;
    }
    sx = round16(&gs.ship.x);
    sy = round16(&gs.ship.y);
    if (sy < -30 || sy > SCREEN_H + 10) { hide(0); hide(1); return; }
    d = (uint8_t)(((gs.ship.angle + 4) >> 3) & 31);
    line = (uint8_t)(32 + (((sy + 40) * 3) >> 2) - 30 - 7);   /* PM line of the shape's top */
    pp_x = (uint8_t)(48 + (sx >> 1) - 4);
    pp_line = line;
    pp_h = 16;
    pp_n = 0;
    pp_shape = ship_shape[d];
    pp_key = (uint8_t)(1 + d);
    ply_put();
    pp_n = 1;
    if (gs.ship.thrusting && gs.ship.fuel > 0) {
        uint8_t big = (gs.frame & 2) != 0;
        pp_shape = big ? flame_big_shape[d] : flame_shape[d];
        pp_key = (uint8_t)(64 + d + (big ? 32 : 0));
        PCOLR_(1, (gs.frame & 4) ? C_ORANGE : C_YELLOW);
        ply_put();
    } else {
        hide(1);
    }
}

void view_frame(void)
{
    view_ship();
    stars_step();
    if (pt_live || gs.state == STATE_CRASHING) particles_draw();
    view_hud();
}

/* ---- screens */
void view_game(void)
{
    hide(0);
    hide(1);
    irq_title(0);
    dli_colours();                         /* (the title's are the planet's) */
    view_hud_reset();
    view_msg(0, 0, 0);                     /* (the title's list is in the bitmap) */
    view_world();
    COLOR0 = C_YELLOW;                     /* the top HUD: score */
    COLOR1 = C_WHITE;                      /* level */
    COLOR2 = C_SLATE;                      /* F */
    COLOR3 = C_GREEN;                      /* the gauge */
    COLOR4 = 0;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    view_msg(0, 0, 0);
}

/* the title: big letters, the high scores, the controls */
static uint8_t *trow, *press_row;

static uint8_t *title_line(uint8_t *d, uint8_t mode, const char *s, uint8_t pf)
{
    uint8_t w = (mode & 15) == 7 || (mode & 15) == 6 ? 20 : 40;
    memset(trow, 0, w);
    if (s) put6(trow + ((w - strlen(s)) >> 1), s, pf);
    d = dl_lms(d, (uint8_t)(0x40 | mode), trow);
    trow += w;
    return d;
}

void view_title(const char *const *names, const uint32_t *scores)
{
    uint8_t *d = DL_TITLE;
    uint8_t i;
    char line[21];
    hide(0);
    hide(1);
    trow = TITLE;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = title_line(d, 7, "URANUS", 1);
    d = title_line(d, 0x87, "LANDER", 0);   /* the DLI that colours the planet */
    /* the planet: 54 lines of ANTIC E, three colours of its own each (the
       title's DLI, irq.s); its 10 bytes in the middle of 40 */
    memset(PLANET, 0, PLANET_LINES * 40);
    for (i = 0; i < PLANET_LINES; ++i) {
        memcpy(PLANET + i * 40 + 15, planet_bits + i * 10, 10);
        dli_bk[i] = 0;
        dli_pf0[i] = planet_pal[i][0];
        dli_pf1[i] = planet_pal[i][1];
        dli_pf2[i] = planet_pal[i][2];
        dli_pf3[i] = C_CYAN;
        if (i == 0) d = dl_lms(d, 0x4E, PLANET);
        else *d++ = 0x0E;
    }
    dli_bk[i] = 0;                          /* after it: the text colours (irq.s) */
    dli_pf0[i] = C_YELLOW;
    dli_pf1[i] = C_WHITE;
    dli_pf2[i] = A8_COLOR(0, 8);
    dli_pf3[i] = C_CYAN;
    d = title_line(d, 6, "TOP SCORES", 3);
    *d++ = 0x30;
    for (i = 0; i < 5; ++i) {
        uint32_t v = scores[i];
        uint8_t k;
        line[0] = (char)('1' + i);
        line[1] = '.';
        line[2] = ' ';
        memcpy(line + 3, names[i], 3);
        line[6] = ' ';
        for (k = 12; k > 6; --k) { line[k] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
        line[13] = 0;
        d = title_line(d, 6, line, i < 3 ? 0 : 2);
    }
    d = title_line(d, 6, "STICK  ROTATE", 2);
    d = title_line(d, 6, "FIRE/UP  THRUST", 2);
    d = title_line(d, 6, "OPTION  AUTOPILOT", 2);
    d = title_line(d, 6, "M  MUSIC ON/OFF", 2);
    *d++ = 0x70;
    press_row = trow;
    d = title_line(d, 6, "PRESS FIRE", 0);
    d = dl_lms(d, 0x41, DL_TITLE);
    COLOR0 = C_YELLOW;
    COLOR1 = C_WHITE;
    COLOR2 = A8_COLOR(0, 8);
    COLOR3 = C_CYAN;
    COLOR4 = 0;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    irq_title(1);
    SDLSTL = (uint16_t)DL_TITLE;
}

/* the title's blinking prompt and URANUS's flicker (the original cycles it) */
void view_title_frame(uint8_t t)
{
    static const uint8_t flick[4] = {C_CYAN, C_WHITE, C_CYAN, A8_COLOR(10, 6)};
    COLOR1 = flick[(t >> 3) & 3];
    if ((t & 31) == 0) {                   /* PRESS FIRE blinks */
        memset(press_row, 0, 20);
        if (t & 32) put6(press_row + 5, "PRESS FIRE", 0);
    }
}

void view_init(void)
{
    uint8_t t;
    /* the OS's screen and display list are in $BC20-$BFFF, where our font
       and lists go: screen DMA off for a frame first (ANTIC must not run
       a half-overwritten list) */
    SDMCTL = 0;
    t = RTCLOK2;
    while (RTCLOK2 == t)
        ;
    font_init();
    dl_build();
    dli_colours();
    SDLSTL = (uint16_t)DL_GAME;
    SDMCTL = 0x22;                         /* normal playfield, display list DMA */
    pmg_init();
    GPRIOR = 1;                            /* players over playfield */
    PCOLR0 = C_CYAN;
    memset(p_key, 0, 4);
    memset(p_h, 0, 4);
    irq_install();
}
