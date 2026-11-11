/*
 * Spectral Keep: the screen (the original's scene.c, cut down for ANTIC).
 *
 * The room: ANTIC E, 160 x 208 (a pixel is two of the original's across,
 * a line one of its lines: the same projection, x / 2), four colours a
 * band of 8 lines (a DLI each: tools/gen_gfx.py chooses each band's three
 * from the room's own picture; the background is black). It is drawn as
 * the room is entered, from its pieces in bank 1 in the original's draw
 * order (draw.s), and copied to bank 3. The lifts, crates and pickups are
 * drawn over it (bank 2's bitmap sprites); when one moves, the boxes they
 * covered are restored from bank 3 and they are drawn again.
 *
 * The characters are players: the explorer 0 + 1 (three colours), the
 * room's other two (at most two in any room) 2 and 3; sprites.s moves them
 * every frame between their places at the last two logic steps.
 *
 * The HUD: two text lines (ANTIC 2) under the room.
 *
 * port: everything is drawn in front of the room's picture: nothing hides
 * behind a pillar or an arch, and there are no shadows.
 */
#include <string.h>
#include "a8game.h"
#include "a8xe.h"
#include "keep.h"
#include "view.h"
#include "a8log.h"
#ifndef EXP_DMA
#define EXP_DMA 0x3E
#endif

extern int32_t __fastcall__ smul16(int16_t a, int16_t b);

/* draw.s */
extern uint8_t *dr_ops;
extern uint16_t dr_n;
extern uint8_t dr_nogates, db_id, db_x0, db_w, db_y0, db_h;
extern int16_t db_x, db_y;
void draw_ops(void);
void draw_bmp(void);
void bmp_box(void);
void save_bg(void);
void restore_box(void);
/* disp.s, sprites.s */
extern uint8_t dli_c0[26], dli_c1[26], dli_c2[26];
void disp_install(void);
extern uint8_t spr_rows[2][4][48], spr_h[2][4], spr_x0[2][4], spr_x1[2][4], spr_y0[2][4], spr_y1[2][4];
extern uint8_t spr_size[2][4], spr_flip, spr_set, spr_shown_x[4], spr_shown_y[4];
void spr_reset(void);
extern uint8_t fc_frame, fc_set, fc_p, fc_multi, fc_h, fc_xo, fc_yo, fc_dbl;
void frame_copy(void);
/* the banks' tables (gfx_data.s) */
extern uint8_t gfx_rooms[], gfx_frames[], gfx_bmp[];

#define BANKOFF(p) ((uint16_t)(p) - 0x4000)

/* ---- the screen's memory (view.h) */
static uint8_t band_dli[26];                /* a DLI at the end of band b (its line 7) */

static void build_dl(void)
{
    uint8_t *d = DL, l;
    *d++ = 0x70;
    *d++ = 0x70;
    for (l = 0; l < LINES; ++l) {
        uint8_t op = 0x0E;
        if (l == 0 || l == 102 || l == 204) op = 0x4E;
        if ((l & 7) == 7 && band_dli[l >> 3]) op |= 0x80;
        *d++ = op;
        if (op & 0x40) {
            uint16_t a = (uint16_t)LINE_LO[l] | ((uint16_t)LINE_HI[l] << 8);
            *d++ = (uint8_t)a;
            *d++ = (uint8_t)(a >> 8);
        }
    }
    *d++ = 0x42;                            /* the HUD: 2 rows of ANTIC 2 */
    *d++ = (uint8_t)(uint16_t)HUD;
    *d++ = (uint8_t)((uint16_t)HUD >> 8);
    *d++ = 0x02;
    *d++ = 0x41;
    *d++ = (uint8_t)(uint16_t)DL;
    *d++ = (uint8_t)((uint16_t)DL >> 8);
}

void view_init(void)
{
    uint8_t l;
    uint16_t a;
    for (l = 0; l < LINES; ++l) {
        a = l < 204 ? 0x8010 + (uint16_t)l * 40 : 0xA000 + (uint16_t)(l - 204) * 40;
        LINE_LO[l] = (uint8_t)a;
        LINE_HI[l] = (uint8_t)(a >> 8);
    }
    build_dl();
    memset(HUD, 0, 80);
    PMBASE = 0xB0;
    GRACTL = 3;
    GPRIOR = 0x21;                          /* players over the room, multicolour players */
    PCOLR0 = 0x04;                          /* the explorer: grey, light grey, their OR white */
    (&PCOLR0)[1] = 0x0A;
    COLOR4 = 0x00;
    band_dli[25] = 1;
    dli_c1[0] = 0x0E;
    dli_c2[0] = 0x82;
    spr_reset();
    SDLSTL = (uint16_t)DL;                  /* ours first: the XEX loaded code over the */
    SDMCTL = 0x3E;                          /*   OS's display list, $BC20 */
    frame_wait();
    frame_wait();
    disp_install();
}

