/*
 * render.c - the road (Atari edition). The original's renderer draws each
 * 8 m segment far to near with an 8-point cross-section; here the road is
 * pseudo-3D in the OutRun manner: the segment boundaries ahead are projected
 * (centre line, height, scale), each pair makes a band of rows, and the
 * rows' shapes are drawn by rows.s (only rows whose shape changed). The
 * bands' colours (stripes, flood water, overpass shadow, the low sun) are
 * display-list interrupts (display.s); the vehicles, animals and people are
 * player/missile graphics (sprites.c).
 */
#pragma static-locals (on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "sc.h"
#include "render.h"

/* rows.s */
extern int16_t bd_c[20], bd_lw[20], bd_g[20], bd_p[20], bd_row[20];
extern uint8_t bd_mode[20], bd_side[80], bd_fw[40], bd_n;
extern uint8_t bt_top[20], bt_b[20], bt_n, bt_hor;
extern void draw_bands(void);
extern void rows_reset(void);
extern void rows_init(void);
extern void __fastcall__ sky_rows(uint8_t n);
/* mathx.s */
extern uint8_t mk, dn;
extern void mul_init(void);
extern int16_t __fastcall__ mulk(int16_t v);     /* (v * k) >> 4 for mk = k / 2 */
extern int16_t __fastcall__ mult(int16_t v);     /* v * mk */
extern int16_t __fastcall__ divn(int16_t v);
/* display.s */
extern uint8_t *dl_next;
extern uint8_t dl_base_next;
extern uint8_t dlt_bk[64], dlt_p0[64], dlt_p1[64], dlt_p2[64];

#define M_THREE  0x01
#define M_GHOST  0x02
#define M_PATH   0x04
#define M_NOLINE 0x08

#define CAM_BACK 230
#define CAM_UP   92
#define ZNEAR    48
#define NB_MAX   20

#define BITMAP   ((uint8_t *)0xA000)
#define DL_A     ((uint8_t *)0xA900)
#define DL_B     ((uint8_t *)0xAA00)
#define HUD      ((uint8_t *)0xAB00)     /* 20 (ANTIC 6) */
#define MSG      ((uint8_t *)0xAB20)     /* 2 x 40 (ANTIC 2) */

uint16_t cam_z;
int16_t cam_y, cam_dir16, cam_x;          /* camera: z, height, heading (x per 128 z, x16), x */
int16_t cam_d;                            /* the chase camera's offset, units, lagging the car */
extern uint8_t ktab8[513];                /* (project.s) 45 * 256 / zr, by zr / 8 */
extern int16_t bseg[20];
extern int16_t pj_s0, pj_segmax, pj_lfrac, pj_planfrom;
extern uint8_t pj_nb, pj_three, pj_pathok;
extern int16_t *pj_plan;
extern void project(void);
static uint8_t *dl_rows[2];              /* the view rows' instructions */
uint8_t dloff[VROWS];                    /* (colours.s) each view row's instruction in them */
static uint8_t back;                     /* the display list being made: 0 or 1 */

/* per boundary */
static uint8_t nb;
#define brow bd_row

/* a route's colours: sky (top, middle, at the horizon), ground (light,
   dark), road, line, water */
typedef struct { uint8_t sky[3], g1, g2, road, line, water, side; } Pal;
static const Pal pals[NUM_ROUTES] = {
    { { 0x92, 0x96, 0x9A }, 0xC6, 0xC4, 0x06, 0x0E, 0x86, 0xC2 },   /* levee: green, the river */
    { { 0x84, 0x88, 0x8C }, 0xB4, 0xB2, 0x04, 0x0E, 0x86, 0x08 },   /* freeway */
    { { 0x74, 0x78, 0x7C }, 0x24, 0x22, 0x06, 0x0E, 0x84, 0x26 },   /* mountain: rock */
    { { 0x42, 0x46, 0x4A }, 0x06, 0x04, 0x02, 0x0C, 0x84, 0x08 },   /* town at dusk */
};
static const Pal *pal, *last_pal;
static uint8_t last_gl = 0xFF, c_sky[3];
/* colours.s */
extern uint8_t co_g1, co_g2, co_road, co_line, co_water, co_shadow, co_path, co_nset, co_ndl;
extern uint8_t *co_dl, *co_tb, *co_set;
extern void band_colours(void);
static uint8_t dl_set[2][24], dl_nset[2];   /* the rows given DLIs last time, per list */

