/*
 * Sky Knights - the Atari display (the original's draw.c, redone for the
 * hardware).
 *
 *   0-7     the top HUD row (ANTIC 6): player 1's score and lives, the wave
 *   8-183   the field: ANTIC E, 160 x 176 = the world's y 10.7..245 (x / 2,
 *           y * 3 / 4 - 8): the ceiling is at 10, the ground's bottom 244
 *   184-191 the bottom HUD row: player 2 (or the controls)
 *
 * The players' knights are hardware players in two colours each: player 1
 * on players 0 (its yellow mount) and 2 (its blue rider), player 2 on 1
 * (cyan) and 3 (red). The enemies and pods are XORed into the field
 * (xspr.s), in their type's colour: field colour 1 is the ledges' brown in
 * the ledges' rows and the wraiths' blue everywhere else (DLIs around each
 * ledge), 2 the swoopers' orange, 3 the raiders' grey and the pods.
 * Messages (the wave, game over) are two ANTIC 6 rows a second display
 * list shows over the sky.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "view.h"

#define FIELD    ((uint8_t *)0xA010)   /* 176 lines x 40 */
#define HUD_TOP  ((uint8_t *)0xBB90)
#define HUD_BOT  ((uint8_t *)0xBBA4)
#define MSG      ((uint8_t *)0xBBB8)   /* 2 x 20 */
#define FONT     ((uint8_t *)0xBC00)   /* 512 */
#define DL_GAME  ((uint8_t *)0xBE00)
#define DL_MSG   ((uint8_t *)0xBF00)
#define TITLE    ((uint8_t *)0xA010)
#define DL_TITLE ((uint8_t *)0xA800)
#define LINES    176
#define MSG_LINE 16                    /* the message rows over lines 16..31 */

extern uint8_t dli_bk[16], dli_pf0[16], dli_pf1[16], dli_pf2[16], dli_pf3[16];
void irq_install(void);

/* xspr.s */
extern const uint8_t *xs_img;
extern int8_t xs_bx, xs_ln;
extern uint8_t xs_h, xs_w, xs_col, xs_ci, xs_ii;
void xs_draw(void);

/* gfx.c */
extern const uint8_t kn_mount[8][17], kn_rider[8][17], en_img[32][51], egg_img[4][12];
#define KN_LINES 17

/* ---- colours (the original's palette) */
#define C_LEDGE    A8_COLOR(2, 6)      /* brown */
#define C_GROUND   A8_COLOR(1, 8)      /* tan */
#define C_WRAITH   A8_COLOR(8, 6)      /* blue */
#define C_SWOOPER  A8_COLOR(2, 10)     /* orange */
#define C_RAIDER   A8_COLOR(0, 10)     /* grey */
#define C_P1MOUNT  A8_COLOR(1, 14)     /* yellow */
#define C_P1KNIGHT A8_COLOR(8, 10)     /* blue */
#define C_P2MOUNT  A8_COLOR(10, 12)    /* cyan */
#define C_P2KNIGHT A8_COLOR(4, 8)      /* red */
#define C_SCORE    A8_COLOR(1, 14)

static uint8_t scode(char c)
{
    uint8_t a = (uint8_t)c;
    if (a < 0x20) return (uint8_t)(a + 0x40);
    if (a < 0x60) return (uint8_t)(a - 0x20);
    return a;
}

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

/* ---- display lists */
static uint8_t *dl_lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

/* the field's lines from..to-1: a DLI on the lines before and at the end
   of each ledge (its rows get the ledge colour) and on the last line */
static uint8_t dli_line[LINES];

static uint8_t *dl_field(uint8_t *d, uint8_t from, uint8_t to)
{
    uint8_t l, op;
    for (l = from; l < to; ++l) {
        op = (uint8_t)(0x0E | (dli_line[l] ? 0x80 : 0));
        if (l == from || l == 102) d = dl_lms(d, (uint8_t)(op | 0x40), FIELD + l * 40);
        else *d++ = op;
    }
    return d;
}

static uint8_t wline(WORD y)
{
    return (uint8_t)(((y * 3) >> 2) - 8);
}

