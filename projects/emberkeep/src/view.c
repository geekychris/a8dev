/*
 * Emberkeep: the screen (the original's render.cpp).
 *
 * The chamber: ANTIC E, 160 x 180 (the original's 320 x 240 at x / 2,
 * y * 3 / 4: a tile 8 x 12), 4 colours: black, grey stone, brick red and
 * each tile row's accent (a DLI: the rune's yellow, the oil's orange, the
 * portal's green). The whole chamber is drawn once into an image (lamp.s:
 * in a 130XE bank, or in main memory on a 64K machine); the screen shows
 * only the lantern's light: the original's square (shrinking from 40 to 8
 * pixels as the oil burns, its corners chopped), the rest black. A frame
 * writes only the bytes at the light's edges that changed.
 *
 * The player is players 0 + 1 (its red and the rest), the enemies in the
 * light players 2 and 3, clipped to it line by line and pixel by pixel
 * (the original draws them under its black mask). The HUD: score, chamber,
 * lives, runes, the lantern's fuel bar.
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8xe.h"
#include "game.h"
#include "view.h"

#define FIELD    ((uint8_t *)0xA010)        /* 180 lines x 40 */
#define HUD      ((uint8_t *)0xBC40)        /* ANTIC 6: 20 */
#define BAR      ((uint8_t *)0xBC54)        /* ANTIC E: 40 */
#define DL_GAME  ((uint8_t *)0xBD00)
#define DL_TEXT  ((uint8_t *)0xBE00)
#define TEXT     ((uint8_t *)0xA010)        /* the title's rows (20 bytes each) */
#define LINES    180

extern uint8_t dli_pf0[16], dli_pf1[16], dli_pf2[16], dli_pf3[16];
void irq_install(void);
extern uint8_t lr[7], lr_dirty[15];         /* the lit square (lamp.s), changed tile rows */
extern uint8_t *lp_img;
extern uint8_t lp_bank, lt_col, lt_row, lt_tile;
void lamp_update(void);
void lamp_tile(void);
void lamp_reset(void);
void lamp_chamber(void);
extern uint8_t lc_open;
extern const uint8_t pl_gfx0[], pl_gfx1[], en_gfx[];

enum { G_FLOOR, G_WALL_A, G_WALL_B, G_RUNE, G_OIL, G_HAZARD, G_PORTAL_LOCKED, G_PORTAL_OPEN };

/* colours (the original's palette, near enough) */
#define C_GREY    0x06
#define C_BRICK   0x34
#define C_YELLOW  0xEC
#define C_ORANGE  0x2A
#define C_GREEN   0xC8

uint8_t view_banks;
static uint8_t img_main[LINES * 40];        /* the image on a 64K machine */
static uint8_t portal_open;

/* ---- the chamber's image */
static uint8_t tile_gfx_of(uint8_t row, uint8_t col)
{
    switch (grid[row][col]) {
    case T_WALL: return ((row + col) & 1) ? G_WALL_A : G_WALL_B;
    case T_RUNE: return G_RUNE;
    case T_OIL: return G_OIL;
    case T_HAZARD: return G_HAZARD;
    case T_PORTAL: return portal_open ? G_PORTAL_OPEN : G_PORTAL_LOCKED;
    }
    return G_FLOOR;
}

/* the accent of tile row r: the open portal's green, a rune's yellow, an
   oil flask's orange */
static void row_accent(uint8_t r)
{
    static const uint8_t *g;
    static uint8_t c, a, rune;
    g = grid[r];
    a = C_YELLOW;
    rune = 0;
    for (c = GRID_W; c; --c) {
        switch (*g++) {
        case T_PORTAL: if (portal_open) { dli_pf2[r] = C_GREEN; return; } break;
        case T_OIL: a = C_ORANGE; break;
        case T_RUNE: rune = 1; break;
        }
    }
    dli_pf2[r] = rune ? C_YELLOW : a;
}

static void row_colours(void)
{
    uint8_t r;
    for (r = 0; r < GRID_H; ++r) row_accent(r);
}

static uint8_t portal_r = 0xFF, portal_c;

static void redraw_tile(uint8_t row, uint8_t col)
{
    lt_row = row;
    lt_col = col;
    lt_tile = tile_gfx_of(row, col);
    lamp_tile();
    lr_dirty[row] = 1;                      /* its lit lines recopied */
}

void view_level(void)
{
    uint8_t r, c;
    if (view_banks) {
        lp_bank = (uint8_t)(gs.level_index >> 1);
        lp_img = (uint8_t *)((gs.level_index & 1) ? 0x6000 : 0x4000);
    } else {
        lp_bank = 0xFF;
        lp_img = img_main;
    }
    portal_open = gs.runes_remaining == 0;
    portal_r = 0xFF;
    for (r = 0; r < GRID_H; ++r)
        for (c = 0; c < GRID_W; ++c)
            if (grid[r][c] == T_PORTAL) { portal_r = r; portal_c = c; }
    lc_open = portal_open;
    lamp_chamber();
    row_colours();
    memset(FIELD, 0, LINES * 40);
    lamp_reset();
}

