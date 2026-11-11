/*
 * board.c - the board on the screen.
 *
 * The board's 13 x 13 grid points are worked out on the host for 3 x 3
 * tilts (tools/gen_view.py: the original's camera, the tilt drawn twice as
 * big as it is) (port: the original projects ~500 points a frame; the
 * Spectrum Next port also uses host-made tables). A new board (the tilt
 * crossed a step, a gate opened or shut, a new level) is drawn into the
 * hidden buffer a row a frame, far to near (the floor in runs of a kind,
 * then the walls' tops and fronts), then shown; meanwhile the marble keeps
 * moving: it is a player, placed every frame by interpolating the shown
 * board's grid, with its shadow (a second player).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a3d.h"
#include "mt.h"
#include "board.h"
#include "view_tab.h"

/* this file's BSS (and -Cl's static locals) in the SDK's LOWRAM */
#pragma bss-name ("LOWRAM")

static uint8_t shown_view = 0xFF, shown_gates;
static uint8_t stage;                   /* 0 idle; 1-4 clear; 5..16 rows; 17 the edge; 18 show */
static uint8_t build_view, build_gates;
static const uint8_t *vx, *vy, *rise;

/* port: tables, not cc65 multiplies */
static const uint8_t row13[14] = { 0, 13, 26, 39, 52, 65, 78, 91, 104, 117, 130, 143, 156, 169 };
static const uint16_t c40[12] = { 0, 40, 80, 120, 160, 200, 240, 280, 320, 360, 400, 440 };
#define V(c, r)  (row13[r] + (c))
static const uint8_t *shown_x, *shown_y;           /* the shown view's grid */

/* what a cell is drawn as */
enum { K_NONE, K_FLOOR, K_SAND, K_CP, K_EXIT, K_GATE, K_WALL };
static uint8_t kind(char ch, uint8_t gates)
{
    switch (ch) {
    case '.': case 'S': case 'B': return K_FLOOR;
    case 'Z': return K_SAND;
    case 'C': return K_CP;
    case 'E': return K_EXIT;
    case 'G': return gates ? K_GATE : K_WALL;
    case '#': return K_WALL;
    }
    return K_NONE;                      /* a hole */
}
/* patterns (even, odd lines): 1 floor, 2 walls, 3 the accent (exit, checkpoints) */
static const uint8_t pat_e[7] = { 0, 0x55, 0x76, 0xDD, 0xFF, 0x9A, 0xAA };
static const uint8_t pat_o[7] = { 0, 0x55, 0x67, 0x77, 0xFF, 0x6A, 0xAA };

static void quad(uint8_t c0, uint8_t r0, uint8_t c1, uint8_t r1, uint8_t up0, uint8_t up1)
{
    /* the cells (c0..c1) x (r0..r1): far edge up, clockwise on the screen */
    a3d_px[0] = vx[V(c0, r0)]; a3d_py[0] = vy[V(c0, r0)] - up0;
    a3d_px[1] = vx[V(c1, r0)]; a3d_py[1] = vy[V(c1, r0)] - up0;
    a3d_px[2] = vx[V(c1, r1)]; a3d_py[2] = vy[V(c1, r1)] - up1;
    a3d_px[3] = vx[V(c0, r1)]; a3d_py[3] = vy[V(c0, r1)] - up1;
    a3d_poly(4);
}

static void draw_row(uint8_t r)
{
    uint8_t c, e, k;
    const char *row = G.cell[r];
    /* the floor in runs */
    for (c = 0; c < MT_N; c = e) {
        k = kind(row[c], build_gates);
        for (e = c + 1; e < MT_N && kind(row[e], build_gates) == k; ++e)
            ;
        if (k == K_NONE || k == K_WALL) continue;
        a3d_pattern(pat_e[k], pat_o[k]);
        quad(c, r, e, r + 1, 0, 0);
    }
    /* the bumpers: a diamond on the floor */
    for (c = 0; c < MT_N; ++c)
        if (row[c] == 'B') {
            uint8_t x = (vx[V(c, r)] + vx[V(c + 1, r + 1)]) >> 1, y = (vy[V(c, r)] + vy[V(c + 1, r + 1)]) >> 1;
            uint8_t w = (vx[V(c + 1, r)] - vx[V(c, r)]) >> 2;
            a3d_colour(3);
            a3d_px[0] = x; a3d_py[0] = y - 2;
            a3d_px[1] = x + w; a3d_py[1] = y;
            a3d_px[2] = x; a3d_py[2] = y + 2;
            a3d_px[3] = x - w; a3d_py[3] = y;
            a3d_poly(4);
        }
    /* the walls: the top, raised, then the front */
    for (c = 0; c < MT_N; c = e) {
        k = kind(row[c], build_gates);
        for (e = c + 1; e < MT_N && kind(row[e], build_gates) == k; ++e)
            ;
        if (k != K_WALL) continue;
        a3d_colour(2);
        quad(c, r, e, r + 1, rise[r], rise[r + 1]);
        a3d_pattern(0x88, 0x22);
        a3d_px[0] = vx[V(c, r + 1)]; a3d_py[0] = vy[V(c, r + 1)] - rise[r + 1];
        a3d_px[1] = vx[V(e, r + 1)]; a3d_py[1] = vy[V(e, r + 1)] - rise[r + 1];
        a3d_px[2] = vx[V(e, r + 1)]; a3d_py[2] = vy[V(e, r + 1)];
        a3d_px[3] = vx[V(c, r + 1)]; a3d_py[3] = vy[V(c, r + 1)];
        a3d_poly(4);
    }
}

