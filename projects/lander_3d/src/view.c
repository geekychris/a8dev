/*
 * view.c - Lander 3D's picture.
 *
 * The camera follows the craft from behind and above (60 m back, 22 m up)
 * and never turns (port: the original turns it to look down at the craft;
 * the Spectrum Next port also keeps its yaw at 0). It looks level, with the
 * picture shifted up (the horizon on line 7 of 44, a lens shift instead of
 * a pitch): a grid point then projects with one multiply, and the craft
 * stays at the same place on the screen, as two players (a3d's polygons
 * draw the ground). The ground is a window of 8 x 7 cells round the craft,
 * drawn far to near (each cell over what is behind it), shaded by its
 * slope; beyond it the plain. The pads are octagons over the ground.
 * Lines are shown twice (a3d_double): 160 x 44 to fill.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a3d.h"
#include "ld.h"
#include "view.h"
#include "sprites.h"

/* all of this file's BSS (its tables and cc65's static locals, -Cl) in LOWRAM */
#pragma bss-name ("LOWRAM")

extern uint8_t a3d_ylo[96], a3d_yhi[96];
extern uint8_t a3d_bsel;
#pragma zpsym ("a3d_bsel")

#define FX_   80                        /* focal length across (pixels) */
#define FY_   34                        /* ... and down (lines shown twice are ~2.3x taller) */
#define CAM_D 60                        /* metres behind */
#define CAM_H 22                        /* metres above */
#define COLS  6                         /* grid points across the window */
#define ROWS  5                         /* ... and in depth */
#define SPR_Y 102                       /* the lander's top scan line */
#define SPR_X 120                       /* its left colour clock */

uint16_t view_cycles;
static int16_t cam_x16, cam_y16, cam_z16;   /* the camera in 1/16 m */
static int16_t ssx[ROWS * COLS], ssy[ROWS * COLS];
static uint8_t gi0, gj0, ncols, nrows;

static const struct { uint8_t c0, c1, c2; uint8_t sky[7]; } pal[W_COUNT] = {
    { 0x04, 0x0A, 0xC8, { 0x00, 0x00, 0x90, 0x90, 0x92, 0x92, 0x94 } },    /* Moon: greys, green pads */
    { 0x22, 0x28, 0xCA, { 0x40, 0x42, 0x42, 0x44, 0x44, 0x46, 0x36 } },    /* Mars: rust, pink sky */
    { 0x12, 0x18, 0xAC, { 0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04 } },    /* asteroid: browns, black sky */
};
uint8_t view_sky[7];

static int16_t gpos16(uint8_t i) { return (((int16_t)i << L.cshift) - (int16_t)L.half) << 4; }

void view_world(uint8_t world)
{
    COLOR0 = pal[world].c0;
    COLOR1 = pal[world].c1;
    COLOR2 = pal[world].c2;
    memcpy(view_sky, pal[world].sky, 7);
    COLOR4 = view_sky[0];
}

void view_init(void)
{
    pmg_init();
    pmg_size(0, 1); pmg_size(1, 1); pmg_size(3, 1);
    pmg_color(0, 0x2A);                 /* the descent stage: gold */
    pmg_color(1, 0x0C);                 /* the ascent stage: grey-white */
    pmg_color(3, 0x00);                 /* the shadow */
    pmg_x(0, SPR_X); pmg_x(1, SPR_X); pmg_x(2, SPR_X + 4); pmg_x(3, SPR_X);
    pmg_priority(0x21);                 /* players over the playfield, multicolour */
    /* four stars: the missiles, one scan line each, in the sky */
    pmg_missile_mem()[36] = 0x01; pmg_missile_x(0, 70);
    pmg_missile_mem()[44] = 0x04; pmg_missile_x(1, 111);
    pmg_missile_mem()[40] = 0x10; pmg_missile_x(2, 160);
    pmg_missile_mem()[49] = 0x40; pmg_missile_x(3, 189);
}

/* project the window's grid points (port: a row's x step is one multiply,
   each point's y one more) */
