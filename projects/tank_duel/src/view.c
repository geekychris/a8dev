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

static int16_t ccx, ccz;
static void centre(int16_t wx, int16_t wz) { corner(0, wx, wz); ccx = a3d_vx[0]; ccz = a3d_vz[0]; }

static int16_t ycam_(int16_t y)         /* height -> camera y (the pixels' aspect: 109/128) */
{
    return (int16_t)((y - EYE) * 109) >> 7;   /* (16 bits: |y - EYE| < 300) */
}
#define YMAX 131
static int16_t ytab[YMAX];              /* ycam for 0..130 (view_init) */
#define ycam(y) ((uint16_t)(y) < YMAX ? ytab[y] : ycam_(y))

/* the faces of a prism of n corners (clockwise seen from above: world x
   right, z up), bottom vertices 0..n-1, top n..2n-1. port: a side is
   drawn only if the camera is outside its edge, by more than a little
   (world 2D cross product): a3d's own winding test, on screen and on
   shrunk deltas, can let a face seen edge-on through */
static uint8_t fi[5];

/* a face through vertices fi[0..k-1]: straight to a3d_poly when none is
   behind the near plane (a3d_face's C near-clipping and gather cost ~5,000
   cycles a face), else through a3d_face */
static uint8_t nearclip;

static void face(uint8_t k)
{
    uint8_t i;
    if (nearclip) { a3d_face(k, fi); return; }
    for (i = 0; i < k; ++i) { a3d_px[i] = a3d_sx[fi[i]]; a3d_py[i] = a3d_sy[fi[i]]; }
    a3d_poly(k);
}

/* port: far away (beyond LOD units) an object is one polygon, its outline
   between its leftmost and rightmost vertical edges */
#define LOD 350
static int16_t far_d;
static uint8_t vis;                     /* the sides seen: bit i = side i (corner i to i + 1) */

/* port: which sides of a box face the camera, from the camera's place in
   the box's own frame (lu across, lv along): no products */
static void box_vis(int16_t lu, int16_t lv, int16_t a, int16_t b0, int16_t b1)
{
    vis = 0;
    if (lu < -a) vis |= 1;              /* its left (corner 0 -> 1) */
    if (lv > b1) vis |= 2;              /* its front */
    if (lu > a) vis |= 4;               /* its right */
    if (lv < b0) vis |= 8;              /* its back */
}

