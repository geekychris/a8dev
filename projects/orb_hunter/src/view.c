/*
 * Orb Hunter - the Atari display (the original's draw.c, redone for the
 * hardware).
 *
 * The screen (192 lines) is the original's 320 x 256 at x / 2, y * 3 / 4:
 *
 *   0-11    the HUD (where the original draws it, over the room's top row):
 *           energy, rockets, the area (ANTIC 6, 20 columns, 4 colours)
 *   12-191  the room's tile rows 1..15: ANTIC E, 160 x 180, a tile 8 x 12
 *
 * Colour: four in ANTIC E, so a DLI at every tile row sets that row's: the
 * area's rock (PF0, PF1: green caverns, red magma halls, purple dens ...)
 * and the row's special colour (PF2: its lava, else its barred doors, else
 * its metal, else the orbs and save point; a lava row's highlights glow
 * orange too). PF3 is the shots' colour (the missiles are the fifth player).
 *
 * The hunter is players 0 and 1 (its two colours), the creatures players
 * 2 and 3 (a boss is both, 32 pixels wide), the hunter's shots missiles.
 * Bombs, the creatures' shots and the sparks are XORed points (gfx.s).
 * A room is drawn once when it is entered (gfx.s, ~3 frames); then only
 * lava (animated a quarter at a time), opened doors and taken orbs.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "view.h"

#define BITMAP   ((uint8_t *)0xA010)   /* 180 lines x 40: line 102 is $B000 */
#define HUD      ((uint8_t *)0xBC40)   /* 20 */
#define DL_GAME  ((uint8_t *)0xBD00)
#define FONT     ((uint8_t *)0xBE00)   /* 512: ANTIC 6's 64 glyphs */
#define TITLE    ((uint8_t *)0xA010)   /* the title's rows (the room is free then) */
#define DL_TITLE ((uint8_t *)0xA800)
#define LINES    180

extern uint8_t dli_bk[16], dli_pf0[16], dli_pf1[16], dli_pf2[16], dli_pf3[16];
void irq_install(void);

/* gfx.s */
extern uint8_t dt_tile, dt_x, dt_row;
void draw_tile(void);
void draw_room(void);
#define NDOTS 40
extern uint8_t dn_px[NDOTS], dn_ln[NDOTS], dn_w[NDOTS];
void dots_flush(void);
void dots_reset(void);
extern uint8_t pp_n, pp_x, pp_line, pp_h, pp_key;
extern const uint8_t *pp_shape;
extern uint8_t p_line[4], p_h[4], p_key[4];
void ply_put(void);

/* data.c: the sprites */
extern const uint8_t sh_hunter0_a[12], sh_hunter0_b[12], sh_hunter1_a[12], sh_hunter1_b[12];
extern const uint8_t sh_roll0_a[6], sh_roll0_b[6], sh_roll1_a[6], sh_roll1_b[6];
extern const uint8_t sh_roll2_a[6], sh_roll2_b[6], sh_roll3_a[6], sh_roll3_b[6];
extern const uint8_t sh_crawler0[12], sh_crawler1[12], sh_flyer0[12], sh_flyer1[12], sh_hopper[12];
extern const uint8_t sh_turret0[12], sh_turret1[12];
extern const uint8_t sh_beast_l[24], sh_beast_r[24], sh_drake0_l[24], sh_drake0_r[24];
extern const uint8_t sh_drake1_l[24], sh_drake1_r[24], sh_orb0[12];

/* ---- colours */
#define C_BG       A8_COLOR(8, 0)
#define C_HUNTER   A8_COLOR(11, 8)
#define C_HUNTER2  A8_COLOR(1, 12)
#define C_ENEMY    A8_COLOR(5, 10)
#define C_LAVA     A8_COLOR(3, 6)
#define C_LAVA2    A8_COLOR(2, 12)
#define C_DOOR     A8_COLOR(8, 8)
#define C_METAL    A8_COLOR(0, 6)
#define C_METAL2   A8_COLOR(0, 12)
#define C_ITEM     A8_COLOR(10, 12)
#define C_BEAM     A8_COLOR(12, 12)