static void dl_build(void)
{
    uint8_t *d, k = 0;
    memset(dli_line, 0, sizeof dli_line);
    /* DLI 0 is the top HUD row's (the field's start colours) */
    dli_bk[0] = 0;
    dli_pf0[0] = C_WRAITH;
    dli_pf1[0] = C_SWOOPER;
    dli_pf2[0] = C_RAIDER;
    dli_pf3[0] = C_SCORE;
    k = 1;

    /* the ledges, top to bottom (plat_y is 60, 100, 140, 180, 236) */
    {
        static const uint8_t order[6] = {6, 4, 3, 1, 0, 0};
        uint8_t j;
        for (j = 0; j < 5; ++j) {
            uint8_t p = order[j];
            uint8_t top = wline(plat_y[p]), bot = (uint8_t)(top + 5);
            dli_line[top - 1] = 1;              /* before it: its colour */
            dli_bk[k] = 0;
            dli_pf0[k] = p == 0 ? C_GROUND : C_LEDGE;
            dli_pf1[k] = C_SWOOPER;
            dli_pf2[k] = C_RAIDER;
            dli_pf3[k] = C_SCORE;
            ++k;
            if (bot < LINES - 1) {
                dli_line[bot] = 1;              /* after it: the wraiths' blue */
                dli_bk[k] = 0;
                dli_pf0[k] = C_WRAITH;
                dli_pf1[k] = C_SWOOPER;
                dli_pf2[k] = C_RAIDER;
                dli_pf3[k] = C_SCORE;
                ++k;
            }
        }
    }
    dli_line[LINES - 1] = 1;                    /* the bottom HUD's colours */
    dli_bk[k] = 0;
    dli_pf0[k] = C_P2MOUNT;
    dli_pf1[k] = C_SCORE;
    dli_pf2[k] = A8_COLOR(0, 8);
    dli_pf3[k] = C_P2KNIGHT;

    d = DL_GAME;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_lms(d, 0xC6, HUD_TOP);
    d = dl_field(d, 0, LINES);
    d = dl_lms(d, 0x46, HUD_BOT);
    d = dl_lms(d, 0x41, DL_GAME);

    d = DL_MSG;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_lms(d, 0xC6, HUD_TOP);
    d = dl_field(d, 0, MSG_LINE);
    d = dl_lms(d, 0x46, MSG);                   /* (no DLIs in these lines) */
    *d++ = 0x06;
    d = dl_field(d, MSG_LINE + 16, LINES);
    d = dl_lms(d, 0x46, HUD_BOT);
    d = dl_lms(d, 0x41, DL_MSG);
}

void view_msg(const char *a, const char *b)
{
    memset(MSG, 0, 40);
    if (a) put6(MSG + ((20 - strlen(a)) >> 1), a, 3);
    if (b) put6(MSG + 20 + ((20 - strlen(b)) >> 1), b, 3);
    SDLSTL = (uint16_t)(a || b ? DL_MSG : DL_GAME);
}

/* ---- the field: the ledges */
static void hline_bytes(uint8_t line, WORD x0, WORD x1, uint8_t v)
{
    uint8_t *p = FIELD + line * 40;
    uint8_t c;
    for (c = (uint8_t)(x0 >> 3); c <= (uint8_t)((x1 - 1) >> 3) && c < 40; ++c) p[c] = v;
}

static void draw_ledges(void)
{
    uint8_t i, l, top;
    memset(FIELD, 0, LINES * 40);
    for (i = 0; i < NUM_PLATFORMS; ++i) {
        top = wline(plat_y[i]);
        for (l = 0; l < 6; ++l) {
            /* the surface solid, the underside dotted (the original's shadow) */
            hline_bytes((uint8_t)(top + l), plat_x[i], plat_x[i] + plat_w[i], l < 2 ? 0x55 : (l & 1 ? 0x44 : 0x11));
        }
    }
}

/* ---- HUD */
static int32_t hud_score[2] = {-1, -1};
static int8_t hud_lives[2] = {-1, -1};
static WORD hud_wave = -1;

void view_hud_reset(void)
{
    memset(HUD_TOP, 0, 20);
    memset(HUD_BOT, 0, 20);
    put6(HUD_TOP, "1P", 0);
    put6(HUD_TOP + 14, "WAVE", 2);
    if (gs.num_players > 1) put6(HUD_BOT, "2P", 0);
    else put6(HUD_BOT, "STICK FLY  FIRE FLAP", 2);
    hud_score[0] = hud_score[1] = -1;
    hud_lives[0] = hud_lives[1] = -1;
    hud_wave = -1;
}

