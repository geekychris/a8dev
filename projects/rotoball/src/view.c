/*
 * Rotoball: the split screen (the original's pitch.c and render_frame).
 *
 * The original's 320 x 256 at x / 2, y * 3 / 4 (see tools/gen_gfx.py): two
 * panes of 96 lines, the top one P2's (or the CPU's) view, the bottom one
 * P1's. A pane: 2 blank lines, its HUD row (ANTIC 2), the sky (blank
 * lines: COLBK), the horizon (a black line) and 63 floor lines (ANTIC E),
 * each pointed by its display list at the stripes of its row for the
 * camera (floor.c, floor.s): nothing is drawn. Two display lists: the
 * frame makes one while the other shows, the VBI swaps them.
 *
 * The things on the pitch are players and missiles, the same ones in both
 * panes (the DLI between them moves them): the ball players 0 + 1 (its
 * body and highlight, OR'd), the other skimmer players 2 + 3 (its body and
 * dark outline), the three posts of the goal ahead missiles 0-2.
 *
 * port: projections and sizes are the original's (focal length 180, its
 * clamps to the pane, its near clip), at the Atari's scale; the shapes are
 * its draw_ball_sphere and draw_skimmer, made on the host at every size.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "gfx.h"
#include "view.h"

void irq_install(void);
extern uint8_t rg_top[14], rg_bot[14], dl_next;

void floor_update(void);
extern uint8_t *fu_dl;
#pragma zpsym ("fu_dl")
extern const uint8_t *fu_o1, *fu_o2;
extern int16_t fu_cx;
extern uint8_t fu_f, fu_flip, fu_sub;

void pm_put(void);
extern const uint8_t *pp_src;
#pragma zpsym ("pp_src")
extern uint8_t pp_n, pp_pane, pp_h;
extern int8_t pp_top;
void pm_post(void);
extern uint8_t po_m, po_pane, po_dst, po_len;

/* colours (the original's palette, near enough) */
#define C_SKY_TOP   0x82        /* P2's sky: deep blue */
#define C_SKY_BOT   0x54        /* P1's: magenta */
#define C_GRID_A    0x90        /* the cells: dark blue-black */
#define C_GRID_B    0x0C        /* and pale grey (also the HUD's text) */
#define C_BLACK     0x00
#define C_P1        0x98        /* P1's goal and skimmer: cyan */
#define C_P2        0x28        /* P2's: amber */
#define C_BALL      0x34        /* the ball's red; its highlight OR'd */
#define C_SPOT      0x0A
#define C_EDGE      0x00        /* the skimmer's outline */

/* ---- the display lists */
static uint8_t back;                        /* the list being made: 0 DL0, 1 DL1 */
int16_t view_cam[2][4];                     /* the cameras a list shows (x, z of P1, P2; for the test) */
static uint8_t fo1[2][FROWS * 4], fo2[2][FROWS * 4]; /* row r's LMS offsets in a pane, at 4 r */
static uint8_t pane_ofs[2];
static const uint8_t none[1] = {0xFF};
static const uint8_t *ov[2] = {none, none};

/* the text row at pane line l, or 0xFF */
static uint8_t overlay_at(uint8_t p, uint8_t l)
{
    const uint8_t *o = ov[p];
    for (; *o != 0xFF; o += 2)
        if (o[0] == l) return o[1];
    return 0xFF;
}

static uint8_t *lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

