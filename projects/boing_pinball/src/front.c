/*
 * front.c - Boing Pinball's front end for the Atari 130XE: the display,
 * the controls, the sounds, the attract mode, high scores.
 *
 * The table is ANTIC mode 4 characters (tools/gen_gfx.py), 48 rows, a
 * screen and a half: the screen scrolls (finely, VSCROL) to follow the
 * ball. Every row has its own colours and font (the DLI: disp.s). The
 * Boing ball is two players (its red and white squares, 8 frames of
 * spin), more balls in multiball players 2 and 3; the flippers are drawn
 * into characters of their own (8 frames each, copied from bank 3 as they
 * move); lamps are characters whose inverse bit lights them (PF3 for
 * PF2); drop targets swap characters. The dot-matrix display is two text
 * rows at the top.
 *
 * The simulation (the original's rules and physics, pbz.h) steps 25 times
 * a second: every second frame, more steps when behind (at most 2 a frame).
 *
 * Controls: joystick left / right the flippers (FIRE both); down pulls the
 * plunger, let go to launch; up nudges. Keys: Z / M (or SHIFT) flippers,
 * SPACE plunger, N nudge. START starts a game; on the title SELECT shows
 * the high scores.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8xe.h"
#include "a8log.h"
#include "pb.h"
#include "gfx_data.h"

/* everything the banked code reads must be outside the bank window
   $4000-$7FFF (the front end's own memory is inside it): under the ROM */
#pragma bss-name (push, "HIBSS")
Game G;
static Bot bot;
static PbInput in;
static uint16_t fc_dsttab[2][FLIP_CELLS];

#pragma bss-name (pop)

#define MAP        ((uint8_t *)0x8000)
#define ROMFONT    ((uint8_t *)0x8800)
#define FONTS      ((uint8_t *)0xA000)
#define DLIST      ((uint8_t *)0xBC00)
#define DMD_BIG    ((uint8_t *)0xBD00)       /* 20 characters, mode 6 */
#define DMD_SMALL  ((uint8_t *)0xBD20)       /* 40 characters, mode 2 */
#define VIS_ROWS   22                        /* table rows on screen */
#define TABLE_Y0   48                        /* the table's first scan line */

extern void disp_start(void);
extern uint8_t scroll_row, scroll_fine;
extern uint8_t *dl_lms;
extern uint8_t t_bk[], t_pf0[], t_pf1[], t_pf2[], t_pf3[], t_font[];
extern const uint8_t *fc_src;
extern uint16_t *fc_dst;
extern uint8_t fc_n;
extern void flip_copy(void);
extern const uint8_t g_fonts[], g_map[], g_flips[];

enum { M_ATTRACT, M_PLAY, M_OVER, M_ENTRY };
static int32_t last_score;
static int8_t last_ball;
static uint8_t mode, blink_t, chase;
static uint16_t mode_t, scroll_px;
static uint8_t flip_frame[2] = { 0xFF, 0xFF };
static uint8_t lamp_shown[29], drop_shown[8];
static uint8_t ball_spr[8][2][16];           /* the Boing ball: frame, red/white, lines */
static uint8_t spin;
uint16_t ticks, ticks_late;                  /* steps run; steps that ran late (the tests read them) */
/* for the tests: the first ball and the game, after each step: x, y, vx,
   vy (P, V), state, the ball's number, the game's state, the balls */
int16_t probe[8];

/* ------------------------------------------------------------ text */
static uint8_t scode(char ch)
{
    uint8_t c = (uint8_t)ch;
    if (c < 0x20) return (uint8_t)(c + 0x40);
    if (c < 0x60) return (uint8_t)(c - 0x20);
    return c;
}