/* ---- the projection (scene.c): the original's pixels; the Atari's x is half */

extern fix pj_x, pj_y, pj_z;                /* proj.s */
extern int16_t pj_ox, pj_oy, pj_sx, pj_sy;
void proj_xyz(void);

static void proj(fix x, fix y, fix z, int16_t *sx, int16_t *sy)
{
    pj_x = x;
    pj_y = y;
    pj_z = z;
    proj_xyz();
    *sx = pj_sx;
    *sy = pj_sy;
}

/* ---- the room's picture */
static uint8_t gidx;                        /* the room's index in gfx_rooms */

/* the bitmap sprites drawn now: id, anchor, box */
#define MAXBMP 24
static uint8_t nb, b_id[MAXBMP], b_x0[MAXBMP], b_w[MAXBMP], b_y0[MAXBMP], b_h[MAXBMP];
static int16_t b_x[MAXBMP], b_y[MAXBMP];

static void bmp_list(uint8_t *n, uint8_t *id, int16_t *x, int16_t *y)
{
    uint8_t i, k = 0;
    int16_t sx, sy;
    for (i = 0; i < R.nlift && k < MAXBMP - 1; ++i) {
        const Lift *l = &R.lift[i];
        const Body *b = l->body;
        int16_t pk = (int16_t)((b->py - l->base + FX / 4 - 1) >> (FXS - 2));   /* (>= 0: a shift) */
        proj(b->px, b->py, b->pz, &sx, &sy);
        if (pk > 8) pk = 8;
        if (pk > 0) { id[k] = (uint8_t)(1 + pk); x[k] = sx; y[k] = sy; ++k; }
        id[k] = 1; x[k] = sx; y[k] = sy; ++k;
    }
    for (i = 0; i < R.npk && k < MAXBMP; ++i) {
        const Pickup *p = &R.pk[i];
        proj(((fix)p->x << FXS) + FX / 2, (fix)p->y << FXS, ((fix)p->z << FXS) + FX / 2, &sx, &sy);
        id[k] = p->kind == 'R' ? 10 : p->kind == 'K' ? 11 : 12;
        x[k] = sx; y[k] = sy; ++k;
    }
    for (i = 0; i < R.nact && k < MAXBMP; ++i) {
        const Body *b = R.act[i].body;
        if (R.act[i].kind != K_CRATE) continue;
        proj(b->px, b->py, b->pz, &sx, &sy);
        id[k] = 0; x[k] = sx; y[k] = sy; ++k;
    }
    *n = k;
}

static void bmp_draw_all(void)
{
    uint8_t i;
    for (i = 0; i < nb; ++i) {
        db_id = b_id[i];
        db_x = b_x[i];
        db_y = b_y[i];
        bmp_box();
        b_x0[i] = db_x0; b_w[i] = db_w; b_y0[i] = db_y0; b_h[i] = db_h;
        draw_bmp();
    }
}

/* after a step: what moved is restored and drawn again */
static void bmp_update(void)
{
    static uint8_t n2, id2[MAXBMP];
    static int16_t x2[MAXBMP], y2[MAXBMP];
    uint8_t i;
    bmp_list(&n2, id2, x2, y2);
    if (n2 == nb && !memcmp(id2, b_id, nb) && !memcmp(x2, b_x, nb * 2) && !memcmp(y2, b_y, nb * 2)) return;
    for (i = 0; i < nb; ++i) {
        db_x0 = b_x0[i]; db_w = b_w[i]; db_y0 = b_y0[i]; db_h = b_h[i];
        restore_box();
    }
    nb = n2;
    memcpy(b_id, id2, nb);
    memcpy(b_x, x2, nb * 2);
    memcpy(b_y, y2, nb * 2);
    bmp_draw_all();
}

