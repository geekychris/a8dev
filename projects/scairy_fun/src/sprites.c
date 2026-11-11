/*
 * sprites.c - the car, the traffic and the cones as player/missile
 * graphics over the road (the original draws them as 3D models). The car
 * is players 2 and 3 side by side at the bottom of the view; the two
 * nearest objects ahead are players 0 and 1, projected like the road and
 * drawn at their size (single, double or quad width, 1-12 rows); the
 * nearest cones are the missiles, in COLPF3 (the fifth player).
 */
#pragma static-locals (on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "sc.h"
#include "render.h"
#include "sprites.h"

extern uint8_t ktab8[513];
extern uint8_t mk;
extern int16_t __fastcall__ mulk(int16_t v);

#define PL(n)   ((uint8_t *)(0x9C00 + ((n) << 8)))
#define MIS     ((uint8_t *)0x9B00)
#define LINE(r) (VIEW_LINE0 + ((r) << 1))  /* the scan line of view row r */

/* the car from behind: 16 pixels (two players), 9 rows of 2 lines */
static const uint8_t car_l[18] = {
    0x0F, 0x0F, 0x1F, 0x1F, 0x30, 0x30, 0x7F, 0x7F, 0xFF, 0xFF,
    0xEF, 0xEF, 0xFF, 0xFF, 0x7F, 0x7F, 0x60, 0x60 };
static const uint8_t car_r[18] = {
    0xF0, 0xF0, 0xF8, 0xF8, 0x0C, 0x0C, 0xFE, 0xFE, 0xFF, 0xFF,
    0xF7, 0xF7, 0xFF, 0xFF, 0xFE, 0xFE, 0x06, 0x06 };
#define CAR_ROWS 18
#define CAR_Y    (LINE(VROWS) - CAR_ROWS - 2)

/* what an object looks like: colour, half width and height (units) */
static const uint8_t ocol[OBJ_COUNT] = { 0, 0x34, 0x0C, 0xC6, 0x86, 0x24, 0x2A, 0x04, 0x0E, 0xF6, 0x56 };
static const uint8_t car_cols[4] = { 0x34, 0x84, 0xE6, 0x0A };
static const uint8_t ohw[OBJ_COUNT] = { 0, 15, 20, 16, 5, 10, 4, 10, 17, 8, 4 };
static const uint8_t oht[OBJ_COUNT] = { 0, 22, 48, 30, 26, 22, 12, 8, 36, 14, 26 };

static uint8_t old_y[2], old_h[2], old_my[4];

/* silhouettes, 8 x 8 (from behind): a car, a truck (also the ambulance and
   the tractor), a person or animal, debris */
static const uint8_t sh_car[8]   = { 0x3C, 0x7E, 0x42, 0xFF, 0xFF, 0xDB, 0xFF, 0x66 };
static const uint8_t sh_truck[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDB, 0xFF, 0x66 };
static const uint8_t sh_fig[8]   = { 0x18, 0x3C, 0x18, 0x3C, 0x3C, 0x3C, 0x24, 0x24 };
static const uint8_t sh_deb[8]   = { 0x00, 0x00, 0x00, 0x18, 0x3C, 0x7E, 0xFF, 0xFF };
static const uint8_t *const shape_of[OBJ_COUNT] = {
    sh_car, sh_car, sh_truck, sh_truck, sh_fig, sh_fig, sh_deb, sh_deb, sh_truck, sh_fig, sh_fig };
/* the middle w pixels of 8 */
static const uint8_t cut[9] = { 0x00, 0x18, 0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0xFF };

void sprites_init(void)
{
    pmg_init();
    GPRIOR = 0x11;                        /* players over the road, the fifth player */
    memcpy(PL(2) + CAR_Y, car_l, CAR_ROWS);
    memcpy(PL(3) + CAR_Y, car_r, CAR_ROWS);
    PCOLR0 = 0x34;
    pmg_color(2, 0x36);
    pmg_color(3, 0x36);
    COLOR3 = 0x2A;                        /* (missiles: the cones' orange) */
}

/* the camera's view of a point: x pixel (0 = left edge) and row, by its
   distance zr (units, > 48), road position z and offset du (units) */
static int16_t sx, sy;
static uint8_t project_pt(uint16_t z, int16_t du)
{
    static int16_t zr, line;
    static uint8_t k;
    zr = (int16_t)(z - cam_z);
    if (zr <= 64 || zr >= 4000) return 0;
    k = ktab8[(uint16_t)zr >> 3];
    mk = k;
    line = ((cam_dir16 >> 4) * (zr >> 4)) >> 3;         /* dir16 * zr / 2048, in 16 bits */
    sx = 80 + (mulk(road_cx(z, 0) + du - cam_x - line) >> 4);
    sy = HOR + (mulk(cam_y - road_y(z)) >> 4);
    mk = k;
    return k;
}

