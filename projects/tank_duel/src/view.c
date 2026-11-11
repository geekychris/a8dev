/*
 * view.c - Tank Duel from the turret: the sky and the floor (background
 * colours set line by line by DLIs: nothing to draw), the arena's walls,
 * the cover and the tanks (the original's models, tools/gen_models.py),
 * shells, mortar rounds on their arcs with the rings where they will land,
 * explosions, and the radar; the gun and the sight are players.
 *
 * The camera only turns (yaw) and sits at a fixed height, so a model's
 * matrix is a turn by the camera's yaw less the model's, made from two
 * sine lookups (port: the original builds a camera with pitch). Things are
 * drawn far to near, sorted by their distance ahead; a3d transforms,
 * projects, culls, clips at the near plane and fills.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a3d.h"
#include "td.h"
#include "view.h"
#include "models.h"

#pragma bss-name ("LOWRAM")

#define LINES 88
#define HZ    38                        /* the horizon's line */
#define FOCAL 110
#define EYE   34                        /* the camera's height */
#define ASPECT 109                      /* mode D's taller pixels (Q7) */
#define NEARZ 16

extern void td_dli(void);
extern uint8_t dli_bk[], dli_n;

static int8_t cc, cs;                   /* the camera's yaw: cosine, sine (Q7) */
static uint8_t cyaw;
extern uint8_t pl_x, pl_y;
extern void __fastcall__ plot_(uint8_t c);
#define plot(x, y, c) (pl_x = (x), pl_y = (y), plot_(c))
#define MULQ7(v, q) a3d_mulf((v), (int16_t)(q) * 2)
static int16_t cx, cz;                  /* the camera, units */
extern int16_t tc_x, tc_z, tc_cx, tc_cz, tc_rel[3];
extern int8_t tc_c, tc_s;
extern void cam_xz(void);
extern void radar_clear(void);
#define rel tc_rel

/* blasts: explosions, a few frames each */
#define NBLASTS 4
static int16_t b_x[NBLASTS], b_z[NBLASTS];
static uint8_t b_t[NBLASTS], b_size[NBLASTS], b_next;

/* what is drawn this frame, far to near */
#define NDRAW 40
static uint8_t d_kind[NDRAW], d_idx[NDRAW], d_n, d_idx_k;
static int16_t d_z[NDRAW];
enum { D_OBS, D_TANK, D_SHELL, D_MORTAR, D_BLAST };

/* ---------------------------------------------------------------- set-up */
static const uint8_t gun_l[34] = {
    0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F };
static const uint8_t sight[12] = { 0x10, 0x10, 0x10, 0, 0, 0xC6, 0, 0, 0x10, 0x10, 0x10, 0 };
static uint8_t gun_y;
#define GUN_Y 174

void view_init(void)
{
    uint8_t k, *m0, *m1, v, r;
    a3d_init(LINES, 2);
    COLOR0 = 0x08;                      /* grey: walls, pillars, the bulwark */
    COLOR1 = 0x26;                      /* orange-brown: crates, pyramids, the mortar */
    COLOR2 = 0xB8;                      /* green: the scout, shields */
    COLOR4 = 0x96;
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = 0x02;
    a3d_view(80, HZ, FOCAL);
    /* DLIs: the sky's bands, the horizon, the floor's bands, the text rows */
    a3d_dl_flag(9, 0x80); a3d_dl_flag(19, 0x80); a3d_dl_flag(29, 0x80); a3d_dl_flag(HZ - 1, 0x80);
    a3d_dl_flag(44, 0x80); a3d_dl_flag(54, 0x80); a3d_dl_flag(68, 0x80);
    dli_n = 0;
    VDSLST = (uint16_t)td_dli;
    pmg_init();
    pmg_priority(1);
    /* the gun: players 0 and 1 (double width), mirrored halves */
    m0 = pmg_player_mem(0); m1 = pmg_player_mem(1);
    for (k = 0; k < sizeof(gun_l); ++k) {
        v = gun_l[k];
        m0[GUN_Y + k] = v;
        r = (uint8_t)(((v & 1) << 7) | ((v & 2) << 5) | ((v & 4) << 3) | ((v & 8) << 1) | ((v & 16) >> 1) |
                      ((v & 32) >> 3) | ((v & 64) >> 5) | ((v & 128) >> 7));
        m1[GUN_Y + k] = r;
    }
    gun_y = GUN_Y;
    pmg_size(0, 1); pmg_size(1, 1);
    pmg_x(0, 48 + 80 - 16);
    pmg_x(1, 48 + 80);
    pmg_color(0, 0x06); pmg_color(1, 0x06);
    /* the sight: player 2 */
    memcpy(pmg_player_mem(2) + 32 + 2 * HZ - 6, sight, sizeof(sight));
    pmg_x(2, 48 + 80 - 4);
    pmg_color(2, 0xBA);
}

