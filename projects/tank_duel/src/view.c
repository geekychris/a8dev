/*
 * Tank Duel: the picture from the turret (the original's render.c), with
 * the a8_3d renderer (projects/a8_3d).
 *
 * ANTIC D, 160 x V_LINES, double buffered. The sky and the floor are the
 * background colour, changed by a DLI at the horizon (dli.s): nothing is
 * filled for them. Everything standing in the arena is a vertical prism
 * (the walls, pillars, crates, the tanks' hulls and turrets) or a pyramid,
 * so only footprints turn with the camera (2D, a3d_mulf) and a3d projects
 * and fills the faces. Drawn far to near: the walls, then the objects by
 * distance, each object's faces culled by their winding.
 *
 * port: no floor grid, pillars are square, a mortar's ring is on the radar
 * only, a tank far away is its hull only, three colours and their dithers
 * for everything (the original has 32 pens).
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a3d.h"
#include "td.h"
#include "view.h"

#define EYE      34                     /* the eye above the floor (units) */
#define WALL_H   70

static int16_t cam_x, cam_z, cs, cc;
static uint16_t cam_yaw;    /* the camera: units, sine and cosine Q8 */
static uint8_t nv;                      /* vertices used this object */

/* a footprint corner (world units) into camera space (vertex i) */
extern int16_t r_dx, r_dz, r_cx, r_cz, r_cs, r_cc;      /* fast.s */
void rot2(void);

static void corner(uint8_t i, int16_t wx, int16_t wz)
{
    r_dx = wx - cam_x;
    r_dz = wz - cam_z;
    rot2();
    a3d_vx[i] = r_cx;
    a3d_vz[i] = r_cz;
}

/* the same for a corner (ox, oz) off a centre already turned: (ccx, ccz)
   + the offset turned (port: 4 multiplies a corner made objects slow) */
static int16_t ccx, ccz;
static void centre(int16_t wx, int16_t wz) { corner(0, wx, wz); ccx = a3d_vx[0]; ccz = a3d_vz[0]; }
static void offset(uint8_t i, int16_t ox, int16_t oz)
{
    r_dx = ox;
    r_dz = oz;
    rot2();
    a3d_vx[i] = ccx + r_cx;
    a3d_vz[i] = ccz + r_cz;
}

static int16_t ycam(int16_t y)         /* height -> camera y (the pixels' aspect: 109/128) */
{
    return (int16_t)((y - EYE) * 109) >> 7;   /* (16 bits: |y - EYE| < 300) */
}

/* the faces of a prism of n corners (clockwise seen from above: world x
   right, z up), bottom vertices 0..n-1, top n..2n-1. port: a side is
   drawn only if the camera is outside its edge, by more than a little
   (world 2D cross product): a3d's own winding test, on screen and on
   shrunk deltas, can let a face seen edge-on through */
static uint8_t fi[5];

/* the side from corner i to j faces the camera (camera space: the camera
   at the origin; edges are short, so the products don't saturate) */
static uint8_t outside(uint8_t i, uint8_t j)
{
    int16_t ex = a3d_vx[j] - a3d_vx[i], ez = a3d_vz[j] - a3d_vz[i];
    return a3d_mulf(ez, a3d_vx[i]) - a3d_mulf(ex, a3d_vz[i]) > 2;
}

static void prism_faces(uint8_t n, const uint8_t *shade, uint8_t top)
{
    uint8_t i, j;
    for (i = 0; i < n; ++i) {
        j = (uint8_t)(i + 1 == n ? 0 : i + 1);
        if (!outside(i, j)) continue;
        fi[0] = i; fi[1] = (uint8_t)(n + i); fi[2] = (uint8_t)(n + j); fi[3] = j;
        a3d_shade_set(shade[i]);
        a3d_face(4, fi);
    }
    if (top) {
        for (i = 0; i < n; ++i) fi[i] = (uint8_t)(n + n - 1 - i);
        a3d_shade_set(top);
        a3d_face(n, fi);
    }
}

/* a vertical prism: its n corners already in camera space (a3d_vx/vz
   0..n-1), from height y0 to y1 */