extern int8_t *pr_h;
extern uint8_t pr_n;
extern int16_t pr_x, pr_step, pr_dy, pr_ky, *pr_sx, *pr_sy;
extern void proj_row(void);
extern void ld_service(void);     /* game.c: the controls and the sprites, between rows */

static void project(void)
{
    uint8_t r;
    int16_t z16, kx, x16 = gpos16(gi0) - cam_x16, z0 = gpos16(gj0) - cam_z16, dz = (int16_t)L.cell << 4;
    pr_n = ncols;
    pr_dy = cam_y16 >> 4;
    pr_h = &L.h[((uint16_t)gj0 << 5) + gi0];
    pr_sx = ssx; pr_sy = ssy;
    for (r = 0, z16 = z0; r < nrows; ++r, z16 += dz) {
        kx = a3d_muldiv(FX_, 4096, z16);
        pr_ky = a3d_mulf(kx, FY_ * 256 / FX_) << 4;             /* (h in whole m: x 16) */
        pr_x = a3d_mulf(x16, kx);                               /* 1/16 pixel */
        pr_step = a3d_mulf(dz, kx);
        proj_row();
        pr_h += LD_S;
        pr_sx += ncols; pr_sy += ncols;
    }
}

/* the ground's shades in its two colours (the third is the pads') */
static const uint8_t shade_e[4] = { 0x55, 0x65, 0x66, 0xAA };
static const uint8_t shade_o[4] = { 0x55, 0x56, 0x99, 0xAA };

static void cells(void)
{
    uint8_t r, c, j, i;
    int8_t *h0, *h1;
    int16_t d, *fx_, *fy, *nx, *ny;
    for (r = nrows - 1; r > 0; --r) {                           /* far to near */
        j = gj0 + r - 1;
        h0 = &L.h[((uint16_t)j << 5) + gi0];                    /* the near row */
        h1 = h0 + LD_S;                                         /* the far row */
        fx_ = ssx + r * COLS; fy = ssy + r * COLS;
        nx = fx_ - COLS; ny = fy - COLS;
        for (c = 0; c + 1 < ncols; ++c) {
            /* lit when it rises away from us and falls to the right */
            d = (h1[c] - h0[c]) * 2 + (h0[c] - h0[c + 1]) + 4;
            i = d < 0 ? 0 : d < 6 ? 1 : d < 14 ? 2 : 3;
            a3d_pattern(shade_e[i], shade_o[i]);
            a3d_px[0] = fx_[c];     a3d_py[0] = fy[c];
            a3d_px[1] = fx_[c + 1]; a3d_py[1] = fy[c + 1];
            a3d_px[2] = nx[c + 1];  a3d_py[2] = ny[c + 1];
            a3d_px[3] = nx[c];      a3d_py[3] = ny[c];
            a3d_poly(4);
        }
        ld_service();
    }
}

static const int8_t oct[4][2] = { { 4, -3 }, { 4, 3 }, { -4, 3 }, { -4, -3 } };    /* port: a square */
static const uint8_t pad_pat[3][2] = { { 0xFF, 0xFF }, { 0xBB, 0xEE }, { 0x77, 0xDD } };

/* a pad: an octagon at its height; one scale for all its corners, from its
   middle's distance (port: they are small) */