/* the gun kicks back when it fires */
void view_gun(uint8_t kick)
{
    uint8_t *m0 = pmg_player_mem(0), *m1 = pmg_player_mem(1), y = GUN_Y + (kick ? 4 : 0);
    if (y == gun_y) return;
    if (y > gun_y) {                    /* down: shift the shapes */
        memmove(m0 + y, m0 + gun_y, sizeof(gun_l));
        memmove(m1 + y, m1 + gun_y, sizeof(gun_l));
        memset(m0 + gun_y, 0, y - gun_y);
        memset(m1 + gun_y, 0, y - gun_y);
    } else {
        memmove(m0 + y, m0 + gun_y, sizeof(gun_l));
        memmove(m1 + y, m1 + gun_y, sizeof(gun_l));
        memset(m0 + y + sizeof(gun_l), 0, gun_y - y);
        memset(m1 + y + sizeof(gun_l), 0, gun_y - y);
    }
    gun_y = y;
}

void view_blast(int16_t x, int16_t z, uint8_t size)
{
    uint8_t k = b_next;
    b_next = (b_next + 1) & (NBLASTS - 1);
    b_x[k] = x >> 4; b_z[k] = z >> 4; b_t[k] = 10; b_size[k] = size;
}

/* ---------------------------------------------------------------- drawing */
/* world (units) -> camera space in rel (y scaled for the pixels) */
static void to_cam(int16_t x, int16_t y, int16_t z)
{
    tc_x = x; tc_z = z;
    cam_xz();                           /* (fast.s) */
    rel[1] = (int16_t)((y - EYE) * ASPECT) >> 7;
}

static void model(uint8_t m, uint16_t yaw)
{
    const uint8_t *f = model_f[m];
    uint8_t n, a = cyaw - (uint8_t)(yaw >> 8);
    int8_t c = a3d_cos(a), s = a3d_sin(a);
    a3d_om.m[0] = c; a3d_om.m[1] = 0; a3d_om.m[2] = -s;
    a3d_om.m[3] = 0; a3d_om.m[4] = ASPECT; a3d_om.m[5] = 0;
    a3d_om.m[6] = s; a3d_om.m[7] = 0; a3d_om.m[8] = c;
    a3d_ox = rel[0]; a3d_oy = rel[1]; a3d_oz = rel[2];
    a3d_oshift = 1;
    a3d_xform(0, model_nv[m], model_v[m]);
    while ((n = *f) != 0) {
        a3d_pattern(f[1], f[2]);
        a3d_face(n, f + 3);
        f += 3 + n;
    }
}

/* the arena's walls (70 high): each side clipped at the near plane on the
   floor, then a quad with upright edges (port: not a3d's faces) */