/* the areas (the original names them by the room's row): rock, its highlight */
static const uint8_t area_rock[6][2] = {
    {A8_COLOR(12, 2), A8_COLOR(12, 6)},     /* CAVERN: green */
    {A8_COLOR(3, 2), A8_COLOR(3, 6)},       /* MAGMA: red-brown */
    {A8_COLOR(5, 2), A8_COLOR(5, 6)},       /* BEAST DEN: purple */
    {A8_COLOR(9, 2), A8_COLOR(9, 6)},       /* WYRM HOLLOW: deep blue */
    {A8_COLOR(7, 2), A8_COLOR(7, 6)},       /* CITADEL: blue-grey */
    {A8_COLOR(1, 4), A8_COLOR(1, 8)},       /* SURFACE: tan */
};
static const char *const area_names[6] = {"CAVERN", "MAGMA", "BEAST DEN", "WYRM HOLLOW", "CITADEL", "SURFACE"};

static uint8_t scode(char c)
{
    uint8_t a = (uint8_t)c;
    if (a < 0x20) return (uint8_t)(a + 0x40);
    if (a < 0x60) return (uint8_t)(a - 0x20);
    return a;
}

static void put6(uint8_t *dst, const char *s, uint8_t pf)
{
    while (*s) *dst++ = (uint8_t)((scode(*s++) & 0x3F) | (pf << 6));
}

/* a number by subtraction (cc65's divide is slow), `digits` wide */
static void num6(uint8_t *dst, uint16_t v, uint8_t digits, uint8_t pf)
{
    uint8_t h = 0, t = 0;
    pf = (uint8_t)((pf << 6) | 0x10);
    while (v >= 100) { v -= 100; ++h; }
    while (v >= 10) { v -= 10; ++t; }
    if (digits == 3) *dst++ = (uint8_t)(pf | h);
    *dst++ = (uint8_t)(pf | t);
    *dst = (uint8_t)(pf | (uint8_t)v);
}

/* ---- display lists */
static uint8_t *dl_lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

static void dl_build(void)
{
    uint8_t *d = DL_GAME, l;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = dl_lms(d, 0x46, HUD);               /* the HUD: 8 lines */
    *d++ = 0xB0;                            /* + 4 blank, DLI 0: tile row 1's colours */
    for (l = 0; l < LINES; ++l) {
        uint8_t op = (uint8_t)(0x0E | (l % 12 == 11 && l < LINES - 1 ? 0x80 : 0));
        if (l == 0 || l == 102) d = dl_lms(d, (uint8_t)(op | 0x40), BITMAP + l * 40);
        else *d++ = op;
    }
    d = dl_lms(d, 0x41, DL_GAME);
}

/* each tile row's colours (DLI r - 1 sets row r's) */
static const uint8_t tile_flag[NUM_TILE_TYPES] = {0, 0, 0, 4, 0, 1, 2, 0, 0, 0};

static void row_colours(void)
{
    static uint8_t r, c, a, f, k, lava, door, metal;
    static const uint8_t *row;
    a = (uint8_t)gs.room_y;
    if (a > 5) a = 5;
    for (r = 1; r < ROOM_H; ++r) {
        k = (uint8_t)(r - 1);
        row = room[r];
        f = 0;
        for (c = 0; c < ROOM_W; ++c) f |= tile_flag[row[c]];
        lava = f & 1;
        door = f & 2;
        metal = f & 4;
        dli_bk[k] = C_BG;
        dli_pf0[k] = area_rock[a][0];
        dli_pf1[k] = lava ? C_LAVA2 : metal ? C_METAL2 : area_rock[a][1];
        dli_pf2[k] = lava ? C_LAVA : door ? C_DOOR : metal ? C_METAL : C_ITEM;
        dli_pf3[k] = C_BEAM;
    }
}

/* ---- the font: the OS's first 64 glyphs, and an energy cell */
static void font_init(void)
{
    memcpy(FONT, (const void *)0xE000, 512);
}

/* ---- the room */
static uint8_t lava_x[48], lava_r[48], n_lava;