/* ---- the light: the original's square, its corners chopped */
static uint8_t lt_x0, lt_x1, lt_y0, lt_y1;  /* this frame's (Atari), for the enemies */
static int16_t last_lantern = -1, r, chop;

static void light(void)
{
    static int16_t cx, cy, lx, rx, ty, by;
    if (gs.lantern != last_lantern) {        /* the radius: 40 .. 8 */
        last_lantern = gs.lantern;
        r = 8 + (int16_t)(((uint16_t)gs.lantern * 32) / LANTERN_MAX);
        chop = r / 3;
    }
    cx = gs.px + 8;
    cy = gs.py + 8;
    lx = cx - r; if (lx < 0) lx = 0;
    rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    ty = cy - r; if (ty < 0) ty = 0;
    by = cy + r; if (by > PLAY_H) by = PLAY_H;
    lr[0] = lt_x0 = (uint8_t)(lx >> 1);
    lr[1] = lt_x1 = (uint8_t)(rx >> 1);
    lr[2] = lt_y0 = (uint8_t)((ty * 3) >> 2);
    lr[3] = lt_y1 = (uint8_t)((by * 3) >> 2);
    lr[4] = (uint8_t)(chop >> 1);
    lr[5] = (uint8_t)((chop * 3) >> 2);
    /* the corners cut where the square isn't at the screen's edge */
    lr[6] = 0;
    if (chop > 0) {
        if (ty > 0 && lx > 0) lr[6] |= 1;
        if (ty > 0 && rx < PLAY_W) lr[6] |= 2;
        if (by < PLAY_H && lx > 0) lr[6] |= 4;
        if (by < PLAY_H && rx < PLAY_W) lr[6] |= 8;
    }
    lamp_update();
}

/* ---- players and missiles */
#define PM(n) ((uint8_t *)(0x9C00 + ((n) << 8)))
static uint8_t pm_line[4], pm_on[4];

static void pm_clear(uint8_t n)
{
    if (pm_on[n]) memset(PM(n) + pm_line[n], 0, 12);
    pm_on[n] = 0;
}

static const uint8_t en_colour[3] = {0x9A, 0x58, 0x08};    /* cyan, magenta, grey */

static void sprites(void)
{
    static uint8_t i, slot, line, ex, ey, j, j0, j1, m, k, f, *d;
    static const uint8_t *src;
    /* the player: its pose by facing (the original draws no walk frames) */
    line = (uint8_t)(32 + ((gs.py * 3) >> 2));
    for (k = 0; k < 2; ++k) {
        pm_clear(k);
        src = (k ? pl_gfx1 : pl_gfx0) + gs.facing * 12;
        memcpy(PM(k) + line, src, 12);
        pm_line[k] = line;
        pm_on[k] = 1;
    }
    *(uint8_t *)0xD000 = *(uint8_t *)0xD001 = (uint8_t)(48 + (gs.px >> 1));
    /* the enemies in the light: players 2, 3, clipped to it: the lines
       inside it, the pixels (bits) inside it */
    slot = 2;
    pm_clear(2);
    pm_clear(3);
    for (i = 0; i < gs.num_enemies && slot < 4; ++i) {
        if (!e_alive[i]) continue;
        ex = (uint8_t)(e_x[i] >> 1);
        ey = (uint8_t)((e_y[i] * 3) >> 2);
        if (ex + 8 <= lt_x0 || ex >= lt_x1 || ey + 12 <= lt_y0 || ey >= lt_y1) continue;
        m = (uint8_t)((ex >= lt_x0 ? 0xFF : (0xFF >> (lt_x0 - ex))) &
                      (ex + 8 <= lt_x1 ? 0xFF : (uint8_t)(0xFF << (ex + 8 - lt_x1))));
        k = (uint8_t)(e_colour[i] - 4);
        if (k > 2) k = 0;
        f = (uint8_t)((e_frame[i] >> 2) & 7);
        src = en_gfx + (k * 8 + f) * 12;
        d = PM(slot) + 32 + ey;
        j0 = ey < lt_y0 ? lt_y0 - ey : 0;
        j1 = ey + 12 > lt_y1 ? lt_y1 - ey : 12;
        memset(d, 0, 12);
        for (j = j0; j < j1; ++j) d[j] = src[j] & m;
        pm_line[slot] = (uint8_t)(32 + ey);
        pm_on[slot] = 1;
        *(uint8_t *)(0xD000 + slot) = (uint8_t)(48 + ex);
        *(uint8_t *)(0x2C0 + slot) = en_colour[k];
        ++slot;
    }
}