static void view_hud(void)
{
    uint8_t i;
    for (i = 0; i < gs.num_players; ++i) {
        uint8_t *row = i ? HUD_BOT : HUD_TOP;
        if (gs.score[i] != hud_score[i]) {
            hud_score[i] = gs.score[i];
            num6(row + 3, (uint32_t)gs.score[i], 6, 1);
        }
        if (p_lives[i] != hud_lives[i]) {
            hud_lives[i] = p_lives[i];
            row[10] = (uint8_t)(0xC0 | 0x0A);   /* '*' */
            num6(row + 11, (uint32_t)(p_lives[i] < 0 ? 0 : p_lives[i]), 1, 3);
        }
    }
    if (gs.wave != hud_wave) {
        hud_wave = gs.wave;
        num6(HUD_TOP + 18, (uint32_t)gs.wave, 2, 2);
    }
}

/* ---- the knights: players */
#define PCOLR_(n, c) (*(uint8_t *)(0x2C0 + (n)) = (c))
#define HPOSP_(n, x) (*(volatile uint8_t *)(0xD000 + (n)) = (x))
/* player_put is xspr.s's (pp_n, pp_shape, pp_line, pp_frame, pp_hx) */
extern uint8_t pl_line[4], pl_frame[4], pp_n, pp_line, pp_frame, pp_hx;
extern const uint8_t *pp_shape;
#pragma zpsym ("pp_shape")
void pp_put(void);

static void player_hide(uint8_t n)
{
    if (pl_frame[n] != 0xFF) memset((uint8_t *)(0x9C00 + (n << 8)) + pl_line[n], 0, KN_LINES);
    pl_frame[n] = 0xFF;
    HPOSP_(n, 0);
}

static void view_players(void)
{
    uint8_t i;
    for (i = 0; i < 2; ++i) {
        WORD x;
        uint8_t f, line, hx;
        if (i >= gs.num_players || !p_alive[i] || gs.state == STATE_TITLE) {
            player_hide(i);
            player_hide((uint8_t)(i + 2));
            continue;
        }
        x = b_px[i];
        f = (uint8_t)(b_facing[i] * 4 + p_flapping[i] * 2 + b_anim_frame[i]);
        /* PM line of world y - 2 (the head): 32 + (y - 2) * 3 / 4 */
        line = (uint8_t)(2 + (((uint16_t)(b_py[i] + 38) * 3) >> 2));
        hx = (uint8_t)(17 + ((uint16_t)(x + 64) >> 1));
        if (x < -20 || x > SCREEN_W) hx = 0;
        pp_line = line;
        pp_frame = f;
        pp_hx = hx;
        pp_n = i;
        pp_shape = kn_mount[f];
        pp_put();
        pp_n = (uint8_t)(i + 2);
        pp_shape = kn_rider[f];
        pp_put();
    }
}

/* ---- enemies and pods: XORed into the field */
#define NOBJ (MAX_ENEMIES + MAX_EGGS)
/* the objects drawn and ob_set are in xspr.s: set o_k (0-7 enemies, 8-15
   pods), o_on, o_bx (byte column), o_ln (line), o_img, o_col (colour index:
   0 $AA, 1 $FF, 2 $55), then call it; it redraws only what changed */
extern uint8_t ob_on[NOBJ];
extern uint8_t o_k, o_on, o_img, o_col;
extern int8_t o_bx, o_ln;
void ob_set(void);
extern uint8_t ve_hide, ve_par;
void view_enemies(void);


static void view_objects(void)
{
    static uint8_t i, title;
    static WORD x, y, px;
    title = gs.state == STATE_TITLE;
    /* port: with more than 5 enemies (waves 4 on) half of them are moved
       each time, in turn: the drawing then fits in the time the rules leave
       (at 50 Hz); the knights move every time */
    {
        static uint8_t par;
        uint8_t n = 0;
        for (i = 0; i < MAX_ENEMIES; ++i) n += e_active[i];
        par ^= 1;
        ve_par = n > 5 ? par : 0xFF;
    }
    ve_hide = title;
    view_enemies();
    for (i = 0; i < MAX_EGGS; ++i) {
        o_k = (uint8_t)(MAX_ENEMIES + i);
        if (!g_active[i] || title) {
            if (ob_on[o_k]) { o_on = 0; ob_set(); }
            continue;
        }
        x = FIX_INT(g_x[i]);
        y = FIX_INT(g_y[i]);
        o_col = 1;
        /* the original's wobble: it flashes before it hatches, and shakes */
        if (g_timer[i] < EGG_WOBBLE_TIME) {
            if (g_timer[i] & 4) o_col = 0;
            if (g_timer[i] < 60) x += (g_timer[i] & 2) ? 1 : -1;
        }
        px = x >> 1;
        o_on = 1;
        o_bx = (int8_t)(px >> 2);
        o_ln = (int8_t)(((y * 3) >> 2) - 8);
        o_img = (uint8_t)px & 3;
        ob_set();
    }
}