uint8_t board_view(void)
{
    uint8_t p = G.pitch < -15 ? 0 : G.pitch > 15 ? 2 : 1;
    uint8_t r = G.roll < -15 ? 0 : G.roll > 15 ? 2 : 1;
    return p * 3 + r;
}

void board_invalidate(void) { shown_view = 0xFF; }

uint8_t board_step(void)
{
    uint8_t v = board_view();
    if (!stage) {
        if (v == shown_view && G.gates_open == shown_gates) return 0;
        stage = 1;
        build_view = v;
        build_gates = G.gates_open;
        vx = view_x[v]; vy = view_y[v]; rise = view_rise[v];
    }
    if (stage <= 4) {                               /* the void */
        a3d_clear_rows((stage - 1) * 22, stage * 22, 0);
    } else if (stage <= 16) {
        draw_row(stage - 5);
    } else if (stage == 17) {                       /* the slab's near edge */
        a3d_pattern(0x88, 0x22);
        a3d_px[0] = vx[V(0, 12)]; a3d_py[0] = vy[V(0, 12)];
        a3d_px[1] = vx[V(12, 12)]; a3d_py[1] = vy[V(12, 12)];
        a3d_px[2] = vx[V(12, 12)]; a3d_py[2] = vy[V(12, 12)] + 3;
        a3d_px[3] = vx[V(0, 12)]; a3d_py[3] = vy[V(0, 12)] + 3;
        a3d_poly(4);
    } else {
        a3d_show();
        shown_view = build_view;
        shown_x = vx; shown_y = vy;
        shown_gates = build_gates;
        stage = 0;
        return 1;
    }
    ++stage;
    return 0;
}

/* ---------------------------------------------------------------- the marble */
static const uint8_t ball[2][10] = {
    { 0x30, 0x78, 0xFC, 0xFC, 0xFC, 0xFC, 0x78, 0x30, 0, 0 },     /* near */
    { 0x00, 0x70, 0xF8, 0xF8, 0xF8, 0x70, 0x00, 0x00, 0, 0 },     /* far */
};
static const uint8_t shadow[6] = { 0x78, 0xFC, 0xFC, 0x78, 0, 0 };
static uint8_t last_y, last_sy;

static const uint8_t div40[60] = {
    0,0,0,0,0, 1,1,1,1,1, 2,2,2,2,2, 3,3,3,3,3, 4,4,4,4,4, 5,5,5,5,5,
    6,6,6,6,6, 7,7,7,7,7, 8,8,8,8,8, 9,9,9,9,9, 10,10,10,10,10, 11,11,11,11,11 };
static const uint8_t frac40[40] = {
    0, 6, 13, 19, 26, 32, 38, 45, 51, 58, 64, 70, 77, 83, 90, 96, 102, 109, 115, 122,
    128, 134, 141, 147, 154, 160, 166, 173, 179, 186, 192, 198, 205, 211, 218, 224, 230, 237, 243, 250 };

/* the shown board's point at board coordinates (1/64 unit) */
static void at(int16_t x, int16_t z, int16_t *sx, int16_t *sy)
{
    const uint8_t *px, *py;
    int16_t u = (x >> 6) + 240, w = 240 - (z >> 6);
    uint8_t c, r, fu, fw, i;
    int16_t a, top, bot;
    if (u < 0) u = 0;
    if (u > 479) u = 479;
    if (w < 0) w = 0;
    if (w > 479) w = 479;
    c = div40[(uint8_t)(u >> 3)]; r = div40[(uint8_t)(w >> 3)];
    fu = frac40[(uint8_t)(u - c40[c])];
    fw = frac40[(uint8_t)(w - c40[r])];
    i = row13[r] + c;
    px = shown_x + i; py = shown_y + i;
    a = px[0]; top = a + a3d_mulf(px[1] - a, fu);
    a = px[13]; bot = a + a3d_mulf(px[14] - a, fu);
    *sx = top + a3d_mulf(bot - top, fw);
    a = py[0]; top = a + a3d_mulf(py[1] - a, fu);
    a = py[13]; bot = a + a3d_mulf(py[14] - a, fu);
    *sy = top + a3d_mulf(bot - top, fw);
}

void marble_init(void)
{
    pmg_init();
    pmg_color(0, 0x0E);                 /* the marble */
    pmg_color(1, 0x00);                 /* the shadow */
    pmg_color(2, 0x0A);
    pmg_priority(1);
}

void marble_draw(void)
{
    uint8_t *p0 = pmg_player_mem(0), *p1 = pmg_player_mem(1);
    int16_t sx, sy, y;
    uint8_t isfar, drop;
    if (last_y) { memset(p0 + last_y, 0, 10); }
    if (last_sy) { memset(p1 + last_sy, 0, 6); }
    last_y = last_sy = 0;
    if (shown_view == 0xFF || G.between) return;
    at(G.x, G.z, &sx, &sy);
    isfar = sy < 40;
    drop = G.falling ? (MT_FALL_T - G.falling) : 0;
    y = 32 + sy * 2 - 8 + drop * 2;                  /* the ball sits on the floor */
    if (!G.falling) {
        last_sy = (uint8_t)(32 + sy * 2 - 2);
        memcpy(p1 + last_sy, shadow, 6);
        pmg_x(1, (uint8_t)(48 + sx - 2));
    }
    if (drop < 12 && y > 30 && y < 210) {
        last_y = (uint8_t)y;
        memcpy(p0 + last_y, ball[isfar | (drop > 4)], 10);
        pmg_x(0, (uint8_t)(48 + sx - 3));
    }
}
