/*
 * view.c - Planet Chomp's view: the planet's maze round the chomper, laid
 * flat.
 *
 * port: the original flies a perspective camera over the sphere (the
 * Spectrum Next's version draws ~250 walls a frame at 28 MHz and gets 9
 * frames a second). Here the maze is unwrapped round the chomper's cell
 * onto a flat 24 x 15 grid: walking the middle row out from the chomper,
 * then each column up and down from it, carrying each cell's orientation
 * (which exit points up) across the cube's edges (exits run the other way
 * on every other face). Rows and columns are continuous; near the cube's
 * corners (three faces round a point, not four) neighbouring columns
 * disagree, and a cell can show twice. The view's orientation travels with
 * the chomper as the original's camera's up does.
 *
 * The grid is ANTIC mode 4 characters (gfx_data.c), a cell 2 x 2 of them,
 * in two maps: one shown, one rebuilt when the chomper reaches a new cell
 * (then the VBI swaps the display lists). The screen scrolls finely, both
 * ways (HSCROL, VSCROL), to keep the chomper in the middle between cells.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "maze.h"
#include "game.h"
#include "gfx_data.h"
#include "view.h"

#define GW 15                      /* the grid: cells */
#define GH 9
#define CX 7                       /* the chomper's cell */
#define CY 4
#define MAPW 48                    /* characters a map row (45 used) */
#define MAPH 28                    /* (27 used) */
#define CELL_W 12                      /* a cell: 3 x 3 characters, 12 pixels by 24 lines */
#define CELL_H 24
#define FONT  ((uint8_t *)0xA000)
#define MAP0  ((uint8_t *)0xA400)
#define MAP1  ((uint8_t *)0xAA00)
#define DL0   ((uint8_t *)0xB000)
#define DL1   ((uint8_t *)0xB100)
#define HUD   ((uint8_t *)0xB200)
#define VIS_ROWS 22
#define TOP   40                   /* the maze's first scan line */
#define HOFS  0                    /* (ANTIC's horizontal fetch, found by the test pattern) */

#pragma bss-name (push, "LOWRAM")          /* $0C00-$1FFF (the SDK's) */
static uint8_t gpos[2][CELLS];     /* each map's place of a cell: y * 16 + x, or 0xFF */
static uint8_t grot[2][CELLS];     /* the screen direction of exit 0 there; bit 2: exits run the other way */
static uint8_t * const gpos_of[2] = { gpos[0], gpos[1] };   /* (no multiply by CELLS) */
static uint8_t * const grot_of[2] = { grot[0], grot[1] };
static uint8_t content[CELLS];     /* what the middle of each cell shows: 0, 1 crumb, 2 key, 3 nest */
#pragma bss-name (pop)
static uint8_t shown;              /* the map on screen (0 / 1) */
static uint16_t shown_center = NONE, view_rot_cell = NONE;
static uint8_t view_rot;           /* the chomper's cell's exit-0 direction */
static uint8_t dl_pending;
uint8_t view_hscrol, view_vscrol;  /* the VBI writes them (vbi.s) */

/* the screen direction of exit k at a cell with orientation r (and its handedness) */
static uint8_t sdir(uint8_t r, uint8_t k) { return (uint8_t)((r & 4 ? (r - k) : (r + k)) & 3); }

uint8_t view_screen_dir(uint16_t cell, uint8_t exit)
{
    uint8_t r = grot_of[shown][cell];
    if (gpos_of[shown][cell] == 0xFF) r = view_rot;
    return sdir(r, exit);
}

int8_t view_exit_for(uint16_t cell, uint8_t screen_dir)
{
    uint8_t k;
    for (k = 0; k < 4; ++k) if (view_screen_dir(cell, k) == screen_dir) return (int8_t)k;
    return -1;
}


/* the unwrap (unwrap.s does it; this is its C): along the middle row out
   from the centre, then up and down each column from it, one step at a
   time: from cell c with orientation r towards screen direction s the exit
   is k = (s - r) & 3 (or (r - s) & 3 where exits run the other way), the
   neighbour n = NB(c, k), and n's orientation makes its exit back,
   REV(c, k), point the other way, (s + 2). Each place is drawn as it is
   reached; a cell's first place goes into gpos / grot. */