static void dmd_text(const char *big, const char *small)
{
    static char shown_big[24], shown_small[44];
    uint8_t i, n;
    if (!strcmp(big, shown_big) && !strcmp(small, shown_small)) return;   /* unchanged */
    strncpy(shown_big, big, 23);
    strncpy(shown_small, small, 43);
    memset(DMD_BIG, 0, 20);
    memset(DMD_SMALL, 0, 40);
    n = (uint8_t)strlen(big);
    if (n > 20) n = 20;
    for (i = 0; i < n; ++i) DMD_BIG[(20 - n) / 2 + i] = scode(big[i]);
    n = (uint8_t)strlen(small);
    if (n > 40) n = 40;
    for (i = 0; i < n; ++i) DMD_SMALL[(40 - n) / 2 + i] = scode(small[i]);
}

/* ------------------------------------------------------------ high scores */
typedef struct { char name[4]; int32_t score; } Hs;
static Hs hs[5] = { { "AMI", 200000L }, { "BOI", 100000L }, { "NGA", 50000L }, { "PIN", 20000L }, { "BAL", 5000L } };
static uint8_t ed_place, ed_pos;
static char ed_name[4];

/* ------------------------------------------------------------ the display */
static void display_init(void)
{
    uint8_t *d = DLIST, r;
    uint16_t a;
    SDMCTL = 0;
    /* the ROM font for the text (os_off copied it to $B800, where a table
       font goes now), the table's fonts and map from bank 3 */
    memcpy(ROMFONT, (void *)0xB800, 1024);
    xe_bank_read(3, (uint16_t)g_fonts - 0x4000, FONTS, FONT_BYTES);
    xe_bank_read(3, (uint16_t)g_map - 0x4000, MAP, TROWS * 40);
    CHBAS = (uint8_t)((uint16_t)ROMFONT >> 8);
    for (r = 0; r < TROWS; ++r) {
        t_bk[r] = row_col[r][0];
        t_pf0[r] = row_col[r][1];
        t_pf1[r] = row_col[r][2];
        t_pf2[r] = row_col[r][3];
        t_pf3[r] = row_col[r][4];
        t_font[r] = (uint8_t)(((uint16_t)FONTS >> 8) + row_font[r] * 4);
    }
    t_bk[TROWS] = t_bk[TROWS + 1] = 0;
    /* 24 blank lines, the display (mode 6 + mode 2), the table: 22 mode 4
       rows scrolled + one more, each with the DLI */
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x46;
    *d++ = (uint8_t)(uint16_t)DMD_BIG;
    *d++ = (uint8_t)((uint16_t)DMD_BIG >> 8);
    *d++ = 0xC2;                             /* mode 2 + LMS, DLI: the table's first row */
    *d++ = (uint8_t)(uint16_t)DMD_SMALL;
    *d++ = (uint8_t)((uint16_t)DMD_SMALL >> 8);
    *d++ = 0xE4;                             /* mode 4 + LMS + VSCROL + DLI */
    dl_lms = d;
    *d++ = (uint8_t)(uint16_t)MAP;
    *d++ = (uint8_t)((uint16_t)MAP >> 8);
    for (r = 1; r < VIS_ROWS; ++r) *d++ = 0xA4;
    *d++ = 0x84;                             /* the last, partly shown */
    *d++ = 0x41;
    a = (uint16_t)DLIST;
    *d++ = (uint8_t)a;
    *d = (uint8_t)(a >> 8);
    /* the DMD row's colours: DMD_SMALL uses the mode 2 pair; mode 6 PF0 */
    COLOR0 = 0x2A;                           /* the big letters: orange */
    COLOR1 = 0x2C;                           /* mode 2: letters */
    COLOR2 = 0x20;                           /* mode 2: background */
    COLOR4 = 0x00;
    SDLSTL = (uint16_t)DLIST;
    disp_start();
    SDMCTL = 0x22 | 0x1C;                    /* normal width, DL, players + missiles single line */
}