void sprites_frame(void)
{
    static uint8_t i, n, best[2], k, w8, h, size, j, r, c, *p;
    static const uint8_t *sh;
    static int16_t dz, bd[2], cd;
    static uint16_t z;
    static int16_t seg, s;

    /* the car (it moves with the steering as the camera lags) */
    sx = 80 + (((w.car.d >> 4) - cam_d) * 90 / 230);
    pmg_x(2, PMG_X0 + sx - 8);
    pmg_x(3, PMG_X0 + sx);
    pmg_color(2, w.car.state >= CS_SPLASH ? 0x86 : 0x36);
    pmg_color(3, w.car.state >= CS_SPLASH ? 0x86 : 0x36);

    /* the two nearest objects ahead */
    best[0] = best[1] = 0xFF;
    bd[0] = bd[1] = 0x7FFF;
    for (i = 0; i < MAX_OBJS; ++i) {
        if (!obj_type[i] || obj_type[i] == OBJ_CONE) continue;
        dz = (int16_t)(obj_z[i] - cam_z);
        if (dz <= 64 || dz > 3800) continue;
        if (dz < bd[0]) { bd[1] = bd[0]; best[1] = best[0]; bd[0] = dz; best[0] = i; }
        else if (dz < bd[1]) { bd[1] = dz; best[1] = i; }
    }
    for (n = 0; n < 2; ++n) {
        p = PL(n);
        if (old_h[n]) memset(p + old_y[n], 0, old_h[n]);
        old_h[n] = 0;
        i = best[n];
        if (i == 0xFF || !(k = project_pt(obj_z[i], obj_d[i] >> 4))) { pmg_x(n, 0); continue; }
        /* its size: width and height in pixels (k = scale / 2: px = u * k / 128) */
        w8 = (uint8_t)(((uint16_t)ohw[obj_type[i]] * k) >> 6);       /* full width */
        h = (uint8_t)(((uint16_t)oht[obj_type[i]] * k) >> 7);        /* rows */
        if (w8 < 1) w8 = 1;
        if (h < 1) h = 1;
        if (h > 24) h = 24;
        size = 0;
        if (w8 > 8) { size = 1; w8 = (w8 + 1) >> 1; }
        if (w8 > 8) { size = 3; w8 = (w8 + 1) >> 1; }
        if (w8 > 8) w8 = 8;
        if (sy > VROWS) sy = VROWS;
        if (sy - (int16_t)h < 0) h = (uint8_t)sy;
        if (!h || sx < -40 || sx > 200) { pmg_x(n, 0); continue; }
        /* its shape: an 8-row silhouette sampled to h rows, cut to w8
           pixels (centred); h rows of two scan lines */
        sh = shape_of[obj_type[i]];
        j = cut[w8];
        old_y[n] = LINE(sy - h);
        old_h[n] = h << 1;
        for (r = 0; r < h; ++r) {
            c = sh[(r << 3) / h] & j;
            p[old_y[n] + (r << 1)] = c;
            p[old_y[n] + (r << 1) + 1] = c;
        }
        pmg_size(n, size);
        pmg_x(n, (uint8_t)(PMG_X0 + sx - (4 << size)));   /* (the shape is centred in 8 bits) */
        pmg_color(n, obj_type[i] == OBJ_CAR ? car_cols[obj_colour[i]] : ocol[obj_type[i]]);
    }

    /* the cones of a closed lane: the nearest four as missiles */
    for (j = 0; j < 4; ++j) if (old_my[j]) { MIS[old_my[j]] &= ~(3 << (j << 1)); MIS[old_my[j] + 1] &= ~(3 << (j << 1)); old_my[j] = 0; }
    seg = seg_of(cam_z) + 2;
    n = 0;
    for (s = 0; s < 14 && n < 4; ++s) {
        if (seg + s >= w.nseg || !(seg_flags[seg + s] & SF_CONES)) continue;
        if (w.knocked[(seg + s) >> 3] & (1 << ((seg + s) & 7))) continue;
        if (!cone_at(seg + s, &cd)) continue;
        z = (uint16_t)(seg + s) << SEG_BITS;
        if (!project_pt(z, cd) || sy < 1 || sy > VROWS || sx < 0 || sx > 159) continue;
        old_my[n] = LINE(sy - 1);
        MIS[old_my[n]] |= 3 << (n << 1);
        MIS[old_my[n] + 1] |= 3 << (n << 1);
        pmg_missile_x(n, PMG_X0 + sx);
        n++;
    }
    for (; n < 4; ++n) pmg_missile_x(n, 0);
}