/* walls by screen side (bit s: 1 up, 2 right, 4 down, 8 left) for each
   orientation (8: exit 0's direction, bit 2 the handedness) and closed
   exits (bit k); the 8 outer characters of a cell for its walls
   (gen_gfx.py's numbers): made once, for unwrap.s */
extern uint16_t uw_center;
extern uint8_t uw_rot, *uw_map, *uw_gpos, *uw_grot, *uw_open, *uw_content;
extern uint8_t uw_wallmap[128], uw_ch[128];
extern void unwrap_asm(void);

static void draw_tables(void)
{
    uint8_t r, m, k, w, *c;
    for (r = 0; r < 8; ++r)
        for (m = 0; m < 16; ++m) {
            w = 0;
            for (k = 0; k < 4; ++k) if (m & (1 << k)) w |= (uint8_t)(1 << sdir(r, k));
            uw_wallmap[r * 16 + m] = w;
        }
    for (w = 0; w < 16; ++w) {
        c = uw_ch + w;                       /* parts: TL T TR L R BL B BR */
        c[0] = (uint8_t)(0 + ((w & 1) ? 2 : 0) + ((w & 8) ? 1 : 0));
        c[16] = (uint8_t)(16 + 0 + ((w & 1) ? 1 : 0));
        c[32] = (uint8_t)(4 + ((w & 1) ? 2 : 0) + ((w & 2) ? 1 : 0));
        c[48] = (uint8_t)(16 + 6 + ((w & 8) ? 1 : 0));
        c[64] = (uint8_t)(16 + 2 + ((w & 2) ? 1 : 0));
        c[80] = (uint8_t)(8 + ((w & 4) ? 2 : 0) + ((w & 8) ? 1 : 0));
        c[96] = (uint8_t)(16 + 4 + ((w & 4) ? 1 : 0));
        c[112] = (uint8_t)(12 + ((w & 4) ? 2 : 0) + ((w & 2) ? 1 : 0));
    }
    uw_open = g.open;
    uw_content = content;
}

/* a level's contents (view_level), and a cell's when it's eaten */
void view_level(void)
{
    uint16_t c;
    for (c = 0; c < CELLS; ++c) content[c] = g.crumb[c] ? 1 : g.key[c] ? 2 : c == mz_nest ? 3 : 0;
    shown_center = NONE;                     /* rebuild */
}

static void build_dl(uint8_t *dl, uint8_t *map)
{
    uint8_t r;
    *dl++ = 0x70;
    *dl++ = 0x70;
    *dl++ = 0x70;
    *dl++ = 0xC2;                         /* the HUD (the ROM's font), DLI: the maze's font */
    *dl++ = (uint8_t)(uint16_t)HUD;
    *dl++ = (uint8_t)((uint16_t)HUD >> 8);
    for (r = 0; r <= VIS_ROWS; ++r) {
        *dl++ = (uint8_t)(r < VIS_ROWS ? 0x64 : 0x44);    /* mode 4 + LMS (+ VSCROL) (port: no HSCROL: its wide fetch costs a third of the CPU) */
        *dl++ = (uint8_t)(uint16_t)map;
        *dl++ = (uint8_t)((uint16_t)map >> 8);
    }
    *dl = 0x41;                           /* (the jump's address: view_init) */
}

void view_init(void)
{
    SDMCTL = 0;
    draw_tables();
    memcpy(FONT, pc_font, 1024);
    CHBAS = 0xB4;                         /* the HUD: the ROM's font (os.s copied it there) */
    memset(MAP0, NONE_CHAR, MAPW * MAPH);
    memset(MAP1, NONE_CHAR, MAPW * MAPH);
    memset(HUD, 0, 80);
    build_dl(DL0, MAP0);
    build_dl(DL1, MAP1);
    DL0[3 + 3 + 3 * (VIS_ROWS + 1) + 1] = (uint8_t)(uint16_t)DL0;
    DL0[3 + 3 + 3 * (VIS_ROWS + 1) + 2] = (uint8_t)((uint16_t)DL0 >> 8);
    DL1[3 + 3 + 3 * (VIS_ROWS + 1) + 1] = (uint8_t)(uint16_t)DL1;
    DL1[3 + 3 + 3 * (VIS_ROWS + 1) + 2] = (uint8_t)((uint16_t)DL1 >> 8);
    COLOR0 = 0x96;                  /* walls: blue */
    COLOR1 = 0x1E;                  /* crumbs: pale yellow (and the HUD's letters) */
    COLOR2 = 0x2A;                  /* keys, the nest: gold (and the HUD's background: ) */
    COLOR3 = 0x1C;                  /* the chomper (the fifth player) */
    COLOR4 = 0x00;
    SDLSTL = (uint16_t)DL0;
    shown = 0;
    SDMCTL = 0x22 | 0x1C;
}