static void ball_sprites(void)
{
    /* 8 x 16 (round on screen: a mode 4 pixel is two scan lines wide),
       the checkerboard turning about the vertical axis */
    static const uint8_t mask[16] = { 0x3C, 0x7E, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x7E, 0x3C };
    uint8_t f, y, x, red;
    for (f = 0; f < 8; ++f)
        for (y = 0; y < 16; ++y) {
            red = 0;
            for (x = 0; x < 8; ++x)
                if ((((x + f) >> 1) + (y >> 2)) & 1) red |= (uint8_t)(0x80 >> x);
            ball_spr[f][0][y] = (uint8_t)(mask[y] & red);
            ball_spr[f][1][y] = (uint8_t)(mask[y] & ~red);
        }
}

static void flippers_init(void)
{
    uint8_t fl, k;
    for (fl = 0; fl < 2; ++fl)
        for (k = 0; k < FLIP_CELLS; ++k) {
            uint16_t cell = flip_cell[fl][k];
            uint8_t row = (uint8_t)(cell / 40);
            fc_dsttab[fl][k] = (uint16_t)FONTS + (uint16_t)row_font[row] * 1024 + (uint16_t)MAP[cell] * 8;
        }
}

/* the table's scan line and pixel of a point (P), height z (mm) */
static int16_t table_line(int16_t y, int16_t z)
{
    return (int16_t)(VIEW_OY - (int16_t)(mul16(y, VIEW_KY_Q12) >> 12) - (int16_t)(mul16(z, VIEW_KZ_Q12) >> 12));
}
static int16_t table_px(int16_t x) { return (int16_t)(VIEW_OX + (int16_t)(mul16(x, VIEW_KX_Q12) >> 12)); }

static void draw_flippers(void)
{
    static int16_t shown_ang[2] = { 0x7FFF, 0x7FFF };
    uint8_t i, f;
    const Flipper *fp = G.w.flip;
    for (i = 0; i < G.w.nflip && i < 2; ++i, ++fp) {
        int16_t span, at;
        if (fp->ang == shown_ang[i]) continue;
        shown_ang[i] = fp->ang;
        span = fp->d.up - fp->d.rest;
        at = fp->ang - fp->d.rest;
        f = (uint8_t)(span ? (at * 7 + span / 2) / span : 0);
        if (f > 7) f = 7;
        if (f == flip_frame[i]) continue;
        flip_frame[i] = f;
        fc_src = g_flips + ((uint16_t)(i * 8 + f) * FLIP_CELLS * 8);
        fc_dst = fc_dsttab[i];
        fc_n = FLIP_CELLS;
        flip_copy();
    }
}

static void draw_lamps(void)
{
    static uint8_t on[L_COUNT];
    uint8_t i, k;
    pb_lamps(&G, (int8_t)(blink_t < 10), (int8_t)chase, on);
    for (i = 0; i < 29; ++i) {
        uint8_t v = on[i] ? 0x80 : 0;
        if (v == lamp_shown[i]) continue;
        lamp_shown[i] = v;
        for (k = 0; k < lamp_n[i]; ++k) {
            uint8_t *p = MAP + lamp_cell[i][k];
            *p = (uint8_t)((*p & 0x7F) | v);
        }
    }
    for (i = 0; i < G.t->ndrop && i < 8; ++i) {
        uint8_t up = (uint8_t)G.t->seg[G.t->drop_seg[i]].up;
        if (up == drop_shown[i]) continue;
        drop_shown[i] = up;
        for (k = 0; k < drop_n[i]; ++k) MAP[drop_cell[i][k]] = up ? drop_up[i][k] : drop_down[i][k];
    }
}