/* what's beside the road: colours (far, near) and the near strip's width */

extern uint8_t _SCRBSS_LOAD__[], _SCRBSS_SIZE__[];  /* (scairy_fun.cfg) */

void render_init(void)
{
    static uint16_t i, k16;
    static uint8_t *p, *q, r, k;
    /* the renderer's and sprites' variables are outside the C runtime's BSS:
       zero them first */
    memset(_SCRBSS_LOAD__, 0, (uint16_t)_SCRBSS_SIZE__);
    for (i = 1; i <= 512; ++i) {
        k16 = (uint16_t)(11520U / i) >> 3;        /* 45 * 256 / (i * 8) */
        ktab8[i] = k16 > 255 ? 255 : (uint8_t)k16;
    }
    ktab8[0] = 255;
    mul_init();
    rows_init();
    /* two display lists: HUD, the view (56 ANTIC D rows), two text rows */
    for (k = 0; k < 2; ++k) {
        p = k ? DL_B : DL_A;
        *p++ = 0x70; *p++ = 0x70;
        *p++ = 0x46; *p++ = (uint8_t)(uint16_t)HUD; *p++ = (uint16_t)HUD >> 8;
        *p++ = 0x80;                              /* 1 line: DLI 0 (the sky) */
        *p++ = 0x4D; *p++ = (uint8_t)(uint16_t)BITMAP; *p++ = (uint16_t)BITMAP >> 8;
        dl_rows[k] = p - 3;
        dloff[0] = 0;
        for (r = 1; r < VROWS; ++r) {
            dloff[r] = (uint8_t)(p - dl_rows[k]);
            if (r < DBL_ROW) *p++ = 0x0D;
            else {                                /* the near half: pairs of rows show one */
                q = BITMAP + 40 * (r & ~1);
                *p++ = 0x4D; *p++ = (uint8_t)(uint16_t)q; *p++ = (uint16_t)q >> 8;
            }
        }
        *p++ = 0x42; *p++ = (uint8_t)(uint16_t)MSG; *p++ = (uint16_t)MSG >> 8;
        *p++ = 0x02;
        *p++ = 0x41; *p++ = (uint8_t)(uint16_t)(k ? DL_B : DL_A); *p = (uint16_t)(k ? DL_B : DL_A) >> 8;
    }
    memset(BITMAP, 0, VROWS * 40);
    rows_reset();
    back = 0;
}

uint8_t *render_dl(void) { return DL_A; }
uint8_t *render_hud(void) { return HUD; }
uint8_t *render_msg(void) { return MSG; }

static uint8_t lum(uint8_t c, uint8_t add)
{
    uint8_t l = (c & 15) + add;
    if (l > 14) l = 14;
    return (c & 0xF0) | l;
}