static void prism_faces(uint8_t n, const uint8_t *shade, uint8_t top)
{
    uint8_t i, j;
    nearclip = 0;
    for (i = 0; i < n + n; ++i) nearclip |= a3d_vclip[i];
    for (i = 0; i < n; ++i) {
        j = (uint8_t)(i + 1 == n ? 0 : i + 1);
        if (!(vis & (1 << i))) continue;
        fi[0] = j; fi[1] = (uint8_t)(n + j); fi[2] = (uint8_t)(n + i); fi[3] = i;   /* (clockwise on the screen) */
        a3d_shade_set(shade[i]);
        face(4);
    }
    if (top) {
        for (i = 0; i < n; ++i) fi[i] = (uint8_t)(n + i);
        a3d_shade_set(top);
        face(n);
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

/* ---------- a8_3d's convex path (a3d_prism.s) ----------
   a box in one asm call: the centre (ccx, ccz), the turn (pc, ps: Q7, by
   the angle between the camera and the object), the sides seen (vis: mine
   are bit 0 left, 1 front, 2 right, 3 back; a3d's 0 back, 1 right,
   2 front, 3 left, 4 top). Returns 1 when a corner is nearer than the near
   plane: then the C path below draws it (a3d_face clips). */
static int8_t pc, ps;
static const uint8_t rev4[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };

static void pr_set(int16_t hw, int16_t z0, int16_t z1, int16_t y0, int16_t y1, const uint8_t *sh, uint8_t top)
{
    a3d_pr_x = ccx; a3d_pr_z = ccz;
    a3d_pr_c = pc; a3d_pr_s = ps;
    a3d_pr_hw = (uint8_t)hw; a3d_pr_z0 = (int8_t)z0; a3d_pr_z1 = (int8_t)z1;
    a3d_pr_y0 = ycam(y0); a3d_pr_y1 = ycam(y1);
    a3d_pr_shade[0] = sh[3]; a3d_pr_shade[1] = sh[1];
    a3d_pr_shade[2] = sh[0]; a3d_pr_shade[3] = sh[2];
    a3d_pr_shade[4] = top;
    a3d_pr_vis = (uint8_t)(rev4[vis] | (top && y1 < EYE ? 16 : 0));
    a3d_pr_far = 0;
}

/* 0: drawn (or nothing to draw); 1: near-clipped, use the C path */
static uint8_t fast_box(int16_t hw, int16_t z0, int16_t z1, int16_t y0, int16_t y1, const uint8_t *sh, uint8_t top)
{
    pr_set(hw, z0, z1, y0, y1, sh, top);
    if (far_d > LOD) {                  /* (one quad, the front's shade) */
        a3d_pr_far = 1;
        a3d_pr_shade[0] = sh[1];
    } else if (!a3d_pr_vis) return 0;
    return a3d_prism();
}

/* the shade of a side facing world angle a (light from the south-west) */
static uint8_t side_shade(uint16_t a, uint8_t lo)
{
    int16_t s = td_sin8(a + 0x6000);   /* -256..256 */
    return (uint8_t)(lo + (s > 125 ? 2 : s > -125 ? 1 : 0));
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
        uint16_t tt;
        while (den >= 256) { num >>= 1; den >>= 1; }   /* port: 16-bit division (num <= den) */
        tt = den ? (uint16_t)((num << 8) / den) : 0;
        t = tt > 255 ? 255 : (uint8_t)tt;
    }
    {
        int16_t nx = qx[0] + a3d_mulf(qx[1] - qx[0], t), nz = qz[0] + a3d_mulf(qz[1] - qz[0], t);
        if (da < 0) { qx[0] = nx; qz[0] = nz; } else { qx[1] = nx; qz[1] = nz; }
    }
    return 1;
}

static int16_t acx[4], acz[4];          /* the arena's corners in camera space (a frame's) */

static void wall(uint8_t a, uint8_t b, uint8_t shade)
{
    qx[0] = acx[a]; qz[0] = acz[a];
    qx[1] = acx[b]; qz[1] = acz[b];
    if (!clip_side(qz[0] - WNEAR, qz[1] - WNEAR)) return;
    if (!clip_side(qz[0] - qx[0], qz[1] - qx[1])) return;    /* x <= z */
    if (!clip_side(qz[0] + qx[0], qz[1] + qx[1])) return;    /* -x <= z */
    a3d_pr_ax = qx[0]; a3d_pr_az = qz[0] < WFAR ? WFAR : qz[0];
    a3d_pr_bx = qx[1]; a3d_pr_bz = qz[1] < WFAR ? WFAR : qz[1];
    a3d_pr_y0 = ycam(0); a3d_pr_y1 = ycam(WALL_H);
    a3d_pr_shade[0] = shade;
    a3d_wallquad();                 /* (never near-clipped: the ends are at WFAR or beyond) */
}

static void walls(void)
{
    /* the corners NW, NE, SE, SW once; each wall seen from inside, left to right */
    static const int16_t cx[4] = { -1000, 1000, 1000, -1000 }, cz[4] = { 1000, 1000, -1000, -1000 };
    uint8_t i;
    for (i = 0; i < 4; ++i) { corner(0, cx[i], cz[i]); acx[i] = a3d_vx[0]; acz[i] = a3d_vz[0]; }
    wall(0, 1, 2);                              /* north */
    wall(1, 2, 3);                              /* east */
    wall(2, 3, 2);                              /* south */
    wall(3, 0, 3);                              /* west */
}

/* ---------- the objects, far to near ---------- */
#define MAXOBJ (TD_MAX_OBS + TD_MAX_TANKS)
#define VIEW_FAR 1100                   /* port: nothing beyond (the radar shows it) */
#define MAXDRAW 10                      /* port: at most this many objects drawn */
static uint8_t ord[MAXOBJ], nobj;
static int16_t odist[MAXOBJ];

/* the world's x and z axes in camera space (Q8): x = (cc, cs), z = (-cs, cc) */
static int16_t rA[3], rB[3];            /* r * cc, r * cs for the radii 26, 34, 50 (a frame's) */

static void obstacle(const Obstacle *o)
{
    int16_t r = o->r, ox = UNITS(o->x), oz = UNITS(o->z), A, B;
    uint8_t k = r == 26 ? 0 : r == 34 ? 1 : 2;
    static const uint8_t pil[4] = { 2, 4, 3, 1 }, cra[4] = { 3, 5, 4, 2 };
    centre(ox, oz);
    box_vis(cam_x - ox, cam_z - oz, r, -r, r);
    pc = a3d_sin8[(uint8_t)(cam_yaw >> 8) + 64];
    ps = a3d_sin8[(uint8_t)(cam_yaw >> 8)];
    if (o->kind == OB_PILLAR) { if (!fast_box(r, -r, r, 0, 130, pil, 0)) return; }
    else if (o->kind == OB_CRATE) { if (!fast_box(r, -r, r, 0, 56, o->hp >= 2 ? cra : pil, 0)) return; }
    else {
        static const uint8_t psh[4] = { 3, 5, 4, 2 };
        pr_set(r, -r, r, 0, 110, psh, 0);
        if (!a3d_pyramid()) return;
    }
    /* (near-clipped: the C path) */
    A = rA[k]; B = rB[k];                   /* corners: clockwise from above */
    a3d_vx[0] = ccx - A + B; a3d_vz[0] = ccz - B - A;
    a3d_vx[1] = ccx - A - B; a3d_vz[1] = ccz - B + A;
    a3d_vx[2] = ccx + A - B; a3d_vz[2] = ccz + B + A;
    a3d_vx[3] = ccx + A + B; a3d_vz[3] = ccz + B - A;
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
        nearclip = 0;
        for (i = 0; i < 5; ++i) nearclip |= a3d_vclip[i];
        for (i = 0; i < 4; ++i) {
            if (!(vis & (1 << i))) continue;
            fi[0] = (uint8_t)(i == 3 ? 0 : i + 1); fi[1] = 4; fi[2] = i;
            a3d_shade_set(sh[i]);
            face(3);
        }
    }
}