static void dl_build(uint8_t *dl)
{
    static uint8_t *d, *pane0, p, l, t, r, dli;
    d = dl;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    memset(fo1, 0, sizeof fo1);
    memset(fo2, 0, sizeof fo2);
    for (p = 0; p < 2; ++p) {
        pane0 = d;
        pane_ofs[p] = (uint8_t)(d - dl);
        *d++ = 0x10;                        /* lines 0-1 */
        d = lms(d, 0x42, TROW(p));          /* 2-9: the HUD */
        *d++ = 0x70;                        /* 10-31: the sky */
        *d++ = 0x70;
        *d++ = 0x50;
        d = lms(d, 0x4E, BLACK);            /* 32: the horizon */
        /* 33-94: the floor, ANTIC D (two lines a row); text rows (8 lines)
           at odd lines; the top pane's last floor row has the DLI (the
           bottom pane's registers) */
        for (l = 33; l < 95;) {
            t = overlay_at(p, l);
            if (t != 0xFF) {
                dli = (p == 0 && l + 8 > 94) ? 0x80 : 0;
                d = lms(d, (uint8_t)(0x42 | dli), TROW(t));
                l += 8;
                continue;
            }
            dli = (p == 0 && l == 93) ? 0x80 : 0;
            r = (uint8_t)((l - 33) >> 1);
            fo1[p][4 * r] = (uint8_t)(d + 1 - pane0);
            d = lms(d, (uint8_t)(0x4D | dli), BLACK);
            l += 2;
        }
        /* 95: the divider (top), the last row once more (bottom) */
        if (p == 0) {
            d = lms(d, 0x4E, BLACK);
        } else {
            fo2[p][4 * (FROWS - 1)] = (uint8_t)(d + 1 - pane0);
            d = lms(d, 0x4E, BLACK);
        }
    }
    d = lms(d, 0x41, dl);
    /* a row of one line: its offset twice; hidden by text: the last row's
       (floor.s writes it last) */
    for (p = 0; p < 2; ++p)
        for (r = 0; r < FROWS; ++r) {
            if (!fo1[p][4 * r]) fo1[p][4 * r] = fo1[p][4 * (FROWS - 1)];
            if (!fo2[p][4 * r]) fo2[p][4 * r] = fo1[p][4 * r];
        }
}

void view_overlay(const uint8_t *top, const uint8_t *bottom)
{
    ov[0] = top ? top : none;
    ov[1] = bottom ? bottom : none;
    frame_wait();                           /* (in the vertical blank) */
    dl_build(DL0);
    dl_build(DL1);
}

/* ---- text: ANTIC 2 rows (the ROM font) on black */
static uint8_t scode(char c)
{
    uint8_t a = (uint8_t)c;
    if (a < 0x20) return (uint8_t)(a + 0x40);
    if (a < 0x60) return (uint8_t)(a - 0x20);
    return a;
}

static void put_text(uint8_t *d, const char *s)
{
    while (*s) *d++ = scode(*s++);
}

void view_text(uint8_t row, uint8_t col, const char *s)
{
    put_text(TROW(row) + col, s);
}

void view_text_clear(uint8_t row)
{
    memset(TROW(row), 0, 40);
}

/* ---- the HUD (the original's draw_hud): redrawn when it changes */
static uint8_t hud_s0 = 0xFF, hud_s1, hud_c, hud_sec, hud_pl;

static void put_num(uint8_t *d, uint8_t v)
{
    if (v >= 10) *d++ = (uint8_t)(0x10 + v / 10);
    *d = (uint8_t)(0x10 + v % 10);
}

void view_hud_reset(void)
{
    hud_s0 = 0xFF;
}

static void view_hud(void)
{
    static uint8_t row[2][40];              /* made here, then copied: no blank frame */
    uint8_t sec = G.secs, *d;
    if (hud_s0 == G.score[0] && hud_s1 == G.score[1] && hud_c == (uint8_t)ball_carrier &&
        hud_sec == sec && hud_pl == G.players) return;
    hud_s0 = G.score[0];
    hud_s1 = G.score[1];
    hud_c = (uint8_t)ball_carrier;
    hud_sec = sec;
    hud_pl = G.players;
    memset(row, 0, sizeof row);
    /* the original's "P2 CPU: n *BALL*" from the left edge, the clock at 18 */
    put_text(row[0], G.players == 1 ? "P2 CPU:" : "P2:");
    put_num(row[0] + (G.players == 1 ? 8 : 4), G.score[1]);
    if (ball_carrier == 1) put_text(row[0] + (G.players == 1 ? 11 : 7), "*BALL*");
    put_text(row[1], G.players == 1 ? "P1 YOU:" : "P1:");
    put_num(row[1] + (G.players == 1 ? 8 : 4), G.score[0]);
    if (ball_carrier == 0) put_text(row[1] + (G.players == 1 ? 11 : 7), "*BALL*");
    /* the clock, top centre */
    d = row[0] + 18;
    d[0] = (uint8_t)(0x10 + sec / 600);
    d[1] = (uint8_t)(0x10 + (sec / 60) % 10);
    d[2] = 0x1A;                            /* ':' */
    d[3] = (uint8_t)(0x10 + (sec % 60) / 10);
    d[4] = (uint8_t)(0x10 + sec % 10);
    memcpy(TROW(0), row, 80);
}