/* the window (map pixels) and the scroll registers for both lists */
static uint8_t *dl_of(uint8_t b) { return b ? DL1 : DL0; }

static void set_window(uint8_t b, int16_t wx, int16_t wy)
{
    uint8_t *dl = dl_of(b) + 6, r, col, fine, row, vs;
    uint8_t *map = b ? MAP1 : MAP0;
    uint16_t a;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    col = (uint8_t)(wx >> 2);
    fine = (uint8_t)(wx & 3);
    row = (uint8_t)(wy >> 3);
    vs = (uint8_t)(wy & 7);
    if (fine >= 2) ++col;                                /* by whole characters */
    fine = 0;
    a = (uint16_t)map + (uint16_t)row * MAPW + col + HOFS;
    for (r = 0; r <= VIS_ROWS; ++r, a += MAPW, dl += 3) {
        dl[1] = (uint8_t)a;
        dl[2] = (uint8_t)(a >> 8);
    }
    view_hscrol = fine;
    view_vscrol = vs;
}

/* the player's place on the shown map (map pixels: x of 160, y of 240) */
static uint8_t mover_xy(const Mover *m, uint8_t b, int16_t *x, int16_t *y)
{
    uint16_t c = m->from;
    uint8_t p = gpos_of[b][c], s;
    int16_t t;
    if (p == 0xFF) return 0;
    *x = (int16_t)((p & 15) * CELL_W + CELL_W / 2);
    *y = (int16_t)((p >> 4) * CELL_H + CELL_H / 2);
    if (m->moving) {
        s = sdir(grot_of[b][c], m->exitk);
        t = (int16_t)(uint8_t)(m->t >> 8);            /* 0..255 */
        t += t << 1;
        /* 12 pixels, 24 lines a cell */
        if (s == 1) *x += t >> 6; else if (s == 3) *x -= t >> 6;
        else if (s == 2) *y += t >> 5; else *y -= t >> 5;
    }
    return 1;
}

/* rebuild the hidden map round the chomper's cell, then show it */
static void rebuild(void)
{
    uint16_t c = g.player.from;
    uint8_t b = shown ^ 1, *map = b ? MAP1 : MAP0;
    /* the orientation travels with the chomper (the original's camera up) */
    if (view_rot_cell != NONE && view_rot_cell != c) {
        uint8_t k;
        for (k = 0; k < 4; ++k)
            if (NB(view_rot_cell, k) == c && gpos[shown][c] != 0xFF) {
                view_rot = (uint8_t)(grot[shown][c] & 3);
                break;
            }
    }
    view_rot_cell = c;
    uw_center = c;
    uw_rot = (uint8_t)(view_rot | (FLIPPED(c) ? 4 : 0));
    uw_map = map;
    uw_gpos = gpos[b];
    uw_grot = grot[b];
    unwrap_asm();
    shown_center = c;
    dl_pending = (uint8_t)(b + 1);
}

/* a crumb or key eaten: its middle in both maps */
static const uint16_t row_off[9] = { 0, 3 * MAPW, 6 * MAPW, 9 * MAPW, 12 * MAPW, 15 * MAPW, 18 * MAPW, 21 * MAPW, 24 * MAPW };
void view_cell_changed(uint16_t c)
{
    /* port: not (b ? MAP1 : MAP0)[i]: cc65 compiled that as b + i (it wrote
       into the stack page) */
    static uint8_t * const maps[2] = { MAP0, MAP1 };
    uint8_t b, p, v;
    v = g.crumb[c] ? 1 : g.key[c] ? 2 : c == mz_nest ? 3 : 0;
    content[c] = v;
    v += 24;
    for (b = 0; b < 2; ++b) {
        p = gpos_of[b][c];
        if (p == 0xFF) continue;
        maps[b][row_off[p >> 4] + (uint8_t)((p & 15) * 3) + MAPW + 1] = v;
    }
}