/* a box turned by yaw: half width hw across, from z0 to z1 along; the
   tank's axes in camera space by the angle between it and the camera */
static int16_t tux, tuz, tvx, tvz;

static int16_t tlu, tlv;                /* the camera in the tank's frame */

static void tbox(int16_t hw, int16_t z0, int16_t z1, int16_t y0, int16_t y1, const uint8_t *sh, uint8_t top)
{
    box_vis(tlu, tlv, hw, z0, z1);
    if (!fast_box(hw, z0, z1, y0, y1, sh, top)) return;
    corners4(hw, z0, z1, tux, tuz, tvx, tvz);
    prism(4, y0, y1, sh, top);
}

static void tank(const Tank *t, int16_t d)
{
    uint16_t ry = t->yaw - cam_yaw;
    uint8_t sh[4], lo = t->type == TK_SCOUT ? 4 : t->type == TK_MORTAR ? 2 : 0;
    int16_t s = td_sin8(ry), c = td_cos8(ry);
    pc = a3d_sin8[(uint8_t)((uint16_t)(cam_yaw - t->yaw) >> 8) + 64];
    ps = a3d_sin8[(uint8_t)((uint16_t)(cam_yaw - t->yaw) >> 8)];
    tux = c; tuz = -s;                      /* across: the tank's right */
    tvx = s; tvz = c;                       /* along: its front */
    /* the camera in the tank's frame: across = (cos, -sin), along = (sin, cos) */
    r_dx = cam_x - UNITS(t->x);
    r_dz = cam_z - UNITS(t->z);
    r_cs = td_sin8(t->yaw);
    r_cc = td_cos8(t->yaw);
    rot2();
    tlu = r_cx;
    tlv = r_cz;
    r_cs = cs;
    r_cc = cc;
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
        if (cz < A3D_NEAR - r || cz > VIEW_FAR) continue;   /* port: a draw distance */
        cx = r_cx;
        if (cx > cz + r + r || cx < -cz - r - r) continue;   /* (90 degrees and a margin) */
        /* insert by distance, far first */
        for (j = n; j && odist[j - 1] < cz; --j) { ord[j] = ord[j - 1]; odist[j] = odist[j - 1]; }
        ord[j] = i;
        odist[j] = cz;
        ++n;
    }
    /* port: the nearest MAXDRAW only (the farthest are dropped) */
    k = n > MAXDRAW ? (uint8_t)(n - MAXDRAW) : 0;
    for (; k < n; ++k) {
        i = ord[k];
        far_d = odist[k];
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
{
    const Tank *p = &G.tk[0];
    cam_x = UNITS(p->x);
    cam_z = UNITS(p->z);
    cam_yaw = p->yaw;
    r_cs = cs = td_sin8(p->yaw);
    r_cc = cc = td_cos8(p->yaw);
    rA[0] = a3d_mulf(26, cc); rB[0] = a3d_mulf(26, cs);
    rA[1] = a3d_mulf(34, cc); rB[1] = a3d_mulf(34, cs);
    rA[2] = a3d_mulf(50, cc); rB[2] = a3d_mulf(50, cs);
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
    uint8_t y;
    for (y = 0; y < YMAX; ++y) ytab[y] = ycam_(y);
    a3d_view(80, V_HORIZON, 80);
}