/* ---- a pane's camera and the projection (the original's project()).
   port: mathx.s's pobj: 1 / depth from a table, products by quarter
   squares, not 32-bit divisions. A point's sizes all go with 1 / depth:
   from its ground line g = 270 x 256 / depth, the ball's radius
   180 / depth = 2 g / 3, the skimmer's scale 360 / depth = 4 g / 3, the
   posts' tops 1080 / depth = 4 g */
uint16_t __fastcall__ recip(uint16_t z);            /* 2^22 / z */
uint16_t __fastcall__ mulhi(uint16_t a, uint16_t b); /* a * b >> 16 */
void pobj(void);
extern int16_t cam_x, cam_z, o_wx, o_wz;
extern uint8_t cam_s, o_vis, o_sx;
extern uint16_t o_g, o_inv;
uint8_t off90[114];                         /* (t * 90) >> 7 */

static uint8_t proj(int16_t wx, int16_t wz)
{
    o_wx = wx;
    o_wz = wz;
    pobj();
    return o_vis;
}

/* ---- the sprites of a pane */
static const uint8_t size_of[5] = {0, 0, 1, 1, 3};
static const uint8_t pbase[2] = {32, 128};  /* a pane's line 0 in PM lines */
/* port: tables, not ternaries of constant pointers (cc65 #3035, gotchas) */
static uint8_t * const dls[2] = {DL0, DL1};
static uint8_t * const rgs[2] = {rg_top, rg_bot};

static void pane(uint8_t p)
{
    static uint8_t cam, other, i, w, idx, g8, base;
    static uint8_t rg[14];
    static int16_t y0, y1, c, d;
    static int16_t post_x[3];
    static uint16_t g;
    static const uint8_t *src;
    memcpy(rg, rgs[p], 14);
    cam = p ? 0 : 1;
    other = (uint8_t)(1 - cam);
    cam_s = cam;
    cam_x = sk_x[cam];
    cam_z = sk_z[cam];
    base = pbase[p];
    pp_pane = p;
    po_pane = p;

    /* the floor: the camera's phase across (cz / 10: a two-cell period,
       1/256) and its cell along (cx / 5, 8.8); P2 sees it mirrored */
    {
        uint8_t fz = (uint8_t)mulhi((uint16_t)(cam_z + 40960u), 6554);       /* / 10 */
        int16_t cx5 = (int16_t)mulhi((uint16_t)(cam_x + 25600), 13108) - 5120; /* / 5 */
        /* P1 (facing +X) sees the stripes across mirrored: its parity flips */
        fu_f = cam ? fz : (uint8_t)-fz;
        fu_cx = cx5;
        fu_sub = cam;
        fu_flip = cam ? 0 : 0x80;
        fu_dl = dls[back] + pane_ofs[p];
        fu_o1 = fo1[p];
        fu_o2 = fo2[p];
        floor_update();
    }

    /* the posts of the goal ahead (the other side's), z = -40, 0, 40: the
       middle one, the others +/- 90 x 40 / depth; from the ground to 8
       units up */
    po_len = 0;
    if (proj(cam ? -PITCH_LENGTH : PITCH_LENGTH, 0)) {
        c = o_sx;
        d = (int16_t)mulhi(14400, o_inv);   /* 3600 x 4 */
        g = o_g;
        y1 = g > 63 ? 95 : 32 + (int16_t)g;
        y0 = g > 7 ? 0 : 32 - ((int16_t)g << 2);
        if (p) { if (y0 < 2) y0 = 2; }
        else if (y1 > 94) y1 = 94;
        if (cam) d = -d;
        post_x[1] = c;
        post_x[0] = c + d;                  /* z = -40: P1's right */
        post_x[2] = c - d;
        if (y1 >= y0) {
            po_dst = (uint8_t)(base + y0);
            po_len = (uint8_t)(y1 - y0 + 1);
        }
    }
    for (i = 0; i < 3; ++i) {
        po_m = i;
        rg[4 + i] = 0;
        if (po_len) {
            /* the original's clamp to the screen: a post off to the side
               stands at its edge */
            c = post_x[i];
            rg[4 + i] = (uint8_t)(48 + (c < 0 ? 0 : c > 159 ? 159 : c));
        }
        pm_post();
    }

    /* the other skimmer at its ground point, scale 360 / depth = 4 g / 3 */
    rg[2] = rg[3] = 0;
    idx = 0xFF;
    if (proj(sk_x[other], sk_z[other])) {
        g = o_g;
        if (g >= 30) idx = 40 - SKIM_S0;
        else {
            g8 = (uint8_t)g;
            g8 = (uint8_t)(g8 + (g8 >> 2) + (g8 >> 4));
            idx = (uint8_t)((g8 < 6 ? 6 : g8) - SKIM_S0);
        }
    }
    if (idx != 0xFF) {
        w = skim_w[idx];
        pp_h = skim_h[idx];
        pp_top = (int8_t)((g > 63 ? 95 : 32 + (uint8_t)g) - skim_g[idx]);
        src = skim_data + skim_ofs[idx];
        pp_n = 2;
        pp_src = src;
        pm_put();
        pp_n = 3;
        pp_src = src + pp_h;
        pm_put();
        rg[2] = rg[3] = (uint8_t)(48 + o_sx - 4 * w);
        rg[9] = rg[10] = size_of[w];
    } else {
        pp_h = 0;
        pp_n = 2;
        pm_put();
        pp_n = 3;
        pm_put();
    }

    /* the ball, centred on the horizon; radius 180 / depth = 2 g / 3.
       Closer than the near clip (the camera carries it): at the centre,
       its biggest */
    rg[0] = rg[1] = 0;
    idx = 0xFF;
    {
        int16_t bx = ball_x - cam_x;
        if (cam) bx = -bx;
        if (bx > 0 && bx < ONE / 2) {
            o_sx = 80;
            idx = BALL_N - 1;
        } else if (proj(ball_x, ball_z)) {
            g = o_g;
            if (g >= 48) idx = 32 - BALL_R0;
            else {
                g8 = (uint8_t)g;
                g8 = (uint8_t)((g8 >> 1) + (g8 >> 3) + (g8 >> 5));
                idx = (uint8_t)((g8 < 3 ? 3 : g8) - BALL_R0);
            }
        }
    }
    if (idx != 0xFF) {
        w = ball_w[idx];
        pp_h = ball_h[idx];
        pp_top = (int8_t)(32 - ball_c[idx]);
        src = ball_data + ball_ofs[idx];
        pp_n = 0;
        pp_src = src;
        pm_put();
        pp_n = 1;
        pp_src = src + pp_h;
        pm_put();
        rg[0] = rg[1] = (uint8_t)(48 + o_sx - 4 * w);
        rg[7] = rg[8] = size_of[w];
    } else {
        pp_h = 0;
        pp_n = 0;
        pm_put();
        pp_n = 1;
        pm_put();
    }
    memcpy(rgs[p], rg, 14);
}