static void walls(void)
{
    static const int16_t wx[5] = { -TD_ARENA, TD_ARENA, TD_ARENA, -TD_ARENA, -TD_ARENA };
    static const int16_t wz[5] = { TD_ARENA, TD_ARENA, -TD_ARENA, -TD_ARENA, TD_ARENA };
    static int16_t X[5], Z[5];
    int16_t ax, az, bx, bz, kq, ky, t;
    uint8_t k;
    for (k = 0; k < 4; ++k) {
        tc_x = wx[k]; tc_z = wz[k];
        cam_xz();
        X[k] = rel[0]; Z[k] = rel[2];
    }
    X[4] = X[0]; Z[4] = Z[0];
    for (k = 0; k < 4; ++k) {
        ax = X[k]; az = Z[k]; bx = X[k + 1]; bz = Z[k + 1];
        if (az < NEARZ && bz < NEARZ) continue;
        if (az < NEARZ) { ax += a3d_muldiv(bx - ax, NEARZ - az, bz - az); az = NEARZ; }
        else if (bz < NEARZ) { bx += a3d_muldiv(ax - bx, NEARZ - bz, az - bz); bz = NEARZ; }
        /* the far end first, then clockwise: top a, top b, foot b, foot a with a left of b */
        kq = (int16_t)((uint16_t)(FOCAL * 256u) / (uint16_t)az);
        ky = a3d_mulf(kq, 219);
        t = a3d_mulf(ax, kq);
        a3d_px[0] = 80 + t; a3d_py[0] = HZ - a3d_mulf(ky, 36);
        a3d_px[3] = a3d_px[0]; a3d_py[3] = HZ + a3d_mulf(ky, EYE);
        kq = (int16_t)((uint16_t)(FOCAL * 256u) / (uint16_t)bz);
        ky = a3d_mulf(kq, 219);
        t = a3d_mulf(bx, kq);
        a3d_px[1] = 80 + t; a3d_py[1] = HZ - a3d_mulf(ky, 36);
        a3d_px[2] = a3d_px[1]; a3d_py[2] = HZ + a3d_mulf(ky, EYE);
        if (a3d_px[0] > a3d_px[1]) continue;            /* (seen from outside: never, in the arena) */
        a3d_pattern(0x55, k & 1 ? 0x55 : 0x44);
        a3d_poly(4);
    }
}

/* a point at world (x, y, z) on the screen: 0 if behind */
static uint8_t point(int16_t x, int16_t y, int16_t z)
{
    to_cam(x, y, z);
    if (rel[2] < NEARZ) return 0;
    a3d_vx[0] = rel[0]; a3d_vy[0] = rel[1]; a3d_vz[0] = rel[2];
    a3d_project(0, 1);
    return 1;
}

/* a square r units round a point, facing the camera */
static void spot(int16_t x, int16_t y, int16_t z, uint8_t r, uint8_t pat)
{
    int16_t h, sx, sy;
    uint8_t w, ln;
    if (!point(x, y, z)) return;
    h = (int16_t)((uint16_t)r * FOCAL / (uint16_t)rel[2]);
    if (h > 12) h = 12;
    w = (uint8_t)h;
    sx = a3d_sx[0]; sy = a3d_sy[0];
    if (sx - w < 0 || sx + w >= 160) return;
    a3d_pattern(pat, pat);
    for (ln = 0; ln <= w; ++ln)
        if (sy - (w >> 1) + ln >= 0 && sy - (w >> 1) + ln < LINES) a3d_span((uint8_t)(sy - (w >> 1) + ln), sx - w, sx + w + 1);
}

/* the ring where a mortar round will land: 12 corners on the floor */
static void ring(int16_t x, int16_t z)
{
    uint8_t k, a;
    for (k = 0; k < 12; ++k) {
        a = k * 21;
        to_cam(x + (int16_t)((TD_SPLASH_R * a3d_sin(a)) >> 7), 1, z + (int16_t)((TD_SPLASH_R * a3d_cos(a)) >> 7));
        a3d_vx[k] = rel[0]; a3d_vy[k] = rel[1]; a3d_vz[k] = rel[2];
    }
    a3d_project(0, 12);
    for (k = 0; k < 12; ++k)            /* port: dots, not lines */
        if (!a3d_vclip[k] && (uint16_t)a3d_sx[k] < 160 && (uint16_t)a3d_sy[k] < LINES) plot((uint8_t)a3d_sx[k], (uint8_t)a3d_sy[k], 2);
}

static void blast(uint8_t k)
{
    int16_t sx, sy, r, v;
    uint8_t j, a, t = b_t[k];
    if (!point(b_x[k], 16 + (10 - t) * 2, b_z[k])) return;
    sx = a3d_sx[0]; sy = a3d_sy[0];
    r = (int16_t)((uint16_t)b_size[k] * (4 + t) / 14 * FOCAL / (uint16_t)rel[2]);
    if (r < 2) r = 2;
    if (r > 40) r = 40;
    for (j = 0; j < 8; ++j) {           /* a ragged star, turning as it fades */
        a = j * 32 + t * 6;
        v = (j & 1) ? r >> 1 : r;
        a3d_px[j] = sx + ((v * a3d_sin(a)) >> 7);
        v = (v * a3d_cos(a)) >> 7;
        a3d_py[j] = sy - (v - (v >> 3));                /* (7/8: the pixels' aspect) */
    }
    a3d_pattern(t > 6 ? 0xAA : t > 3 ? 0xA6 : 0x44, t > 6 ? 0xAA : t > 3 ? 0x6A : 0x11);   /* orange, orange and grey, grey dots */
    a3d_poly(8);
}