/* an orb (an item not taken yet) at its tile: 4 pixels x 6 lines of
   colour 3, byte-aligned */
static void orb_put(uint8_t tx, uint8_t ty, uint8_t on)
{
    static const uint8_t nib3[16] = {
        0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
        0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF,
    };
    uint8_t l, line;
    uint8_t *p;
    if (ty < 1 || ty >= ROOM_H) return;
    line = (uint8_t)((ty - 1) * 12 + 2);
    p = BITMAP + line * 40 + tx * 2;
    for (l = 0; l < 6; ++l, p += 40)
        *p = on ? nib3[sh_orb0[l] >> 4] : 0;
}

static uint8_t drawn_room = 0xFF;
static uint8_t dots_shown;              /* dots drawn last frame */

void view_room(void)
{
    uint8_t r, c, i;
    dots_reset();
    dots_shown = 0;
    draw_room();
    row_colours();
    n_lava = 0;
    for (r = 1; r < ROOM_H; ++r)
        if (dli_pf2[r - 1] == C_LAVA)       /* (rows with lava: row_colours) */
            for (c = 0; c < ROOM_W; ++c)
                if (room[r][c] == TILE_LAVA && n_lava < sizeof lava_x) {
                    lava_x[n_lava] = c;
                    lava_r[n_lava++] = r;
                }
    for (i = 0; i < g_num_items; ++i) {
        ItemSpawn *it = &g_items[i];
        if (!it->collected && it->room_x == gs.room_x && it->room_y == gs.room_y)
            orb_put((uint8_t)it->tile_x, (uint8_t)it->tile_y, 1);
    }
    drawn_room = (uint8_t)(gs.room_y * WORLD_W + gs.room_x);
    view_hud_reset();
}

/* tiles the game changed (opened doors), orbs taken */
static uint16_t seen_items;

static void room_changes(void)
{
    uint8_t i;
    /* an item taken: rub its orb out */
    if (gs.items_collected != seen_items) {
        for (i = 0; i < g_num_items; ++i) {
            ItemSpawn *it = &g_items[i];
            if (it->collected && !(seen_items & ((uint16_t)1 << i)) &&
                it->room_x == gs.room_x && it->room_y == gs.room_y)
                orb_put((uint8_t)it->tile_x, (uint8_t)it->tile_y, 0);
        }
        seen_items = gs.items_collected;
    }
}

/* a door opened (world_set_tile): redraw it */
void view_tile_changed(uint8_t tx, uint8_t ty)
{
    if (ty < 1) return;
    dt_tile = room[ty][tx];
    dt_x = tx;
    dt_row = ty;
    draw_tile();
}

static void lava_step(void)
{
    /* the original's lava moves every frame; here a quarter of the tiles a
       frame, each 4 frames on, with the surface's 4 phases */
    uint8_t i, f = (uint8_t)(gs.frame & 3), ph = (uint8_t)((gs.frame >> 4) & 3);
    for (i = f; i < n_lava; i += 4) {
        dt_tile = (uint8_t)(10 + ph);
        dt_x = lava_x[i];
        dt_row = lava_r[i];
        draw_tile();
    }
}

/* ---- HUD */
static WORD hud_health = -1, hud_miss = -1;
static uint8_t hud_area = 0xFF, hud_msg;

void view_hud_reset(void)
{
    hud_health = hud_miss = -1;
    hud_area = 0xFF;
    hud_msg = 0;
}

void view_message(const char *s)
{
    memset(HUD, 0, 20);
    if (s) put6(HUD + ((20 - strlen(s)) >> 1), s, 1);
    hud_msg = s != 0;
    if (!s) view_hud_reset();
}

static void view_hud(void)
{
    WORD v;
    if (hud_msg) return;
    if (h_health != hud_health) {
        hud_health = h_health;
        put6(HUD, "E", 0);
        v = h_health < 0 ? 0 : h_health;
        num6(HUD + 1, (uint16_t)v, 3, 0);
    }
    if (h_missiles != hud_miss) {
        hud_miss = h_missiles;
        if (h_has_missiles) {
            put6(HUD + 5, "M", 1);
            num6(HUD + 6, (uint16_t)h_missiles, 2, 1);
        }
    }
    if ((uint8_t)gs.room_y != hud_area) {
        uint8_t a = (uint8_t)gs.room_y;
        hud_area = a;
        if (a > 5) a = 5;
        memset(HUD + 9, 0, 11);
        put6(HUD + 20 - strlen(area_names[a]), area_names[a], 2);
    }
}