/* the balls (players 0+1 the first, 2 and 3 the others) and the scroll */
static void draw_balls(void)
{
    uint8_t i, n = 0, *p;
    int16_t follow = -1, lowest = 0x7FFF;
    static uint8_t old_y[4] = { 0, 0, 0, 0 };
    for (i = 0; i < G.w.nballs; ++i) {
        const Ball *b = &G.w.balls[i];
        int16_t t;
        if (b->state == BS_DRAINED) continue;
        t = table_line(b->y, b->z);
        if (b->y < lowest) { lowest = b->y; follow = t; }
    }
    /* the scroll: the lowest ball a third of the way up the screen */
    if (follow >= 0) {
        int16_t want = follow - 110;
        if (want < 0) want = 0;
        if (want > (TROWS - VIS_ROWS) * 8) want = (TROWS - VIS_ROWS) * 8;
        if ((int16_t)scroll_px < want) scroll_px += (uint16_t)((want - (int16_t)scroll_px + 3) >> 2);
        else if ((int16_t)scroll_px > want) scroll_px -= (uint16_t)(((int16_t)scroll_px - want + 3) >> 2);
    }
    scroll_row = (uint8_t)(scroll_px >> 3);
    scroll_fine = (uint8_t)(scroll_px & 7);
    for (i = 0; i < G.w.nballs && n < 3; ++i) {
        const Ball *b = &G.w.balls[i];
        int16_t sy, sx;
        if (b->state == BS_DRAINED) continue;
        sy = TABLE_Y0 + table_line(b->y, b->z) - (int16_t)scroll_px - 8;
        sx = PMG_X0 + table_px(b->x) - 4;
        if (sy < TABLE_Y0 - 4 || sy > 230) {    /* off the scrolled view: hidden */
            if (n == 0) {
                memset(pmg_player_mem(0) + old_y[0], 0, 16);
                memset(pmg_player_mem(1) + old_y[0], 0, 16);
                pmg_x(0, 0);
                pmg_x(1, 0);
            } else {
                memset(pmg_player_mem((uint8_t)(n + 1)) + old_y[n], 0, 16);
                pmg_x((uint8_t)(n + 1), 0);
            }
            ++n;
            continue;
        }
        if (n == 0) {
            const uint8_t *r = ball_spr[spin & 7][0], *w = ball_spr[spin & 7][1];
            p = pmg_player_mem(0);
            memset(p + old_y[0], 0, 16);
            memcpy(p + (uint8_t)sy, r, 16);
            p = pmg_player_mem(1);
            memset(p + old_y[0], 0, 16);
            memcpy(p + (uint8_t)sy, w, 16);
            pmg_x(0, (uint8_t)sx);
            pmg_x(1, (uint8_t)sx);
            old_y[0] = (uint8_t)sy;
        } else {
            p = pmg_player_mem((uint8_t)(n + 1));
            memset(p + old_y[n], 0, 16);
            memcpy(p + (uint8_t)sy, ball_spr[0][0], 16);
            pmg_x((uint8_t)(n + 1), (uint8_t)sx);
            old_y[n] = (uint8_t)sy;
        }
        ++n;
    }
    for (; n < 3; ++n) {                       /* the unused ones off */
        uint8_t pl = (uint8_t)(n == 0 ? 0 : n + 1);
        pmg_x(pl, 0);
        if (n == 0) pmg_x(1, 0);
    }
}

static void draw_dmd(void)
{
    static Dmd dm;
    static char big[24], small[44];
    if (mode == M_ENTRY) {
        dmd_text("HIGH SCORE", ed_name);
        return;
    }
    pb_dmd(&G, &dm);
    if (mode == M_ATTRACT) {
        if ((mode_t / 150) & 1) {
            uint8_t i;
            char *p = small;
            for (i = 0; i < 3; ++i) {
                memcpy(p, hs[i].name, 3);
                p[3] = ' ';
                pb_fmt(p + 4, hs[i].score);
                p += strlen(p);
                *p++ = ' ';
                *p++ = ' ';
            }
            *p = 0;
            dmd_text("HIGH SCORES", small);
        } else
            dmd_text("BOING PINBALL", "COMET TOUR   PRESS START");
        return;
    }
    if (dm.flash && (blink_t & 4)) { dmd_text("", dm.small ? dm.small : ""); return; }
    strcpy(big, dm.big ? dm.big : "");
    strcpy(small, dm.small ? dm.small : "");
    dmd_text(big, small);
}