/* add something to draw at camera depth z (culled outside the view) */
static void add(uint8_t kind, uint8_t idx, int16_t x, int16_t z, uint8_t r)
{
    int16_t X, Z, ax;
    uint8_t i;
    if (d_n >= NDRAW) return;
    to_cam(x, 0, z);
    X = rel[0]; Z = rel[2];
    if (Z < -(int16_t)r) return;                        /* behind */
    ax = X < 0 ? -X : X;
    if (ax - r > Z - (Z >> 2)) return;                  /* outside the view (36 degrees each side) */
    /* insert, far first */
    for (i = d_n; i && d_z[i - 1] < Z; --i) { d_kind[i] = d_kind[i - 1]; d_idx[i] = d_idx[i - 1]; d_z[i] = d_z[i - 1]; }
    d_kind[i] = kind; d_idx[i] = idx; d_z[i] = Z;
    ++d_n;
}

/* the radar (top right): the enemies and where mortar rounds will land,
   turned so ahead is up, 64 units a pixel (port: the obstacles aren't
   shown) */
static const uint8_t rim_x[16] = { 144, 149, 154, 157, 158, 157, 154, 149, 144, 139, 134, 131, 130, 131, 134, 139 };
static const uint8_t rim_y[16] = { 0, 1, 4, 7, 12, 17, 20, 23, 24, 23, 20, 17, 12, 7, 4, 1 };

static void blip(int16_t x, int16_t z, uint8_t c)
{
    int16_t dx = ((x >> 4) - cx) >> 4, dz = ((z >> 4) - cz) >> 4, rx, rz;   /* 16 units */
    if (dx > 120 || dx < -120 || dz > 120 || dz < -120) return;
    rx = (dx * cc - dz * cs) >> 9;      /* (Q7 and 16 units -> 64 units a pixel) */
    rz = (dx * cs + dz * cc) >> 9;
    if (rx < -12 || rx > 12 || rz < -10 || rz > 10) return;
    plot((uint8_t)(144 + rx), (uint8_t)(12 - rz), c);
}

static void radar(void)
{
    uint8_t k;
    for (k = 0; k < 16; ++k) plot(rim_x[k], rim_y[k], 1);
    for (k = 1; k < G.ntanks; ++k)
        if (G.t_alive[k]) blip(G.t_x[k], G.t_z[k], G.t_type[k] == TK_SCOUT ? 3 : 2);
    for (k = 0; k < TD_MAX_MORTARS; ++k)
        if (G.m_alive[k]) blip(G.m_tx[k], G.m_tz[k], 2);
    plot(144, 12, 3);
}

/* port: far cover as a flat shape facing the camera (rel: its centre on
   the floor, camera space), not a model: a pillar or crate a rectangle, a
   pyramid a triangle */
static void billboard(uint8_t kind)
{
    int16_t kq = (int16_t)((uint16_t)(FOCAL * 256u) / (uint16_t)rel[2]);   /* pixels a unit * 256 */
    int16_t ky = a3d_mulf(kq, 219), sx, hw, top, bot;
    uint8_t w, h;
    sx = 80 + a3d_mulf(rel[0], kq);
    w = kind == OB_PILLAR ? 26 : kind == OB_PYRAMID ? 50 : 38;
    h = kind == OB_PILLAR ? 130 : kind == OB_PYRAMID ? 110 : 56;
    hw = a3d_mulf(kq, w);
    if (!hw) hw = 1;
    top = HZ - a3d_mulf(ky, h - EYE);
    bot = HZ + a3d_mulf(ky, EYE);
    if (kind == OB_PYRAMID) {
        a3d_px[0] = sx; a3d_py[0] = top;
        a3d_px[1] = sx + hw; a3d_py[1] = bot;
        a3d_px[2] = sx - hw; a3d_py[2] = bot;
        a3d_pattern(0xAA, 0xAA);
        a3d_poly(3);
        return;
    }
    a3d_px[0] = sx - hw; a3d_py[0] = top;
    a3d_px[1] = sx + hw; a3d_py[1] = top;
    a3d_px[2] = sx + hw; a3d_py[2] = bot;
    a3d_px[3] = sx - hw; a3d_py[3] = bot;
    if (kind == OB_PILLAR) a3d_pattern(0x55, 0x55);
    else if (o_hp[d_idx_k] >= 2) a3d_pattern(0xAA, 0xAA);
    else a3d_pattern(0xA8, 0x8A);
    a3d_poly(4);
}