/* ---- sprites */
#define PCOLR_(n, c) (*(uint8_t *)(0x2C0 + (n)) = (c))
#define SIZEP_(n, s) (*(volatile uint8_t *)(0xD008 + (n)) = (s))
#define HPOSM_(n, x) (*(volatile uint8_t *)(0xD004 + (n)) = (x))

static void hide(uint8_t n)
{
    pp_n = n;
    pp_h = 0;
    ply_put();
}

/* the PM line of a room y (world pixels): 32 + y * 3 / 4 (y -16..256) */
static uint8_t pm_line(WORD y)
{
    uint16_t t = (uint16_t)(y + 64);
    return (uint8_t)(((t + (t << 1)) >> 2) - 16);
}

#define pm_x(x) ((uint8_t)(16 + ((uint16_t)((x) + 64) >> 1)))     /* 48 + x / 2 */

/* a player at world (put_x, put_y): (globals: cc65's six-argument calls
   cost more than the drawing) */
static WORD put_x, put_y;

static void put(void)
{
    if (put_x < -32 || put_x > SCREEN_W || put_y < -16 || put_y > SCREEN_H) {
        pp_h = 0;
        ply_put();
        return;
    }
    pp_x = pm_x(put_x);
    pp_line = pm_line(put_y);
    ply_put();
}
#define PUT(n, x, y, shape, h, key) do { pp_n = (n); put_x = (x); put_y = (y); \
    pp_shape = (shape); pp_h = (h); pp_key = (key); put(); } while (0)

static void view_hunter(void)
{
    static WORD x, y;
    x = FIX_INT(h_x) - 2;
    y = FIX_INT(h_y);
    if (!h_alive || (h_invuln_timer && (gs.frame & 4))) {   /* port: flickers */
        hide(0);
        hide(1);
        return;
    }
    if (h_rolled) {
        static const uint8_t *const ra[4] = {sh_roll0_a, sh_roll1_a, sh_roll2_a, sh_roll3_a};
        static const uint8_t *const rb[4] = {sh_roll0_b, sh_roll1_b, sh_roll2_b, sh_roll3_b};
        uint8_t ph = (uint8_t)((gs.frame >> 1) & 3);
        PUT(0, x, y, ra[ph], 6, (uint8_t)(10 + ph));
        PUT(1, x, y, rb[ph], 6, (uint8_t)(10 + ph));
    } else {
        PUT(0, x, y, h_facing ? sh_hunter1_a : sh_hunter0_a, 12, (uint8_t)(1 + h_facing));
        PUT(1, x, y, h_facing ? sh_hunter1_b : sh_hunter0_b, 12, (uint8_t)(1 + h_facing));
    }
}

static void view_enemies(void)
{
    static uint8_t i, n, key;
    static WORD x, y;
    static const uint8_t *s;
    n = 2;
    for (i = 0; i < MAX_ENEMIES && n < 4; ++i) {
        if (!e_on[i]) continue;
        x = FIX_INT(e_x[i]);
        y = FIX_INT(e_y[i]);
        switch (e_type[i]) {
        case ENEMY_CRAWLER: s = e_facing[i] ? sh_crawler1 : sh_crawler0; key = (uint8_t)(20 + e_facing[i]); break;
        case ENEMY_FLYER: s = (e_anim_frame[i] & 1) ? sh_flyer1 : sh_flyer0; key = (uint8_t)(22 + (e_anim_frame[i] & 1)); break;
        case ENEMY_HOPPER: s = sh_hopper; key = 24; break;
        case ENEMY_TURRET: s = e_anim_frame[i] ? sh_turret1 : sh_turret0; key = (uint8_t)(25 + e_anim_frame[i]); break;
        default: {
            /* a boss: 32 pixels wide, players 2 and 3 */
            uint8_t w = (uint8_t)((e_anim_frame[i] >> 1) & 1);
            if (e_type[i] == ENEMY_BOSS_BEAST) {
                PUT(2, x, y, sh_beast_l, 24, 30);
                PUT(3, x + 16, y, sh_beast_r, 24, 31);
            } else {
                PUT(2, x, y, w ? sh_drake1_l : sh_drake0_l, 24, (uint8_t)(32 + w));
                PUT(3, x + 16, y, w ? sh_drake1_r : sh_drake0_r, 24, (uint8_t)(34 + w));
            }
            PCOLR_(2, C_ENEMY);
            PCOLR_(3, C_ENEMY);
            n = 4;
            continue;
        }
        }
        PUT(n, x, y, s, 12, key);
        PCOLR_(n, e_type[i] == ENEMY_TURRET ? C_METAL2 : C_ENEMY);
        ++n;
    }
    for (; n < 4; ++n) hide(n);
}