static void prism(uint8_t n, int16_t y0, int16_t y1, const uint8_t *shade, uint8_t top)
{
    uint8_t i;
    int16_t b = ycam(y0), t = ycam(y1);
    for (i = 0; i < n; ++i) {
        a3d_vx[n + i] = a3d_vx[i];
        a3d_vz[n + i] = a3d_vz[i];
        a3d_vy[i] = b;
        a3d_vy[n + i] = t;
    }
    a3d_project(0, (uint8_t)(n + n));
    prism_faces(n, shade, y1 < EYE ? top : 0);
}

/* the 4 corners of a footprint around a centre already in camera space
   (ccx, ccz): offsets (-a, b0), (-a, b1), (a, b1), (a, b0) along the axes
   (ux, uz) (across) and (vx, vz) (along), all in camera space, Q8 */
static void corners4(int16_t a, int16_t b0, int16_t b1, int16_t ux, int16_t uz, int16_t vx, int16_t vz)
{
    int16_t ax = a3d_mulf(a, ux), az = a3d_mulf(a, uz);
    int16_t p0x = a3d_mulf(b0, vx), p0z = a3d_mulf(b0, vz), p1x = a3d_mulf(b1, vx), p1z = a3d_mulf(b1, vz);
    a3d_vx[0] = ccx - ax + p0x; a3d_vz[0] = ccz - az + p0z;
    a3d_vx[1] = ccx - ax + p1x; a3d_vz[1] = ccz - az + p1z;
    a3d_vx[2] = ccx + ax + p1x; a3d_vz[2] = ccz + az + p1z;
    a3d_vx[3] = ccx + ax + p0x; a3d_vz[3] = ccz + az + p0z;
}

/* the shade of a side facing world angle a (light from the south-west) */
static uint8_t side_shade(uint16_t a, uint8_t lo)
{
    int16_t s = td_sin(a + 0x6000);    /* -16384..16384 */
    return (uint8_t)(lo + (s > 8000 ? 2 : s > -8000 ? 1 : 0));
}

/* ---------- the walls ----------
   port: each wall is clipped in camera space (2D: the near plane and the
   view's two 45-degree sides) before it is projected, so it reaches a3d as
   a trapezoid on the screen; big polygons clipped at the screen's edges
   cost a3d ~50,000 cycles each */
#define WNEAR 16
#define WFAR  64                        /* a wall's end nearer than this is drawn at it */

/* clip the segment (x0,z0)-(x1,z1) to z >= WNEAR, -z <= x <= z; 0: nothing left */
static int16_t qx[2], qz[2];
/* cut the segment a-b where the signed distance d goes through 0 (da, db
   of a and b): the part inside (d >= 0) stays; 0 if nothing does */
static uint8_t clip_side(int16_t da, int16_t db)
{
    uint8_t t;
    if (da >= 0 && db >= 0) return 1;
    if (da < 0 && db < 0) return 0;
    /* t = da / (da - db) in 1/256 (0..256): the cut, from a */
    {
        uint16_t num = (uint16_t)(da < 0 ? -da : da), den = (uint16_t)(da < 0 ? db - da : da - db);
        uint16_t tt = den ? (uint16_t)(((uint32_t)num << 8) / den) : 0;
        t = tt > 255 ? 255 : (uint8_t)tt;
    }
    {
        int16_t nx = qx[0] + a3d_mulf(qx[1] - qx[0], t), nz = qz[0] + a3d_mulf(qz[1] - qz[0], t);
        if (da < 0) { qx[0] = nx; qz[0] = nz; } else { qx[1] = nx; qz[1] = nz; }
    }
    return 1;
}

static void wall(int16_t x0, int16_t z0, int16_t x1, int16_t z1, uint8_t shade)
{
    uint8_t i;
    static const uint8_t f[4] = { 0, 2, 3, 1 };
    corner(0, x0, z0);
    corner(1, x1, z1);
    qx[0] = a3d_vx[0]; qz[0] = a3d_vz[0];
    qx[1] = a3d_vx[1]; qz[1] = a3d_vz[1];
    if (!clip_side(qz[0] - WNEAR, qz[1] - WNEAR)) return;
    if (!clip_side(qz[0] - qx[0], qz[1] - qx[1])) return;    /* x <= z */
    if (!clip_side(qz[0] + qx[0], qz[1] + qx[1])) return;    /* -x <= z */
    for (i = 0; i < 2; ++i) {                                  /* 0, 1 the foot; 2, 3 the top */
        a3d_vx[i] = a3d_vx[i + 2] = qx[i];
        a3d_vz[i] = a3d_vz[i + 2] = qz[i] < WFAR ? WFAR : qz[i];   /* (on the screen: a3d's y clipping of long edges is slow) */
        a3d_vy[i] = ycam(0);
        a3d_vy[i + 2] = ycam(WALL_H);
    }
    a3d_project(0, 4);
    if (a3d_sx[0] >= a3d_sx[1]) return;         /* (seen from behind) */
    for (i = 0; i < 4; ++i) { a3d_px[i] = a3d_sx[f[i]]; a3d_py[i] = a3d_sy[f[i]]; }
    a3d_shade_set(shade);
    a3d_poly(4);
}