void view_clear_objects(void)
{
    uint8_t k;
    o_on = 0;
    for (k = 0; k < NOBJ; ++k) { o_k = k; ob_set(); }
}

void view_frame(void)
{
    view_players();
    view_objects();
    view_hud();
}

/* ---- screens */
void view_game(void)
{
    uint8_t k;
    for (k = 0; k < NOBJ; ++k) ob_on[k] = 0;
    draw_ledges();
    view_hud_reset();
    COLOR0 = C_P1MOUNT;
    COLOR1 = C_SCORE;
    COLOR2 = A8_COLOR(0, 8);
    COLOR3 = C_P1KNIGHT;
    COLOR4 = 0;
    view_msg(0, 0);
}

static uint8_t *trow, *press_row;

static uint8_t *title_line(uint8_t *d, uint8_t mode, const char *s, uint8_t pf)
{
    memset(trow, 0, 20);
    if (s) put6(trow + ((20 - strlen(s)) >> 1), s, pf);
    d = dl_lms(d, (uint8_t)(0x40 | mode), trow);
    trow += 20;
    return d;
}

void view_title(const char (*names)[4], const uint32_t *scores, uint8_t sel)
{
    uint8_t *d = DL_TITLE;
    uint8_t i;
    char line[21];
    for (i = 0; i < 4; ++i) player_hide(i);
    view_clear_objects();
    trow = TITLE;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = title_line(d, 7, "SKY KNIGHTS", 0);
    d = title_line(d, 6, "JOUST ON FLYING MOUNTS", 2);
    *d++ = 0x70;
    d = title_line(d, 6, sel ? "  1 PLAYER" : "> 1 PLAYER", sel ? 2 : 1);
    d = title_line(d, 6, sel ? "> 2 PLAYERS" : "  2 PLAYERS", sel ? 1 : 2);
    *d++ = 0x70;
    d = title_line(d, 6, "HIGH SCORES", 3);
    for (i = 0; i < 5; ++i) {
        uint32_t v = scores[i];
        uint8_t k;
        line[0] = (char)('1' + i);
        line[1] = ' ';
        memcpy(line + 2, names[i], 3);
        line[5] = ' ';
        for (k = 12; k > 5; --k) { line[k] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
        line[13] = 0;
        d = title_line(d, 6, line, i < 3 ? 0 : 2);
    }
    *d++ = 0x70;
    d = title_line(d, 6, "STICK FLY, FIRE FLAP", 2);
    d = title_line(d, 6, "HIGHER KNIGHT WINS", 2);
    d = title_line(d, 6, "CATCH THE PODS", 2);
    *d++ = 0x70;
    press_row = trow;
    d = title_line(d, 6, "PRESS FIRE", 1);
    d = dl_lms(d, 0x41, DL_TITLE);
    COLOR0 = C_P1MOUNT;
    COLOR1 = C_P2MOUNT;
    COLOR2 = A8_COLOR(0, 8);
    COLOR3 = C_SWOOPER;
    COLOR4 = 0;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    SDLSTL = (uint16_t)DL_TITLE;
}

void view_title_frame(uint8_t t)
{
    if ((t & 31) == 0) {
        memset(press_row, 0, 20);
        if (t & 32) put6(press_row + 5, "PRESS FIRE", 1);
    }
}

void view_init(void)
{
    uint8_t t;
    SDMCTL = 0;                             /* (the OS screen is where our font goes) */
    t = RTCLOK2;
    while (RTCLOK2 == t)
        ;
    memcpy(FONT, (const void *)0xE000, 512);
    dl_build();
    memset(FIELD, 0, LINES * 40);
    SDLSTL = (uint16_t)DL_GAME;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    SDMCTL = 0x22;
    pmg_init();
    GPRIOR = 1;                             /* players over the field */
    PCOLR0 = C_P1MOUNT;
    *(&PCOLR0 + 1) = C_P2MOUNT;
    *(&PCOLR0 + 2) = C_P1KNIGHT;
    *(&PCOLR0 + 3) = C_P2KNIGHT;
    irq_install();
}