void render_frame(void)
{
    static uint8_t j, gl, t, ndl, hor, nset;
    static int16_t s0, seg, yn;
    static uint16_t z, k;
    static int16_t xr, dir_t;
    static uint8_t *dl, *tb, *set;
    static int16_t plan_i;

    pal = &pals[w.route];
    gl = w.glare >> 1;                            /* the low sun washes it out */

    /* the chase camera, as the original's: behind the car and above the
       higher of the two road heights, its sideways offset lagging the car */
    cam_z = w.car.z > CAM_BACK ? w.car.z - CAM_BACK : 0;
    cam_d += ((w.car.d >> 4) - cam_d) / 4;
    seg = seg_of(cam_z);
    if (seg > w.nseg - 2) seg = w.nseg - 2;
    /* the heading: the road's direction at the camera (x per segment x 16),
       interpolated in the segment, then smoothed */
    dir_t = seg_dir[seg] * 16 + (int16_t)(((seg_dir[seg + 1] - seg_dir[seg]) * (int16_t)(cam_z & 127)) >> 3);
    cam_dir16 += (dir_t - cam_dir16) / 3;
    cam_x = road_cx(cam_z, 0) + cam_d;
    cam_y = road_y(cam_z);
    yn = road_y(w.car.z);
    if (yn > cam_y) cam_y = yn;
    cam_y += CAM_UP;

    /* the boundaries: one at ZNEAR (below the screen, here), then the
       segments ahead (project.s): every one to 8 out, every second to 16,
       every fourth to 32 */
    s0 = seg_of(cam_z);
    z = cam_z + ZNEAR;
    seg = seg_of(z);
    k = ktab8[ZNEAR >> 3];
    mk = (uint8_t)k;
    xr = road_cx(z, 0) - cam_x - ((cam_dir16 * 3) >> 7);   /* dir16 * 48 / 2048 */
    bd_c[0] = 1280 + mulk(xr);
    bd_row[0] = HOR + (mulk(cam_y - road_y(z)) >> 4);
    bd_lw[0] = (int16_t)k * 7;
    bd_g[0] = 0;
    plan_i = seg - w.ai.plan_from;
    bd_p[0] = w.ai.on && plan_i >= 0 && plan_i < 16 ? mulk(w.ai.plan[plan_i] >> 4) : 0;
    bseg[0] = seg;
    pj_s0 = s0;
    pj_nb = 1;
    pj_segmax = w.nseg - 1;
    pj_lfrac = ((cam_dir16 >> 2) * (int16_t)(cam_z & 127)) >> 5;   /* dir16 * f / 128 */
    pj_three = route_info[w.route].nl == 3 ? M_THREE : 0;
    pj_pathok = w.ai.on && (!w.ai.blinded || (w.frame & 2));
    pj_planfrom = w.ai.plan_from;
    pj_plan = w.ai.plan;
    project();
    nb = bd_n;
    /* boundary 0 (below the screen) is drawn as the segment it's in */
    t = bd_mode[1];
    bd_mode[0] = t;
    memcpy(bd_side, bd_side + 4, 4);
    bd_fw[0] = bd_fw[2];
    bd_fw[1] = bd_fw[3];

    /* the bands, near to far: each draws the rows between its boundaries
       not hidden by a nearer one (a hill's crest) */
    bd_n = nb;
    draw_bands();
    hor = bt_hor;
    sky_rows(hor);
    /* the display list and its table: the sky (DLI 0 and two more steps
       above the horizon), the bands from the top down (each band's colours
       from the row above it), the text */
    if (gl != last_gl || pal != last_pal) {        /* (the palette with the glare) */
        last_gl = gl;
        last_pal = pal;
        co_g1 = lum(pal->g1, gl);
        co_g2 = lum(pal->g2, gl);
        co_road = lum(pal->road, gl);
        co_line = lum(pal->line, gl);
        co_water = lum(pal->water, gl);
        co_shadow = lum(0x02, gl);
        for (j = 0; j < 3; ++j) c_sky[j] = lum(pal->sky[j], gl);
    }
    co_path = w.ai.blinded ? 0x0E : co_water;
    tb = back ? dlt_bk + 32 : dlt_bk;
    dl = dl_rows[back];
    set = back ? dl_set[1] : dl_set[0];
    for (j = dl_nset[back]; j-- > 0; ) dl[set[j]] &= 0x7F;
    nset = 0;
    tb[0] = c_sky[0];                                /* DLI 0: the sky's top */
    tb[64] = tb[128] = tb[192] = 0;
    ndl = 1;
    t = hor / 3;                                     /* the sky's two steps */
    for (j = 1; j < 3; ++j, t += t) {
        if (t > 1 && t + 1 < hor) {
            set[nset++] = dloff[t - 1];              /* (row t - 1's instruction) */
            dl[dloff[t - 1]] |= 0x80;
            tb[ndl] = c_sky[j];
            tb[ndl + 64] = tb[ndl + 128] = tb[ndl + 192] = 0;
            ndl++;
        }
    }
    /* the bands, from the top down (colours.s) */
    co_dl = dl;
    co_tb = tb;
    co_set = set;
    co_nset = nset;
    co_ndl = ndl;
    band_colours();
    dl_nset[back] = co_nset;
    dl_base_next = back ? 32 : 0;
    dl_next = back ? DL_B : DL_A;
    back ^= 1;
}