/* ---- the HUD */
static uint32_t hud_score = 0xFFFFFFFF;
static uint8_t hud_misc, hud_bar = 0xFF;
static int16_t hud_lantern = -1;
static const char *hud_msg;

static uint8_t scode6(char c)
{
    uint8_t a = (uint8_t)c;
    if (a >= 0x20 && a < 0x60) a -= 0x20;
    return (uint8_t)((a & 0x3F) | 0xC0);    /* PF3: white */
}

static void hud_put(uint8_t col, const char *s)
{
    uint8_t *d = HUD + col;
    while (*s && d < HUD + 20) *d++ = scode6(*s++);
}

void view_hud_msg(const char *s)
{
    hud_msg = s;
    hud_score = 0xFFFFFFFF;
}

static void view_hud(void)
{
    char b[12];
    uint8_t misc = (uint8_t)(gs.level_index << 6 | gs.lives << 4 | (gs.runes_total - gs.runes_remaining));
    uint8_t bar;
    if (hud_score != gs.score || hud_misc != misc) {
        hud_score = gs.score;
        hud_misc = misc;
        memset(HUD, 0, 20);
        if (hud_msg) {
            hud_put((uint8_t)((20 - strlen(hud_msg)) >> 1), hud_msg);
        } else {
            ultoa(gs.score + 1000000UL, b, 10);  /* 1nnnnnn: 6 digits */
            hud_put(0, "SC");
            hud_put(2, b + 1);
            b[0] = 'L';
            b[1] = 'V';
            b[2] = (char)('1' + gs.level_index);
            b[3] = 0;
            hud_put(9, b);
            b[0] = 'R';
            b[1] = (char)('0' + gs.runes_total - gs.runes_remaining);
            b[2] = '/';
            b[3] = (char)('0' + gs.runes_total);
            b[4] = 0;
            hud_put(13, b);
            b[0] = 'L';
            b[1] = (char)('0' + gs.lives);
            b[2] = 0;
            hud_put(18, b);
        }
    }
    /* the fuel bar: 60 pixels from pixel 50, orange (red under a quarter)
       on dark grey */
    if (gs.lantern == hud_lantern) return;
    hud_lantern = gs.lantern;
    bar = (uint8_t)(((uint16_t)gs.lantern * 60) / LANTERN_MAX);
    if (gs.lantern < LANTERN_MAX / 4) bar |= 0x80;   /* (red) */
    if (bar != hud_bar) {
        static const uint8_t head[4] = {0x00, 0xC0, 0xF0, 0xFC};   /* n pixels of a byte */
        uint8_t fill = (bar & 0x80) ? 0xFF : 0xAA, n = bar & 0x7F, full = n >> 2;
        hud_bar = bar;
        memset(BAR + 12, 0x55, 15);         /* the dark grey */
        memset(BAR + 12, fill, full);
        if (n & 3) BAR[12 + full] = (uint8_t)((fill & head[n & 3]) | (0x55 & ~head[n & 3]));
    }
}

void view_frame(void)
{
    /* a pickup changed a tile; the portal opens (and its glow flickers,
       the original's tick / 6) */
    if (gs.changed_row != 0xFF) {
        redraw_tile(gs.changed_row, gs.changed_col);
        row_accent(gs.changed_row);
        gs.changed_row = 0xFF;
    }
    if (gs.runes_remaining == 0 && portal_r != 0xFF) {
        uint8_t open = (uint8_t)((gs.tick / 6) & 1);       /* the original's flicker */
        if (open != portal_open) {
            portal_open = open;
            redraw_tile(portal_r, portal_c);
            row_accent(portal_r);
        }
    }
    light();
    sprites();
    view_hud();
}

/* ---- the display lists */
static uint8_t *lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

static void build_game_dl(void)
{
    uint8_t *d = DL_GAME, l;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0xF0;                            /* DLI 0: row 0's colours */
    for (l = 0; l < LINES; ++l) {
        uint8_t op = 0x0E;
        if (l == 0) op = 0x4E;
        if (l == 102) op = 0x4E;            /* (4K: $B000) */
        if (l % 12 == 11) op |= 0x80;       /* the next row's (or the HUD's) */
        if (op & 0x40) d = lms(d, op, FIELD + l * 40);
        else *d++ = op;
    }
    d = lms(d, 0x46, HUD);
    d = lms(d, 0x4E, BAR);
    d = lms(d, 0x4E, BAR);
    d = lms(d, 0x4E, BAR);
    d = lms(d, 0x4E, BAR);
    d = lms(d, 0x41, DL_GAME);
}