/* the hunter's shots: missiles 0..3 (a beam 2 lines, a rocket 3 and wide) */
static uint8_t m_line[4], m_h[4];

static void view_shots(void)
{
    uint8_t *mem = (uint8_t *)0x9B00;
    uint8_t i, m = 0, l, size = 0;
    for (i = 0; i < 4; ++i) {                        /* clear last frame's */
        uint8_t mask = (uint8_t)~(3 << (i * 2));
        for (l = 0; l < m_h[i]; ++l) mem[m_line[i] + l] &= mask;
        m_h[i] = 0;
        HPOSM_(i, 0);
    }
    for (i = 0; i < MAX_BULLETS && m < 4; ++i) {
        uint8_t line, h;
        if (!b_on[i]) continue;
        h = b_type[i] == BULLET_TYPE_MISSILE ? 3 : 2;
        line = pm_line(FIX_INT(b_y[i]));
        for (l = 0; l < h; ++l) mem[line + l] |= (uint8_t)(3 << (m * 2));
        m_line[m] = line;
        m_h[m] = h;
        HPOSM_(m, pm_x(FIX_INT(b_x[i]) - (b_dx[i] < 0 ? 4 : 0)));
        if (h == 3) size |= (uint8_t)(1 << (m * 2));  /* rockets twice as wide */
        ++m;
    }
    *(volatile uint8_t *)0xD00C = size;              /* SIZEM */
}

/* bombs, the creatures' shots and the sparks: XORed points (world x / 2,
   (y - 16) * 3 / 4: the room's line) */
static uint8_t n_dot;

static void dot(Fixed fx, Fixed fy, uint8_t w)
{
    static WORD x, y;
    x = FIX_INT(fx);
    y = FIX_INT(fy) - 16;
    dn_px[n_dot] = 0xFF;
    if ((uint16_t)x < SCREEN_W && (uint16_t)y < 240) {
        dn_px[n_dot] = (uint8_t)((uint16_t)x >> 1);
        dn_ln[n_dot] = (uint8_t)(((uint16_t)y + ((uint16_t)y << 1)) >> 2);
        dn_w[n_dot] = w;
    }
    ++n_dot;
}

static void view_dots(void)
{
    static uint8_t i, any;
    /* none now and none drawn: nothing to do */
    any = 0;
    for (i = 0; i < MAX_BOMBS; ++i) any |= bm_on[i];
    for (i = 0; i < MAX_ENEMY_BULLETS; ++i) any |= eb_on[i];
    for (i = 0; i < MAX_PARTICLES; ++i) any |= p_on[i];
    if (!any && !dots_shown) return;
    dots_shown = any;
    n_dot = 0;
    for (i = 0; i < MAX_BOMBS; ++i)
        if (bm_on[i]) dot(bm_x[i], bm_y[i], 2); else dn_px[n_dot++] = 0xFF;
    for (i = 0; i < MAX_ENEMY_BULLETS; ++i)
        if (eb_on[i]) dot(eb_x[i], eb_y[i], 2); else dn_px[n_dot++] = 0xFF;
    for (i = 0; i < MAX_PARTICLES; ++i)
        if (p_on[i]) dot(p_x[i], p_y[i], 1); else dn_px[n_dot++] = 0xFF;
    dots_flush();
}