void view_draw(void)
{
    uint8_t k, i, m;
    int16_t f, dx, dz, h;
    cx = G.t_x[0] >> 4; cz = G.t_z[0] >> 4;
    cyaw = (uint8_t)(G.t_yaw[0] >> 8);
    tc_c = cc = a3d_cos(cyaw); tc_s = cs = a3d_sin(cyaw);
    tc_cx = cx; tc_cz = cz;
    a3d_clear_dirty(0);
    radar_clear();                      /* (the dots: no dirty rectangle) */
    walls();
    d_n = 0;
    for (k = 0; k < nobs; ++k)
        if (o_kind[k] != OB_CRATE || o_hp[k]) add(D_OBS, k, o_x[k] >> 4, o_z[k] >> 4, 70);
    for (k = 1; k < G.ntanks; ++k)
        if (G.t_alive[k]) add(D_TANK, k, G.t_x[k] >> 4, G.t_z[k] >> 4, 70);
    for (k = 0; k < TD_MAX_SHELLS; ++k)
        if (G.s_alive[k]) add(D_SHELL, k, G.s_x[k] >> 4, G.s_z[k] >> 4, 8);
    for (k = 0; k < TD_MAX_MORTARS; ++k)
        if (G.m_alive[k]) add(D_MORTAR, k, G.m_tx[k] >> 4, G.m_tz[k] >> 4, 100);
    for (k = 0; k < NBLASTS; ++k)
        if (b_t[k]) add(D_BLAST, k, b_x[k], b_z[k], 40);
    for (i = 0; i < d_n; ++i) {
        k = d_idx[i];
        switch (d_kind[i]) {
        case D_OBS:
            to_cam(o_x[k] >> 4, 0, o_z[k] >> 4);
            if (rel[2] < NEARZ) break;
            d_idx_k = k; billboard(o_kind[k]);   /* port: cover as flat shapes, near or far */
            break;
            m = o_kind[k] == OB_PILLAR ? M_PILLAR : o_kind[k] == OB_PYRAMID ? M_PYRAMID : o_hp[k] >= 2 ? M_CRATE : M_CRATE_CRACKED;
            model(m, 0);
            break;
        case D_TANK:
            to_cam(G.t_x[k] >> 4, 0, G.t_z[k] >> 4);
            m = G.t_type[k];
            if (d_z[i] > 450) m = m == TK_SCOUT ? M_SCOUT_FAR : m == TK_MORTAR ? M_MORTAR_FAR : M_BULWARK_FAR;   /* port: far, the hull only */
            else if (m == TK_SCOUT) m = M_SCOUT;
            else if (m == TK_MORTAR) m = M_MORTAR;
            else m = (uint8_t)(cyaw - (uint8_t)(G.t_yaw[k] >> 8) + 64) < 128 ? M_BULWARK_REAR : M_BULWARK;
            model(m, G.t_yaw[k]);
            break;
        case D_SHELL:
            spot(G.s_x[k] >> 4, 20, G.s_z[k] >> 4, 5, G.s_owner[k] ? 0xAA : 0xFF);
            break;
        case D_MORTAR:
            ring(G.m_tx[k] >> 4, G.m_tz[k] >> 4);
            f = (int16_t)G.m_t[k] * 256 / TD_MORTAR_FLIGHT;     /* 0..256 of the way */
            dx = (G.m_tx[k] - G.m_x0[k]) >> 4; dz = (G.m_tz[k] - G.m_z0[k]) >> 4;
            h = ((uint16_t)(f * (256 - f)) >> 6) * 70 >> 6;     /* 280 high at the top */
            spot((G.m_x0[k] >> 4) + a3d_mulf(dx, f), 20 + h, (G.m_z0[k] >> 4) + a3d_mulf(dz, f), 7, 0xAA);
            break;
        case D_BLAST:
            blast(k);
            break;
        }
    }
    for (k = 0; k < NBLASTS; ++k) if (b_t[k]) --b_t[k];
    radar();
}