/* each frame */
static uint8_t old_y[5];
static void put(uint8_t n, int16_t x, int16_t y, const uint8_t *shape)
{
    uint8_t *p = n < 4 ? pmg_player_mem(n) : pmg_missile_mem();
    memset(p + old_y[n], 0, 16);
    if (!shape || y < TOP - 4 || y > TOP + VIS_ROWS * 8 - 12 || x < PMG_X0 - 4 || x > PMG_X0 + 156) {
        old_y[n] = 0;
        if (n < 4) pmg_x(n, 0);
        else { pmg_missile_x(0, 0); pmg_missile_x(1, 0); pmg_missile_x(2, 0); pmg_missile_x(3, 0); }
        return;
    }
    memcpy(p + (uint8_t)y, shape, 16);
    old_y[n] = (uint8_t)y;
    if (n < 4) pmg_x(n, (uint8_t)x);
    else {
        pmg_missile_x(0, (uint8_t)x);
        pmg_missile_x(1, (uint8_t)(x + 2));
        pmg_missile_x(2, (uint8_t)(x + 4));
        pmg_missile_x(3, (uint8_t)(x + 6));
    }
}

static const uint8_t ghost_col[4] = { 0x58, 0x36, 0x9A, 0xC8 };   /* GLOOM, SPARK, MIST, MOSS */

void view_frame(void)
{
    int16_t px, py, wx, wy, x, y;
    uint8_t i, b;
    if (g.arrived || shown_center == NONE) {
        g.arrived = 0;
        if (g.player.from != shown_center) rebuild();
    }
    if (dl_pending) {                     /* the new map: the VBI shows it from the next frame */
        b = (uint8_t)(dl_pending - 1);
        dl_pending = 0;
        shown = b;
        SDLSTL = (uint16_t)dl_of(b);
    }
    b = shown;
    if (!mover_xy(&g.player, b, &px, &py)) { px = CX * CELL_W + CELL_W / 2; py = CY * CELL_H + CELL_H / 2; }
    wx = px - 80;
    wy = py - 88;
    set_window(b, wx, wy);
    /* the chomper: facing its way (screen), mouth by its steps */
    {
        uint8_t f = view_screen_dir(g.player.from, g.player.moving ? g.player.exitk : g.player.head);
        uint8_t m = (uint8_t)((g.mouth >> 2) % 4);
        if (m == 3) m = 1;
        if (g.state == GS_DYING) m = (uint8_t)(g.timer > 60 ? 2 : (g.timer >> 4) % 3);
        put(4, PMG_X0 + px - wx - 4, TOP + py - wy - 8, spr_chomp[f][m]);
    }
    for (i = 0; i < 4; ++i) {
        const Ghost *gh = &g.ghost[i];
        const uint8_t *shape;
        uint8_t look;
        if (!mover_xy(&gh->mv, b, &x, &y)) { put(i, 0, 0, 0); continue; }
        look = view_screen_dir(gh->mv.from, gh->mv.moving ? gh->mv.exitk : gh->mv.head);
        if (gh->mode == GM_EATEN) { shape = spr_eyes[look]; pmg_color(i, 0x0E); }
        else if (gh->fright) {
            shape = spr_scared;
            pmg_color(i, g.power_left < 50 && (g.clock & 4) ? 0x0E : 0x84);
        } else { shape = spr_ghost[look]; pmg_color(i, ghost_col[i]); }
        if (gh->mode == GM_NEST) y += (int16_t)(((g.clock >> 2) & 4) ? 1 : -1);
        put(i, PMG_X0 + x - wx - 4, TOP + y - wy - 8, shape);
    }
}

/* the HUD row: ASCII to screen codes */
void view_text(uint8_t x, const char *s)
{
    uint8_t *p = HUD + x;
    while (*s) {
        uint8_t c = (uint8_t)*s++;
        *p++ = (uint8_t)(c < 0x20 ? c + 0x40 : c < 0x60 ? c - 0x20 : c);
    }
}

void view_clear_text(void) { memset(HUD, 0, 40); }