void view_game_screen(void)
{
    uint8_t i;
    SDMCTL = 0;
    frame_wait();
    build_game_dl();
    for (i = 0; i < 15; ++i) {
        dli_pf0[i] = C_GREY;
        dli_pf1[i] = C_BRICK;
        dli_pf3[i] = 0;
    }
    /* the HUD: dark grey, orange, red; white text */
    dli_pf0[15] = 0x04;
    dli_pf1[15] = C_ORANGE;
    dli_pf2[15] = 0x34;
    dli_pf3[15] = 0x0E;
    hud_score = 0xFFFFFFFF;
    hud_bar = 0xFF;
    hud_lantern = -1;
    SDLSTL = (uint16_t)DL_GAME;
    SDMCTL = 0x3E;                          /* + PMG, single line */
}

/* ---- text screens (the title, the end): ANTIC 6 / 7 rows in TEXT */
#define TROWS 22
static uint8_t trow_mode[TROWS];

void view_text(uint8_t row, const char *a)
{
    uint8_t *d = TEXT + row * 20;
    memset(d, 0, 20);
    if (a) {
        uint8_t n = (uint8_t)strlen(a);
        d += (20 - n) >> 1;
        while (*a) {
            char c = *a++;
            uint8_t col = 0;
            /* lower case letters: PF1 (the other colour) */
            if (c >= 'a' && c <= 'z') { c = (char)(c - 32); col = 0x40; }
            *d++ = (uint8_t)(((uint8_t)c - 0x20) & 0x3F) | col;
        }
    }
}

static void build_text_dl(void)
{
    uint8_t *d = DL_TEXT, i;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    for (i = 0; i < TROWS; ++i) d = lms(d, trow_mode[i], TEXT + i * 20);
    d = lms(d, 0x41, DL_TEXT);
}

static void text_screen(void)
{
    uint8_t i;
    SDMCTL = 0;
    frame_wait();
    for (i = 0; i < 4; ++i) {
        *(uint8_t *)(0xD000 + i) = 0;
        pm_clear(i);
    }
    memset(TEXT, 0, TROWS * 20);
    for (i = 0; i < TROWS; ++i) trow_mode[i] = 0x46;
    trow_mode[3] = 0x47;                    /* the name: double height */
    build_text_dl();
    for (i = 0; i < 16; ++i) {
        dli_pf0[i] = 0x0E;
        dli_pf1[i] = C_ORANGE;
        dli_pf2[i] = 0x84;
        dli_pf3[i] = 0;
    }
    SDLSTL = (uint16_t)DL_TEXT;
    SDMCTL = 0x22;
}

void view_title(const char (*names)[4], const uint32_t *scores, uint8_t blink)
{
    uint8_t i;
    char line[21];
    static uint8_t shown;
    if (!shown || blink == 0xFF) {
        text_screen();
        shown = 1;
        view_text(3, "EMBERKEEP");
        view_text(6, "a lantern in the");
        view_text(7, "deep dark");
        view_text(9, "HIGH SCORES");
        for (i = 0; i < 5; ++i) {
            memcpy(line, "1 ABC 000000", 13);
            line[0] = (char)('1' + i);
            memcpy(line + 2, names[i], 3);
            ultoa(scores[i] + 1000000UL, line + 6, 10);
            memmove(line + 6, line + 7, 7);
            view_text((uint8_t)(10 + i), line);
        }
        view_text(17, "stick: walk");
        view_text(18, "take the runes, mind");
        view_text(19, "the oil");
    }
    if (blink != 0xFF) view_text(21, (blink & 1) ? "PRESS FIRE TO BEGIN" : 0);
}

void view_end(uint8_t win, uint32_t score, uint8_t blink)
{
    char line[21];
    if (blink == 0xFF) {
        text_screen();
        view_text(3, win ? "DARK BEATEN" : "GAME OVER");
        view_text(6, win ? "the dark is beaten" : 0);
        memcpy(line, "FINAL SCORE ", 12);
        ultoa(score + 1000000UL, line + 12, 10);
        memmove(line + 12, line + 13, 7);
        view_text(9, line);
        return;
    }
    view_text(14, (blink & 1) ? "PRESS FIRE" : 0);
}

void view_init(void)
{
    SDMCTL = 0;                             /* off a frame: the OS screen is in our way */
    frame_wait();
    view_banks = xe_bank_count() == 4;
    memset((void *)0xA000, 0, 0x2000);
    memset((void *)0x9B00, 0, 0x500);
    PMBASE = 0x98;
    GRACTL = 3;
    GPRIOR = 0x01;                          /* players over the chamber */
    PCOLR0 = 0x34;                          /* the player's red */
    *(uint8_t *)0x2C1 = 0x2C;               /* skin, hair, boots */
    COLOR4 = 0;
    CHBAS = 0xE0;
    irq_install();
}
