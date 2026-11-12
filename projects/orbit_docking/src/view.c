/*
 * view.c - Orbit Docking from the cockpit: the stars, the spinning station
 * (the hub, the slot and its lights, four arms, the ring, two solar
 * panels), the approach markers along its axis, debris, and the
 * instruments in players.
 *
 * The view rolls with the craft and never yaws or pitches (it looks back
 * down the axis), so every corner of the station lies on a plane across
 * the axis and is kept as (radius, angle) on it (tools/gen_view.py): a
 * frame works out each plane's scale and where the axis crosses it, then
 * four products a corner (proj.s), not a camera transform a corner (port:
 * the Spectrum Next port found the same). The polygons are drawn far to
 * near in the original's depth order by a3d (ANTIC mode D, double
 * buffered), its back-face test hiding the sides that face away.
 *
 * Players: the sight (0), the roll dial with its needle (1), the axis box
 * (2) and the craft's dot in it (missile 3, in player 3's colour: green on
 * the axis, red off it).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a3d.h"
#include "od.h"
#include "view.h"
#include "view_tab.h"

#if NPLANES > 12 || NCLS > 24
#error "proj.s: more room for the planes and classes (NPL, NCL)"
#endif

#pragma bss-name ("LOWRAM")

extern uint8_t pl_phi[NPLANES], pj_ncls, st_roll, star_n;
extern void rot_stars(void);
extern int16_t pl_cx[NPLANES], pl_cy[NPLANES], cr_x[NCLS], cr_y[NCLS], pv_x[NVERTS], pv_y[NVERTS];
extern int16_t pl_kx[NPLANES], pl_ky[NPLANES], ps_cz, ps_ox, ps_oy;
extern uint8_t ps_nplanes;
extern void proj_setup(void);
extern uint16_t pk_tab[128];
extern void proj_all(void);

#define LINES 88
#define FXN   29440u                    /* 115 pixels * 256: the focal length across */
#define FYN   25088u                    /* 98 lines * 256 (mode D's pixels are taller) */
#define NEAR  12

uint8_t pl_on[NPLANES];
extern uint8_t dp_lod, dp_npolys, dp_first;
extern int16_t rg_cx, rg_cy, rg_ox, rg_oy, rg_ix, rg_iy;
extern uint16_t rg_so, rg_si;
extern void ring_draw(void);
uint8_t ell_w[256];                     /* 255 sqrt(1 - (t/256)^2), made at start-up */
extern uint16_t od_isqrt(uint32_t v);
extern void draw_polys(void);
static int16_t ox, oy;                  /* the camera's offset across, turned by the roll (units) */
static int8_t rc, rs;                   /* the roll's cosine and sine, Q7 */
static uint8_t roll8;
static uint8_t star_roll = 1;
uint8_t star_sx[NSTARS], star_sy[NSTARS];
extern void plot_stars(void);
static uint8_t dial_n = 0xFF, dot_y, full_clear = 2;


#define MULQ7(v, q) a3d_mulf((v), (int16_t)(q) * 2)     /* v * q / 128 */

/* ---------------------------------------------------------------- set-up */
void view_init(void)
{
    uint8_t *m;
    uint16_t t;
    for (t = 0; t < 128; ++t) pk_tab[t] = (uint16_t)(7536640UL / (t + 128));   /* 115 * 65536 / d (proj_setup) */
    for (t = 0; t < 256; ++t) ell_w[t] = (uint8_t)od_isqrt(65025UL - ((uint32_t)(t * t) * 65025UL >> 16));
    a3d_init(LINES, 2);
    COLOR4 = 0x00;                      /* space */
    COLOR0 = 0x06;                      /* the station's greys */
    COLOR1 = 0x0C;
    COLOR2 = 0x86;                      /* the panels, lights, markers */
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = 0x82;
    pmg_init();
    pmg_priority(1);
    m = pmg_player_mem(0);              /* the sight, in the middle */
    memcpy(m + 120 - 6, sight, sizeof(sight));
    pmg_x(0, 48 + 80 - 4);
    pmg_color(0, 0xEC);
    m = pmg_player_mem(2);              /* the axis box, top right */
    memcpy(m + 36, box, BOX_H);
    pmg_size(2, 1);
    pmg_x(2, 48 + 160 - 22);
    pmg_color(2, 0x08);
    pmg_size(1, 1);                     /* the dial, bottom middle */
    pmg_x(1, 48 + 80 - 8);
}