void view_room(void)
{
    uint8_t hdr[6], i, cols[26 * 3];       /* (on the C stack: outside the bank window) */
    const KRoom *r = R.room;
    SDMCTL = 0;
    spr_reset();
    gidx = (uint8_t)(R.level->first_gfx + R.ridx);
    xe_bank_read(1, BANKOFF(gfx_rooms) + gidx * 6, hdr, 6);
    dr_ops = (uint8_t *)((uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8));
    dr_n = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    xe_bank_read(2, (uint16_t)hdr[4] + ((uint16_t)hdr[5] << 8) - 0x4000, cols, sizeof cols);
    for (i = 0; i < LINES; ++i)
        memset((uint8_t *)((uint16_t)LINE_LO[i] | ((uint16_t)LINE_HI[i] << 8)), 0, 40);
    dr_nogates = R.ngate == 0;
    {
        uint8_t t = RTCLOK2;
        draw_ops();
        a8_log("sk: drew room=%s ops=%u frames=%u\n", R.room->id, dr_n, (uint8_t)(RTCLOK2 - t));
    }
    save_bg();
    COLOR0 = cols[0];
    COLOR1 = cols[1];
    COLOR2 = cols[2];
    /* port: a DLI only where the next band's colours differ (each costs up
       to a line of the CPU's time); the last one sets the HUD's */
    {
        uint8_t k = 0, b;
        for (b = 0; b < 25; ++b) {
            band_dli[b] = memcmp(cols + b * 3, cols + b * 3 + 3, 3) != 0;
            if (band_dli[b]) {
                dli_c0[k] = cols[b * 3 + 3];
                dli_c1[k] = cols[b * 3 + 4];
                dli_c2[k] = cols[b * 3 + 5];
                ++k;
            }
        }
        band_dli[25] = 1;
        dli_c0[k] = 0;
        dli_c1[k] = 0x0E;                   /* the HUD: white on dark blue */
        dli_c2[k] = 0x82;
        build_dl();
    }
    pj_ox = 160 + ISO_X * (r->d - r->w) / 2;
    pj_oy = 100 + (3 * ISO_Y + ISO_Z * (r->w + r->d + 2)) / 2;
    bmp_list(&nb, b_id, b_x, b_y);
    bmp_draw_all();
    view_sprites(1);
    SDMCTL = EXP_DMA;
}

/* ---- the characters */
static const uint8_t walk_frame[4] = { 0, 1, 0, 2 };
static const uint8_t npc_colour[7] = { 0, 0x34, 0x1C, 0x58, 0xCA, 0xB6, 0 };   /* by K_*, the original's: guard
                                                                                red, hound yellow, ghost purple,
                                                                                bouncer green, sage green */

static uint8_t frame_of(const Actor *a)
{
    uint8_t fr = walk_frame[(((a->phase >> 8) + 32) >> 6) & 3];
    switch (a->kind) {
    case K_PLAYER: return (uint8_t)(a->face * 3 + fr);
    case K_GUARD: return (uint8_t)(24 + a->face * 3 + fr);
    case K_HOUND: return (uint8_t)(48 + a->face * 3 + fr);
    case K_SAGE: return (uint8_t)(72 + a->face);
    case K_GHOST: return (uint8_t)(80 + a->face);
    case K_BOUNCER: return (uint8_t)(88 + (a->roll & 7));
    }
    return 0xFF;
}

/* a frame's rows into set s, player p (and p + 1 for the explorer); the
   rows are copied only when the set held another frame */
static uint8_t set_frame[2][4] = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}};
static uint8_t fr_h[2][4];
static int8_t fr_xo[2][4], fr_yo[2][4];

static void put_frame(uint8_t s, uint8_t p, uint8_t frame, int16_t sx, int16_t sy, uint8_t multi, uint8_t snap)
{
    uint8_t h, hp;
    int16_t x, y;
    if (set_frame[s][p] != frame) {
        set_frame[s][p] = frame;
        fc_frame = frame;
        fc_set = s;
        fc_p = p;
        fc_multi = multi;
        frame_copy();
        fr_h[s][p] = fc_h;
        fr_xo[s][p] = (int8_t)fc_xo;
        fr_yo[s][p] = (int8_t)fc_yo;
        spr_size[s][p] = spr_size[s][p + multi] = fc_dbl ? 1 : 0;
    }
    h = fr_h[s][p];
    x = 48 + sx + fr_xo[s][p];
    y = PM_TOP + sy + fr_yo[s][p];
    if (x < 0 || x > 255 || y < 0 || y + h > 255) h = 0;
    for (hp = 0; hp <= multi; ++hp) {
        uint8_t q = (uint8_t)(p + hp);
        spr_h[s][q] = h;
        spr_x1[s][q] = (uint8_t)x;
        spr_y1[s][q] = (uint8_t)y;
        spr_x0[s][q] = snap ? (uint8_t)x : spr_shown_x[q];
        spr_y0[s][q] = snap ? (uint8_t)y : spr_shown_y[q];
    }
}