/* ------------------------------------------------------------ sound */
static void sounds(void)
{
    uint8_t i, n = (uint8_t)G.nev;
    const Event *e = G.ev;
    for (i = 0; i < n; ++i, ++e) {
        switch (e->type) {
        case EV_BUMPER:   snd_sfx(2, 60, SND_PURE, 14, 2, -2); break;
        case EV_SLING:    snd_sfx(2, 90, SND_PURE, 12, 2, 3); break;
        case EV_FLIPSTOP: snd_sfx(3, 180, SND_NOISE, 8, 3, 0); break;
        case EV_DROP:     snd_sfx(2, 120, SND_BUZZ, 12, 2, 0); break;
        case EV_SPIN:     snd_sfx(3, 30, SND_PURE, 6, 3, 0); break;
        case EV_PLUNGE:   snd_sfx(3, 40, SND_NOISE, 12, 1, 4); break;
        case EV_RAMP:     snd_sfx(2, 80, SND_PURE, 12, 1, -3); break;
        case EV_DRAIN:    snd_sfx(3, 200, SND_NOISE, 14, 1, 2); break;
        case EV_SAUCER:   snd_sfx(2, 150, SND_BUZZ, 12, 1, 0); break;
        case EV_JACKPOT:
        case EV_MULTIBALL:
        case EV_SUPERNOVA:
        case EV_MISSIONDONE: snd_sfx(2, 30, SND_PURE, 15, 1, -1); break;
        case EV_TILT:     snd_sfx(3, 250, SND_BUZZ, 15, 1, 0); break;
        }
    }
    G.nev = 0;                                 /* read: the rules add the next step's */
}

/* ------------------------------------------------------------ the game */
#define K_Z 23
#define K_M 37
#define K_N 35
#define K_SPACE 33

static uint8_t key_down(uint8_t code) { return (SKSTAT & 0x04) == 0 && (KBCODE & 0x3F) == code; }

static void read_player(uint8_t held, uint8_t edge)
{
    memset(&in, 0, sizeof in);
    in.pull_to = -1;
    in.left = (int8_t)((held & (IN_LEFT | IN_FIRE)) || key_down(K_Z) || (SKSTAT & 0x08) == 0);
    in.right = (int8_t)((held & (IN_RIGHT | IN_FIRE)) || key_down(K_M));
    in.plunger = (int8_t)((held & IN_DOWN) || key_down(K_SPACE));
    if ((edge & IN_UP) || key_down(K_N)) in.nudge_y = 1;
}

static void new_game(uint8_t demo)
{
    pb_game_init(&G, &table_0, demo ? 12345UL : 777UL + frame_count);
    pb_game_start(&G);
    memset(lamp_shown, 0xFF, sizeof lamp_shown);
    memset(drop_shown, 0xFF, sizeof drop_shown);
    bot.rng = 99;
    bot.hold[0] = bot.hold[1] = bot.launch = 0;
    mode = demo ? M_ATTRACT : M_PLAY;
    mode_t = 0;
    last_ball = 0;
    last_score = 0;
    a8_log("pb: game demo=%u\n", demo);
}

static int8_t hs_rank(int32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hs[i].score) return (int8_t)i;
    return -1;
}