/* ---------------------------------------------------------------- the picture */
static void stars(void)
{
    star_n = NSTARS;
    if (roll8 != star_roll) {           /* they turn with the roll: worked out when it changes */
        star_roll = st_roll = roll8;
        rot_stars();
    }
    plot_stars();
}

/* a point (x, y) at world depth z, not turning with the station, on the screen; 0 if too near */
static uint8_t point(int16_t x, int16_t y, int16_t z, int16_t *sx, int16_t *sy, int16_t *kx, int16_t *ky)
{
    int16_t dz = (G.z >> 4) - z, X, Y, t;
    if (dz < 30) return 0;
    *kx = FXN / (uint16_t)dz;
    *ky = FYN / (uint16_t)dz;
    X = (G.x >> 4) - x;
    Y = y - (G.y >> 4);
    t = MULQ7(X, rc) - MULQ7(Y, rs);    /* (a3d_mulf is a macro: no products in its arguments) */
    *sx = 80 + a3d_mulf(t, *kx);
    t = MULQ7(X, rs) + MULQ7(Y, rc);
    *sy = 44 - a3d_mulf(t, *ky);
    return 1;
}

/* the approach markers: squares round the axis (their corners projected
   with the station's), drawn far to near with the debris */
static const int16_t mark_z[3] = { 150, 260, 420 };

/* port: corner ticks (a quarter of each side from each corner), not the
   whole squares: half the pixels, and they still frame the port. mk_fast
   (proj.s) draws the ticks wholly on the screen, unclipped, and returns the
   others: bit 2k corner k to k + 1, bit 2k + 1 back */
extern uint8_t __fastcall__ mk_fast(const uint8_t *v);

static void tick(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    a3d_line(x0, y0, x0 + ((x1 - x0) >> 2), y0 + ((y1 - y0) >> 2), 3);
}

static void marker(uint8_t m)
{
    const uint8_t *v = mark_v[m];
    uint8_t k, a, b, r;
    if (!pl_on[6 + m] || (G.z >> 4) - mark_z[m] < 30) return;
    r = mk_fast(v);
    for (k = 0; r; ++k, r >>= 2) {
        a = v[k]; b = v[(k + 1) & 3];
        if (r & 1) tick(pv_x[a], pv_y[a], pv_x[b], pv_y[b]);
        if (r & 2) tick(pv_x[b], pv_y[b], pv_x[a], pv_y[a]);
    }
}

static void debris(uint8_t k)
{
    int16_t cx, cy, kx, ky, r, t;
    uint8_t j, a, ang = (uint8_t)((G.level_ticks * 900u) >> 8) + k * 35;
    if (!point(od_debris_x(k), G.d_y[k], G.d_z[k], &cx, &cy, &kx, &ky)) return;
    for (j = 0; j < 6; ++j) {           /* a hexagon turning in the view, corners 24 and 14 out */
        a = ang + j * 43;
        r = (j & 1) ? 14 : 24;
        t = MULQ7(r, a3d_cos(a));
        a3d_px[j] = cx + a3d_mulf(t, kx);
        t = MULQ7(r, a3d_sin(a));
        a3d_py[j] = cy - a3d_mulf(t, ky);
    }
    a3d_pattern(0x9D, 0xD9);            /* PF1-ish: dull brown */
    a3d_poly(6);
}