/* after a step (or entering a room: snap, no sliding in) */
void view_sprites(uint8_t snap)
{
    uint8_t s, i, p = 2;
    int16_t sx, sy;
    while (spr_flip) ;                      /* (the VBI hasn't taken the last set yet) */
    s = spr_set ^ 1;
    for (i = 0; i < 4; ++i) spr_h[s][i] = 0;
    for (i = 0; i < R.nact; ++i) {
        const Actor *a = &R.act[i];
        uint8_t fr = frame_of(a);
        if (fr == 0xFF) continue;
        proj(a->body->px, a->body->py, a->body->pz, &sx, &sy);
        if (a->kind == K_PLAYER) {
            if (!a->visible) continue;
            put_frame(s, 0, fr, sx, sy, 1, snap);
        } else if (p < 4) {
            put_frame(s, p, fr, sx, sy, 0, snap);
            (&PCOLR0)[p] = npc_colour[a->kind];
            ++p;
        }
    }
    spr_flip = 1;
}

/* ---- the HUD */
static void hud_put(uint8_t row, uint8_t col, const char *s)
{
    uint8_t *d = HUD + row * 40 + col;
    while (*s && d < HUD + row * 40 + 40) {
        uint8_t c = (uint8_t)*s++;
        if (c >= 'a' && c <= 'z') c -= 32;
        *d++ = c < 0x20 ? 0 : c < 0x60 ? (uint8_t)(c - 0x20) : c;
    }
}

static char *num(char *p, int16_t v)
{
    if (v >= 10) *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    *p = 0;
    return p;
}

static uint8_t hud_msg, hud_state = 0xFF;
static int8_t hud_l = -1, hud_k = -1, hud_r = -1;
static const KRoom *hud_room;

void view_hud(uint8_t force)
{
    char b[41], *p;
    uint8_t vis = G.clock < G.msg_until && G.message[0];   /* (game_message_visible is in bank 0) */
    if (force || G.lives != hud_l || G.keys != hud_k || G.relics != hud_r || R.room != hud_room ||
        G.state != hud_state) {
        hud_l = G.lives; hud_k = G.keys; hud_r = G.relics; hud_room = R.room; hud_state = G.state;
        memset(HUD, 0, 40);
        if (G.state == GS_TITLE) {
            hud_put(0, 0, "SPECTRAL KEEP");
            hud_put(0, 40 - (uint8_t)strlen(G.level->name), G.level->name);
        } else if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) {
            /* the original's end: relics found, rooms explored */
            p = b;
            p = num(p, G.relics);
            memcpy(p, " RELICS  ", 9); p += 9;
            p = num(p, G.nvisited);
            *p++ = '/';
            p = num(p, G.level->nrooms);
            memcpy(p, " ROOMS", 7);
            hud_put(0, 0, G.state == GS_VICTORY ? "VICTORY!" : "GAME OVER");
            hud_put(0, (uint8_t)(40 - strlen(b)), b);
        } else {
            hud_put(0, 0, R.room->name);
            p = b;
            *p++ = 'L'; *p++ = 'I'; *p++ = 'V'; *p++ = 'E'; *p++ = 'S'; *p++ = ' ';
            p = num(p, G.lives);
            *p++ = ' '; *p++ = 'K'; *p++ = 'E'; *p++ = 'Y'; *p++ = 'S'; *p++ = ' ';
            p = num(p, G.keys);
            *p++ = ' '; *p++ = 'R'; *p++ = 'E'; *p++ = 'L'; *p++ = 'I'; *p++ = 'C'; *p++ = 'S'; *p++ = ' ';
            p = num(p, G.relics);
            *p++ = '/';
            p = num(p, G.level->relics);
            hud_put(0, (uint8_t)(40 - (p - b)), b);
        }
        force = 1;
    }
    if (force || G.msg_changed || vis != hud_msg) {
        G.msg_changed = 0;
        hud_msg = vis;
        memset(HUD + 40, 0, 40);
        if (G.state == GS_TITLE) hud_put(1, 0, "FIRE: ENTER   UP/DOWN: CHOOSE A KEEP");
        else if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) hud_put(1, 12, "FIRE: THE TITLE");
        else if (vis) hud_put(1, (uint8_t)(strlen(G.message) < 40 ? (40 - strlen(G.message)) / 2 : 0), G.message);
    }
}

void view_step(void)
{
    if (G.room_changed) {
        G.room_changed = 0;
        view_room();
        view_hud(1);
        return;
    }
    view_sprites(0);
    bmp_update();
    view_hud(0);
}