void view_frame(void)
{
    if (gs.room_changed) {
        gs.room_changed = 0;
        view_room();
    }
    view_hunter();
    view_enemies();
    view_shots();
    view_dots();
    lava_step();
    room_changes();
    view_hud();
}

void view_hide(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) hide(i);
    memset((void *)0x9B00, 0, 256);
    memset(m_h, 0, sizeof m_h);
}

/* ---- screens */
void view_game(void)
{
    view_hide();
    SDLSTL = (uint16_t)DL_GAME;
    memset(HUD, 0, 20);
    COLOR0 = A8_COLOR(12, 10);             /* the HUD: energy */
    COLOR1 = A8_COLOR(1, 14);              /* rockets, messages */
    COLOR2 = C_ITEM;                       /* the area */
    COLOR3 = C_BEAM;
    COLOR4 = C_BG;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    seen_items = gs.items_collected;
    view_room();
}

/* the title */
static uint8_t *trow, *press_row;

static uint8_t *title_line(uint8_t *d, uint8_t mode, const char *s, uint8_t pf)
{
    memset(trow, 0, 20);
    if (s) put6(trow + ((20 - strlen(s)) >> 1), s, pf);
    d = dl_lms(d, (uint8_t)(0x40 | mode), trow);
    trow += 20;
    return d;
}

void view_title(const char (*names)[4], const uint32_t *scores)
{
    uint8_t *d = DL_TITLE;
    uint8_t i;
    char line[21];
    view_hide();
    trow = TITLE;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    d = title_line(d, 7, "ORB HUNTER", 1);
    d = title_line(d, 6, "THE LOST CAVERNS", 2);
    *d++ = 0x70;
    d = title_line(d, 6, "HIGH SCORES", 3);
    for (i = 0; i < 5; ++i) {
        uint32_t v = scores[i];
        uint8_t k;
        line[0] = (char)('1' + i);
        line[1] = ' ';
        memcpy(line + 2, names[i], 3);
        line[5] = ' ';
        for (k = 11; k > 5; --k) { line[k] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
        line[12] = 0;
        d = title_line(d, 6, line, i < 3 ? 0 : 2);
    }
    *d++ = 0x70;
    d = title_line(d, 6, "STICK  RUN  UP JUMP", 2);
    d = title_line(d, 6, "FIRE  SHOOT", 2);
    d = title_line(d, 6, "UP+FIRE  ROCKET", 2);
    d = title_line(d, 6, "DOWN  ROLL / BOMB", 2);
    *d++ = 0x70;
    press_row = trow;
    d = title_line(d, 6, "PRESS FIRE", 1);
    d = dl_lms(d, 0x41, DL_TITLE);
    COLOR0 = A8_COLOR(1, 14);
    COLOR1 = A8_COLOR(11, 10);
    COLOR2 = A8_COLOR(0, 8);
    COLOR3 = C_ITEM;
    COLOR4 = 0;
    CHBAS = (uint8_t)((uint16_t)FONT >> 8);
    SDLSTL = (uint16_t)DL_TITLE;
}

void view_title_frame(uint8_t t)
{
    if ((t & 31) == 0) {
        memset(press_row, 0, 20);
        if (t & 32) put6(press_row + 5, "PRESS FIRE", 1);
    }
}

void view_init(void)
{
    uint8_t t;
    SDMCTL = 0;                             /* (the OS screen is where our font goes) */
    t = RTCLOK2;
    while (RTCLOK2 == t)
        ;
    font_init();
    dl_build();
    memset(BITMAP, 0, LINES * 40);
    SDLSTL = (uint16_t)DL_GAME;
    SDMCTL = 0x22;
    pmg_init();
    GPRIOR = 0x11;                          /* players over the room; missiles = PF3 */
    PCOLR0 = C_HUNTER;
    *(&PCOLR0 + 1) = C_HUNTER2;
    memset(p_key, 0, 4);
    memset(p_h, 0, 4);
    irq_install();
}