static void pads(void)
{
    uint8_t k, v;
    int16_t z16, kx, ky, x16, dy, r16, sy0;
    const Pad *p;
    for (k = 0; k < 3; ++k) {
        p = &L.pads[k];
        r16 = p->r4 << 2;
        z16 = (p->z << 4) - cam_z16;
        if (z16 < 160 + r16 || z16 > 6400) continue;            /* too near (or behind), too far */
        kx = a3d_muldiv(FX_, 4096, z16);
        ky = a3d_muldiv(FY_, 4096, z16);
        dy = cam_y16 - ((int16_t)p->y << 4);
        sy0 = V_HORIZON + (a3d_mulf(dy, ky) >> 4);
        x16 = a3d_mulf((p->x << 4) - cam_x16, kx);              /* the middle, 1/16 pixel */
        if (x16 < -1600 || x16 > 1600 || sy0 < 0 || sy0 > V_LINES + 8) continue;     /* off the screen */
        /* the depth's effect on y: the scale's change, ~ -dy * dz / z (one
           divide for the pad, the corners scale it) */
        dy = a3d_mulf(a3d_muldiv(dy, r16, z16), ky);
        r16 = a3d_mulf(r16, kx);
        for (v = 0; v < 4; ++v) {
            /* clockwise on the screen: the far side up (the octagon seen from above) */
            a3d_px[v] = 80 + ((x16 + ((oct[v][1] * r16) >> 2)) >> 4);
            a3d_py[v] = sy0 - (((oct[v][0] * dy) >> 2) >> 4);
        }
        a3d_pattern(pad_pat[k][0], pad_pat[k][1]);
        a3d_poly(4);
    }
}

void view_draw(void)
{
    int16_t ci, cj;
    cam_x16 = (ld_m[0] << 4) + (ld_frac[0] >> 4);
    cam_y16 = (ld_m[1] << 4) + (ld_frac[1] >> 4) + CAM_H * 16;
    cam_z16 = (ld_m[2] << 4) + (ld_frac[2] >> 4) - CAM_D * 16;
    /* the window: 8 cells across round the craft, 7 deep from just ahead of the camera */
    ci = (((cam_x16 >> 4) + (int16_t)L.half) >> L.cshift) - (COLS - 1) / 2;
    cj = (((cam_z16 >> 4) + 12 + (int16_t)L.half) >> L.cshift) + 1;
    if (ci < 0) ci = 0;
    if (ci > LD_N - COLS) ci = LD_N - COLS;
    if (cj < 0) cj = 0;
    gi0 = (uint8_t)ci;
    gj0 = (uint8_t)cj;
    ncols = COLS;
    nrows = cj + ROWS > LD_N ? (uint8_t)(LD_N - cj) : ROWS;
    a3d_clear_rows(0, V_HORIZON, 0x00);
    a3d_clear_rows(V_HORIZON, V_LINES, 0x55);                   /* the plain */
    if (nrows >= 2) {
        project();
        ld_service();
        cells();
    }
    pads();
}

void view_sprites(uint8_t flame)
{
    uint8_t *p0 = pmg_player_mem(0), *p1 = pmg_player_mem(1), *p2 = pmg_player_mem(2), *p3 = pmg_player_mem(3);
    int8_t f = (int8_t)(-L.roll / 1638);                        /* 9 degrees a frame */
    uint8_t i, n;
    int16_t g, dy, sy;
    static uint8_t shadow_y;
    if (f < -2) f = -2;
    if (f > 2) f = 2;
    memcpy(p0 + SPR_Y, spr_p0[f + 2], SPR_H);
    memcpy(p1 + SPR_Y, spr_p1[f + 2], SPR_H);
    /* the flame under the bell: longer with the throttle, flickering */
    memset(p2 + SPR_Y + 18, 0, 20);
    if (flame) {
        n = (uint8_t)(flame >> 5) + (RANDOM & 3);
        for (i = 0; i < n && i < 20; ++i) p2[SPR_Y + 18 + i] = i < 2 ? 0x3C : i < n / 2 ? 0x18 : 0x10;
        pmg_color(2, RANDOM & 1 ? 0x1E : 0x2C);
    }
    /* the shadow on the ground under the craft */
    if (shadow_y) memset(p3 + shadow_y, 0, 5);
    shadow_y = 0;
    g = ld_ground_here();                                       /* 1/16 m */
    dy = cam_y16 - g;
    sy = V_HORIZON + (a3d_mulf(dy, (int16_t)(FY_ * 4096L / (CAM_D * 16))) >> 4);
    if (sy >= 0 && sy < V_LINES) {
        shadow_y = (uint8_t)(32 + sy * 4);
        memcpy(p3 + shadow_y, spr_shadow, 5);
    }
}