static void walls(void)
{
    /* seen from inside: each from left to right */
    wall(-1000, 1000, 1000, 1000, 2);           /* north */
    wall(1000, 1000, 1000, -1000, 3);           /* east */
    wall(1000, -1000, -1000, -1000, 2);         /* south */
    wall(-1000, -1000, -1000, 1000, 3);         /* west */
}

/* ---------- the objects, far to near ---------- */
#define MAXOBJ (TD_MAX_OBS + TD_MAX_TANKS)
static uint8_t ord[MAXOBJ], nobj;
static int16_t odist[MAXOBJ];

/* the world's x and z axes in camera space (Q8): x = (cc, cs), z = (-cs, cc) */
static void obstacle(const Obstacle *o)
{
    int16_t r = o->r;
    static const uint8_t pil[4] = { 2, 4, 3, 1 }, cra[4] = { 3, 5, 4, 2 };
    centre(UNITS(o->x), UNITS(o->z));
    corners4(r, -r, r, cc, cs, -cs, cc);    /* clockwise from above */
    if (o->kind == OB_PILLAR) prism(4, 0, 130, pil, 0);
    else if (o->kind == OB_CRATE) prism(4, 0, 56, o->hp >= 2 ? cra : pil, 0);
    else {
        /* a pyramid: the base, the apex */
        uint8_t i;
        static const uint8_t sh[4] = { 3, 5, 4, 2 };
        int16_t b = ycam(0);
        for (i = 0; i < 4; ++i) a3d_vy[i] = b;
        a3d_vx[4] = ccx; a3d_vz[4] = ccz; a3d_vy[4] = ycam(110);
        a3d_project(0, 5);
        for (i = 0; i < 4; ++i) {
            if (!outside(i, (uint8_t)(i == 3 ? 0 : i + 1))) continue;
            fi[0] = i; fi[1] = 4; fi[2] = (uint8_t)(i == 3 ? 0 : i + 1);
            a3d_shade_set(sh[i]);
            a3d_face(3, fi);
        }
    }
}

/* a box turned by yaw: half width hw across, from z0 to z1 along; the
   tank's axes in camera space by the angle between it and the camera */
static int16_t tux, tuz, tvx, tvz;

static void tbox(int16_t hw, int16_t z0, int16_t z1, int16_t y0, int16_t y1, const uint8_t *sh, uint8_t top)
{
    corners4(hw, z0, z1, tux, tuz, tvx, tvz);
    prism(4, y0, y1, sh, top);
}

static void tank(const Tank *t, int16_t d)
{
    uint16_t ry = t->yaw - cam_yaw;
    uint8_t sh[4], lo = t->type == TK_SCOUT ? 4 : t->type == TK_MORTAR ? 2 : 0;
    int16_t s = td_sin(ry) >> 6, c = td_cos(ry) >> 6;
    tux = c; tuz = -s;                      /* across: the tank's right */
    tvx = s; tvz = c;                       /* along: its front */
    centre(UNITS(t->x), UNITS(t->z));
    sh[0] = side_shade(t->yaw + 0xC000, lo);    /* its left */
    sh[1] = side_shade(t->yaw, lo);             /* its front */
    sh[2] = side_shade(t->yaw + 0x4000, lo);    /* its right */
    sh[3] = side_shade(t->yaw + 0x8000, lo);    /* its back */
    switch (t->type) {
    case TK_SCOUT:          /* light: a low hull, a little turret */
        tbox(20, -26, 24, 0, 16, sh, 6);
        if (d < 450) tbox(9, -10, 8, 16, 26, sh, 6);
        break;
    case TK_MORTAR:         /* a box with a tube up */
        tbox(22, -26, 26, 0, 22, sh, 4);
        if (d < 450) tbox(6, -8, 4, 22, 58, sh, 0);
        break;
    case TK_BULWARK:        /* dark, heavy; the shield across its front */
        tbox(26, -28, 22, 0, 26, sh, 2);
        if (d < 450) tbox(13, -14, 10, 26, 38, sh, 2);
        sh[0] = sh[1] = sh[2] = sh[3] = 6;
        tbox(32, 26, 32, 2, 40, sh, 6);
        break;
    }
}