void view_frame(void)
{
    view_cam[back][0] = sk_x[0];
    view_cam[back][1] = sk_z[0];
    view_cam[back][2] = sk_x[1];
    view_cam[back][3] = sk_z[1];
    view_hud();
    pane(0);
    pane(1);
    dl_next = back ? 0xA2 : 0xA0;
    back ^= 1;
}

void view_init(void)
{
    SDMCTL = 0;                             /* off a frame: the OS screen is in our way */
    frame_wait();
    frame_wait();
    memset((void *)0xA000, 0, 0x660);
    memset(BLACK, 0xFF, 40);
    floor_build();
    {
        uint8_t t;
        for (t = 0; t < 114; ++t) off90[t] = (uint8_t)((t * 90u) >> 7);
    }
    ov[0] = ov[1] = none;
    dl_build(DL0);
    dl_build(DL1);
    back = 1;

    rg_top[11] = C_P1;                      /* the top pane shows P1's skimmer, */
    rg_top[12] = C_P1;                      /* P1's goal posts */
    rg_top[13] = C_SKY_TOP;
    rg_bot[11] = C_P2;
    rg_bot[12] = C_P2;
    rg_bot[13] = C_SKY_BOT;
    COLOR0 = C_GRID_A;
    COLOR1 = C_GRID_B;
    COLOR2 = C_BLACK;
    COLOR4 = C_SKY_TOP;
    PCOLR0 = C_SPOT;
    *(uint8_t *)0x2C1 = C_BALL;
    *(uint8_t *)0x2C3 = C_EDGE;

    memset((void *)0x9B00, 0, 0x500);
    PMBASE = 0x98;
    GPRIOR = 0x31;                          /* players over the floor, the fifth
                                               player, OR'd colours */
    GRACTL = 3;
    SDLSTL = (uint16_t)DL0;
    irq_install();
    SDMCTL = 0x3E;                          /* normal width, PMG single line */
}