static void tick(uint8_t held, uint8_t edge)
{
    if (mode == M_ATTRACT) pb_bot(&G, &in, &bot);
    else read_player(held, edge);
    pb_game_tick(&G, &in);
    sounds();
    probe[0] = G.w.balls[0].x;
    probe[1] = G.w.balls[0].y;
    probe[2] = G.w.balls[0].vx;
    probe[3] = G.w.balls[0].vy;
    probe[4] = G.w.balls[0].state;
    probe[5] = G.ball_no;
    probe[6] = G.state;
    probe[7] = G.w.nballs;
    if (G.score != last_score) { last_score = G.score; a8_log("pb: score=%ld\n", G.score); }
    if (G.ball_no != last_ball) { last_ball = G.ball_no; a8_log("pb: ball n=%d\n", G.ball_no); }
    if (G.state == GS_OVER && mode != M_OVER && mode != M_ENTRY) {
        int8_t r = hs_rank(G.score);
        a8_log("pb: gameover score=%ld demo=%u\n", G.score, mode == M_ATTRACT);
        if (mode == M_PLAY && r >= 0) {
            ed_place = (uint8_t)r;
            ed_pos = 0;
            memcpy(ed_name, "AAA", 4);
            mode = M_ENTRY;
        } else
            mode = M_OVER;
        mode_t = 0;
    }
}

static void entry_input(uint8_t edge, uint8_t key)
{
    char c = key != KEY_NONE ? key_ascii(key) : 0;
    if (c >= 'A' && c <= 'Z') { ed_name[ed_pos] = c; if (ed_pos < 2) ed_pos++; }
    if (edge & IN_UP) ed_name[ed_pos] = ed_name[ed_pos] == 'Z' ? 'A' : (char)(ed_name[ed_pos] + 1);
    if (edge & IN_DOWN) ed_name[ed_pos] = ed_name[ed_pos] == 'A' ? 'Z' : (char)(ed_name[ed_pos] - 1);
    if ((edge & IN_RIGHT) && ed_pos < 2) ed_pos++;
    if ((edge & IN_LEFT) && ed_pos > 0) ed_pos--;
    if ((edge & (IN_FIRE | IN_START)) || c == '\n') {
        uint8_t i;
        for (i = 4; i > ed_place; --i) hs[i] = hs[i - 1];
        memcpy(hs[ed_place].name, ed_name, 4);
        hs[ed_place].score = G.score;
        a8_log("pb: hiscore name=%s score=%ld place=%u\n", ed_name, G.score, ed_place);
        new_game(1);
    }
}

int main(void)
{
    uint8_t b, n, held, edge, key, due = 0, stepped;
    for (b = 0; b < 4; ++b)
        if (!bank_ok(b)) { a8_log("pb: no banks\n"); a8_fail("needs a 130XE"); for (;;) ; }
    os_off();
    maths_init();
    pmg_init();
    sound_init();
    frame_init();
    display_init();
    ball_sprites();
    flippers_init();
    pmg_color(0, 0x36);                      /* the Boing ball: red */
    pmg_color(1, 0x0E);                      /*   and white */
    pmg_color(2, 0x0C);
    pmg_color(3, 0x0C);
    GPRIOR = 1;                              /* players over the table */
    new_game(1);
    a8_log("pb: ready\n");
    for (;;) {
        n = frame_wait();
        held = in_held;
        edge = take_latch();
        key = key_get();
        ++blink_t;
        if (blink_t >= 20) { blink_t = 0; ++chase; }
        ++spin;
        mode_t += n;
        if (mode == M_ENTRY) { entry_input(edge, key); draw_dmd(); continue; }
        if ((edge & IN_START) && mode != M_PLAY) { new_game(0); due = 0; }
        if (mode == M_OVER && mode_t > 150) new_game(1);
        /* 25 steps a second: one every second frame, at most two a frame */
        due += n;
        if (due > 4) { ticks_late += (uint16_t)(due - 4) >> 1; due = 4; }
        stepped = 0;
        while (due >= 2) {
            due -= 2;
            if (mode != M_OVER) tick(held, edge);
            ++ticks;
            edge = 0;
            stepped = 1;
        }
        /* the ball and the flippers move only when the physics steps: no
           need to draw them again in between (the time goes to the steps) */
        if (stepped) {
            draw_flippers();
            draw_balls();
        }
        if ((blink_t & 3) == 0) draw_lamps();     /* the lamps every 4th frame, the display every 8th */
        if ((blink_t & 7) == 2) draw_dmd();
    }
}