static void objects(void)
{
    uint8_t i, j, k, n = 0;
    int16_t dx, dz, cz, cx, r;
    /* in front of the camera, roughly in view: their distance along it */
    for (i = 0; i < G.nobs + G.ntanks; ++i) {
        if (i < G.nobs) {
            const Obstacle *o = &G.ob[i];
            if (!o->hp) continue;
            dx = UNITS(o->x) - cam_x; dz = UNITS(o->z) - cam_z; r = o->r + 10;
        } else {
            const Tank *t = &G.tk[i - G.nobs];
            if (i == G.nobs || !t->alive) continue;          /* (the player) */
            dx = UNITS(t->x) - cam_x; dz = UNITS(t->z) - cam_z; r = 45;
        }
        r_dx = dx;
        r_dz = dz;
        rot2();
        cz = r_cz;
        if (cz < A3D_NEAR - r) continue;
        cx = r_cx;
        if (cx > cz + r + r || cx < -cz - r - r) continue;   /* (90 degrees and a margin) */
        /* insert by distance, far first */
        for (j = n; j && odist[j - 1] < cz; --j) { ord[j] = ord[j - 1]; odist[j] = odist[j - 1]; }
        ord[j] = i;
        odist[j] = cz;
        ++n;
    }
    for (k = 0; k < n; ++k) {
        i = ord[k];
        if (i < G.nobs) obstacle(&G.ob[i]);
        else tank(&G.tk[i - G.nobs], odist[k]);
    }
    nobj = n;
}

/* ---------- shells and mortar rounds: little squares ---------- */
static void dot(int16_t wx, int16_t y, int16_t wz, uint8_t size, uint8_t col)
{
    int16_t sx, sy;
    uint8_t yy, x0, x1;
    corner(0, wx, wz);
    if (a3d_vz[0] < A3D_NEAR) return;
    a3d_vy[0] = ycam(y);
    a3d_project(0, 1);
    sx = a3d_sx[0];
    sy = a3d_sy[0];
    if (sx < 1 || sx >= 159 || sy < 1 || sy >= (int16_t)a3d_h - (int16_t)size) return;
    x0 = (uint8_t)(sx - 1);
    x1 = (uint8_t)(sx + size);
    if (x1 > 160) x1 = 160;
    a3d_colour(col);
    for (yy = 0; yy < size; ++yy) a3d_span((uint8_t)(sy + yy), x0, x1);
}

static void missiles(void)
{
    uint8_t k;
    const Shell *s = G.sh;
    const Mortar *m = G.mo;
    for (k = 0; k < TD_MAX_SHELLS; ++k, ++s)
        if (s->alive) dot(UNITS(s->x), 20, UNITS(s->z), 2, 3);
    for (k = 0; k < TD_MAX_MORTARS; ++k, ++m) {
        int16_t x, z, y, f;
        if (!m->alive) continue;
        f = (int16_t)(((uint16_t)m->t << 8) / m->flight);       /* 0..256 of the way */
        x = UNITS(m->x0) + (int16_t)((int32_t)UNITS(m->tx - m->x0) * f >> 8);
        z = UNITS(m->z0) + (int16_t)((int32_t)UNITS(m->tz - m->z0) * f >> 8);
        y = 20 + (int16_t)((int32_t)4 * 280 * f * (256 - f) >> 16);
        dot(x, y, z, 3, 3);
    }
}

/* ---------- explosions: a flash for a few frames ---------- */
#define MAXBOOM 4
static int16_t bx[MAXBOOM], bz[MAXBOOM];
static uint8_t bt[MAXBOOM];

void view_boom(pos x, pos z)
{
    uint8_t k;
    for (k = 0; k < MAXBOOM; ++k)
        if (!bt[k]) { bx[k] = UNITS(x); bz[k] = UNITS(z); bt[k] = 6; return; }
}