static void station(void)
{
    uint8_t k, m;
    ps_cz = G.z >> 4; ps_ox = ox; ps_oy = oy;
    ps_nplanes = NPLANES;
    pj_ncls = NCLS;
    proj_setup();                       /* each plane's scale and centre, each radius scaled (proj.s) */
    k = ((uint8_t *)&G.st_angle)[2];    /* (G.st_angle >> 16, without a 32-bit shift) */
    for (m = 0; m < NPLANES; ++m)
        pl_phi[m] = plane_turns[m] ? k - roll8 : (uint8_t)-roll8;
    proj_all();
    /* port: the lights only when they're a few pixels */
    dp_lod = 1 | (cr_x[FACE_CLS] >= 12 ? 8 : 0);
    dp_first = 0;
    dp_npolys = POLYS_UNDER_RING;
    draw_polys();
    /* the ring: radius 260-300 at z = -60 (plane 3), its two edges (port) */
    if (pl_on[3]) {
        rg_cx = pl_cx[3]; rg_cy = pl_cy[3];
        rg_ox = a3d_mulf(300, pl_kx[3]); rg_oy = a3d_mulf(300, pl_ky[3]);
        rg_ix = a3d_mulf(260, pl_kx[3]); rg_iy = a3d_mulf(260, pl_ky[3]);
        if (rg_oy > 0 && rg_iy > 0 && rg_cx + rg_ox >= 0 && rg_cx - rg_ox < 160 && rg_cy + rg_oy >= 0 && rg_cy - rg_oy < LINES) {
            /* its rectangle, for the next clear (the ring is drawn outside a3d) */
            a3d_dr[0] = rg_cy - rg_oy < 0 ? 0 : (uint8_t)(rg_cy - rg_oy);
            a3d_dr[1] = rg_cy + rg_oy + 1 > LINES ? LINES : (uint8_t)(rg_cy + rg_oy + 1);
            a3d_dr[2] = rg_cx - rg_ox < 0 ? 0 : (uint8_t)(rg_cx - rg_ox);
            a3d_dr[3] = rg_cx + rg_ox > 159 ? 159 : (uint8_t)(rg_cx + rg_ox);
            a3d_dirty_mark();
            rg_so = rg_oy > 1 ? 0xFFFFu / (uint16_t)rg_oy : 0xFFFF;   /* (65536 / r: 16 bits do) */
            rg_si = rg_iy > 1 ? 0xFFFFu / (uint16_t)rg_iy : 0xFFFF;
            a3d_pattern(0xAA, 0xAA);
            ring_draw();
        }
    }
    dp_first = POLYS_UNDER_RING;
    dp_npolys = NPOLYS;
    draw_polys();
}

void view_draw(void)
{
    uint8_t k, m, d;
    int16_t cx = (G.x >> 4), cy = (G.y >> 4);
    roll8 = (uint8_t)(G.roll >> 16);
    rc = a3d_cos(roll8);
    rs = a3d_sin(roll8);
    ox = MULQ7(cx, rc) + MULQ7(cy, rs);
    oy = MULQ7(cx, rs) - MULQ7(cy, rc);
    /* the stars are everywhere, but stay put while the roll does: then only
       what was drawn over them is cleared; a roll clears it all (both buffers) */
    if (roll8 != star_roll) full_clear = 2;
    if (full_clear) { a3d_clear(0); --full_clear; }
    else a3d_clear_dirty(0);
    stars();
    station();
    /* the markers and debris, far (low z) to near */
    m = 0; d = G.ndebris;
    for (;;) {
        if (m < 3 && (d == 0 || mark_z[m] < G.d_z[d - 1])) { marker(m); ++m; }
        else if (d) { --d; debris(d); }
        else break;
    }
    (void)k;
}

/* ---------------------------------------------------------------- the instruments */
void view_instruments(void)
{
    int16_t err = od_roll_error(), x, y;
    uint8_t n = (uint8_t)(((uint16_t)err * 2) >> 12) & 15, *mm;
    int32_t lat;
    /* the dial: the needle up when lined up (the slot repeats every half turn: shown a full turn) */
    if (n != dial_n) {
        dial_n = n;
        memcpy(pmg_player_mem(1) + 180, dial[n], DIAL_H);
    }
    pmg_color(1, err < OD_TOL_ROLL && err > -OD_TOL_ROLL ? 0xBA : 0xEC);
    /* the craft against the axis: the dot in the box (18 units a pixel across, 10 a scan line) */
    mm = pmg_missile_mem();
    if (dot_y) { mm[dot_y] &= 0x3F; mm[dot_y + 1] &= 0x3F; dot_y = 0; }
    x = (G.x >> 4) / 18;
    y = (G.y >> 4) / 10;
    if (x > -8 && x < 8 && y > -13 && y < 13) {
        dot_y = (uint8_t)(36 + BOX_H / 2 - 1 - y);
        mm[dot_y] |= 0xC0; mm[dot_y + 1] |= 0xC0;
        pmg_missile_x(3, (uint8_t)(48 + 160 - 22 + 8 + x - 1));
    }
    lat = od_lat2();
    pmg_color(3, lat < (int32_t)OD_TOL_AXIS * OD_TOL_AXIS * 256 ? 0xBA : 0x36);
}