static void booms(void)
{
    uint8_t k;
    for (k = 0; k < MAXBOOM; ++k)
        if (bt[k]) {
            dot(bx[k], 20, bz[k], (uint8_t)(bt[k] + 2), (uint8_t)(bt[k] & 1 ? 3 : 2));
            --bt[k];
        }
}

/* ---------- the frame ---------- */
uint16_t vt_clear, vt_walls, vt_obj, vt_miss, vt_nobj;
void view_draw(void)
#ifdef NODRAW
{ }
void view_draw_unused(void)
#endif
{
    const Tank *p = &G.tk[0];
    cam_x = UNITS(p->x);
    cam_z = UNITS(p->z);
    cam_yaw = p->yaw;
    r_cs = cs = td_sin(p->yaw) >> 6;
    r_cc = cc = td_cos(p->yaw) >> 6;
    {
        uint16_t t = a3d_ticks();
        a3d_clear_dirty(A3D_C0);
        vt_clear += a3d_since(t); t = a3d_ticks();
        walls();
        vt_walls += a3d_since(t); t = a3d_ticks();
        objects();
        vt_obj += a3d_since(t); t = a3d_ticks();
        missiles();
        booms();
        vt_miss += a3d_since(t);
        vt_nobj += nobj;
    }
}

void view_init(void)
{
    a3d_view(80, V_HORIZON, 80);
}

#ifdef BENCH
#include "a8log.h"
void bench_view(void)
{
    uint16_t t;
    uint8_t i;
    const Tank *p = &G.tk[0];
    cam_x = UNITS(p->x); cam_z = UNITS(p->z);
    r_cs = cs = td_sin(p->yaw) >> 6; r_cc = cc = td_cos(p->yaw) >> 6;
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) walls();
    a8_log_s("bench: walls x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    {
        static const char *nm[4] = {"wall N ", "wall E ", "wall S ", "wall W "};
        static const int16_t w[4][4] = {{-1000,1000,1000,1000},{1000,1000,1000,-1000},{1000,-1000,-1000,-1000},{-1000,-1000,-1000,1000}};
        uint8_t k;
        for (k = 0; k < 4; ++k) {
            char b[12];
            t = a3d_ticks();
            wall(w[k][0], w[k][1], w[k][2], w[k][3], 2);
            a8_log_s(nm[k]); ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b);
            a8_log_s(" px="); itoa(a3d_px[0], b, 10); a8_log_s(b); a8_log_s(","); itoa(a3d_py[0], b, 10); a8_log_s(b);
            a8_log_s(" "); itoa(a3d_px[1], b, 10); a8_log_s(b); a8_log_s(","); itoa(a3d_py[1], b, 10); a8_log_s(b);
            a8_log_s(" "); itoa(a3d_px[2], b, 10); a8_log_s(b); a8_log_s(","); itoa(a3d_py[2], b, 10); a8_log_s(b);
            a8_log_s(" "); itoa(a3d_px[3], b, 10); a8_log_s(b); a8_log_s(","); itoa(a3d_py[3], b, 10); a8_log_s(b);
            a8_log_c('\n');
        }
    }
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) {
        a3d_px[0] = 36; a3d_py[0] = 33; a3d_px[1] = 124; a3d_py[1] = 33;
        a3d_px[2] = 124; a3d_py[2] = 35; a3d_px[3] = 36; a3d_py[3] = 35;
        a3d_poly(4);
    }
    a8_log_s("bench: poly 88x2 x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) {
        a3d_px[0] = 36; a3d_py[0] = 33; a3d_px[1] = 124; a3d_py[1] = 33;
        a3d_px[2] = 124; a3d_py[2] = 35; a3d_px[3] = 36; a3d_py[3] = 35;
        a3d_fill(4);
    }
    a8_log_s("bench: fill 88x2 x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) { qx[0] = -500; qz[0] = 300; qx[1] = 700; qz[1] = 900; clip_side(qz[0] - qx[0], qz[1] - qx[1]); }
    a8_log_s("bench: clip x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) corner(0, 100, 300);
    a8_log_s("bench: corner x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) a3d_project(0, 8);
    a8_log_s("bench: project8 x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
    t = a3d_ticks();
    for (i = 0; i < 10; ++i) objects();
    a8_log_s("bench: objects x10 cycles="); { char b[12]; ultoa((unsigned long)a3d_since(t) * 228, b, 10); a8_log_s(b); } a8_log_c('\n');
}
#endif
