/*
 * Warden Keep: the screen (the original's render.cpp).
 *
 * The room: ANTIC E, 160 x 140 (the original's 320 x 240 at x / 2,
 * y * 3 / 4: a tile 16 x 12, a cube 12 lines high), 4 colours: black (the
 * outlines) and the room's three shades (the original's yellow ramp; each
 * room has its own hue). The floor and the north and west walls are drawn
 * once into the screen and copied to `bg`; the barrels, jewels and stone
 * blocks are drawn over them in the original's painter's order (wx + wy).
 * When one moves only its old and new places are redrawn: the background
 * there, then every object that overlaps them (and what overlaps those),
 * back to front.
 *
 * The warden is players 0-3 (its red left face, yellow right face, the
 * white top's halves) so stepping never redraws the room; anything in
 * front of it (by the painter's order) masks it out of the players line by
 * line, so a barrel in front hides it as in the original. The door's blue
 * is missiles 0-3 (as a fifth player, COLOR3). The HUD: two ANTIC 6 rows
 * under a DLI (score, room, jewels, the clock, red in its last 10 seconds).
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "game.h"
#include "gfx.h"
#include "view.h"

#pragma static-locals (on)

#define FIELD    ((uint8_t *)0xA010)        /* LINES lines x 40 */
#define LINES    140
#define LINE0    46                         /* the line of tile 0,0's centre */
#define HUD      ((uint8_t *)0xBC40)        /* ANTIC 6: 2 x 20 */
#define DL_GAME  ((uint8_t *)0xBD00)
#define DL_TEXT  ((uint8_t *)0xBE00)
#define TEXT     ((uint8_t *)0xA010)        /* the text screens' rows (20 bytes each) */
#define PM(n)    ((uint8_t *)(0x9C00 + ((n) << 8)))
#define MISSILES ((uint8_t *)0x9B00)
#define PM_TOP   40                         /* the PM row of the field's line 0 */

extern uint8_t *blit_dst;
extern const uint8_t *blit_src;
extern uint8_t blit_h;
void blit(void);
extern uint16_t restore_off;                /* copy 4 bytes x restore_h lines from bg to the screen */
extern uint8_t restore_h;
void restore(void);
void irq_install(void);
extern uint8_t dli_clock;

uint8_t bg[LINES * 40];              /* the floor and walls, without the objects */

/* the rooms' shades: dark, mid, bright (PF0, PF1, PF2) */
static const uint8_t room_col[NUM_ROOMS][3] = {
    {0x12, 0x18, 0x1E},                     /* the original's golds */
    {0x22, 0x26, 0x2C},                     /* the store: wood browns */
    {0xA2, 0xA6, 0xAC},                     /* the crypt: cold green */
    {0x62, 0x66, 0x6C},                     /* the vault: purple */
};

/* ---- shapes (tables, not multiplies: cc65 calls a routine for those) */
static const uint8_t sum_line[15] = {       /* wx + wy -> the tile centre's line */
    LINE0, LINE0 + 6, LINE0 + 12, LINE0 + 18, LINE0 + 24, LINE0 + 30, LINE0 + 36, LINE0 + 42,
    LINE0 + 48, LINE0 + 54, LINE0 + 60, LINE0 + 66, LINE0 + 72, LINE0 + 78, LINE0 + 84};
static const uint8_t dif_col[15] = {        /* wx - wy + 7 -> the shape's first byte */
    4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32};
#define CL(x, y) sum_line[(uint8_t)((x) + (y))]
#define C0(x, y) dif_col[(uint8_t)((x) - (y) + 7)]

static uint16_t line_addr[LINES];
static int8_t sh_top[SH_COUNT];
static uint8_t sh_h[SH_COUNT];
static const uint8_t *sh_dm[SH_COUNT], *sh_m1[SH_COUNT];

static void draw(uint8_t sh, uint8_t x, uint8_t y)
{
    blit_dst = FIELD + line_addr[(uint8_t)(CL(x, y) + sh_top[sh])] + C0(x, y);
    blit_src = sh_dm[sh];
    blit_h = sh_h[sh];
    blit();
}

static uint8_t obj_shape(uint8_t i)
{
    switch (obj_kind[i]) {
    case O_BLOCK: return SH_BLOCK;
    case O_BARREL: return SH_BARREL;
    }
    return SH_JEWEL;
}

/* each object's place, cached (obj_cache) when it changes: its shape,
   wx + wy, wx - wy + 7, its first byte and lines l0..l1-1 */
static uint8_t ob_sh[MAX_OBJ], ob_sum[MAX_OBJ], ob_dif[MAX_OBJ], ob_c0[MAX_OBJ], ob_l0[MAX_OBJ], ob_l1[MAX_OBJ];

static void obj_cache(uint8_t i)
{
    static uint8_t sh;
    ob_sh[i] = sh = obj_shape(i);
    ob_sum[i] = (uint8_t)(obj_x[i] + obj_y[i]);
    ob_dif[i] = (uint8_t)(obj_x[i] - obj_y[i] + 7);
    ob_c0[i] = dif_col[ob_dif[i]];
    ob_l0[i] = (uint8_t)(sum_line[ob_sum[i]] + sh_top[sh]);
    ob_l1[i] = (uint8_t)(ob_l0[i] + sh_h[sh]);
}

/* the area to redraw: bytes u_c0..u_c1-1, lines u_l0..u_l1-1 (a union:
   it grows by each object redrawn, so what's in front of that is too) */
static uint8_t u_c0, u_c1, u_l0, u_l1;

static void add_rect(uint8_t sh, uint8_t x, uint8_t y)
{
    static uint8_t c, l;
    c = C0(x, y);
    l = (uint8_t)(CL(x, y) + sh_top[sh]);
    restore_off = line_addr[l] + c;         /* the background there */
    restore_h = sh_h[sh];
    restore();
    if (c < u_c0) u_c0 = c;
    if ((uint8_t)(c + 4) > u_c1) u_c1 = (uint8_t)(c + 4);
    if (l < u_l0) u_l0 = l;
    l += sh_h[sh];
    if (l > u_l1) u_l1 = l;
}

/* the objects in the painter's order: by wx + wy, then object_at's order */
static uint8_t order[MAX_OBJ], n_order;

static void sort_objects(void)
{
    static uint8_t i, j, k;
    n_order = 0;
    for (i = 0; i < num_obj; ++i) {
        if (!obj_kind[i]) continue;
        k = ob_sum[i];
        for (j = n_order; j && ob_sum[order[j - 1]] > k; --j) order[j] = order[j - 1];
        order[j] = i;
        ++n_order;
    }
}

/* after add_rect: each object that overlaps the area, back to front */
static void redraw(void)
{
    static uint8_t i, o;
    sort_objects();
    for (i = 0; i < n_order; ++i) {
        o = order[i];
        if (ob_l0[o] >= u_l1 || ob_l1[o] <= u_l0 || ob_c0[o] >= u_c1 || (uint8_t)(ob_c0[o] + 4) <= u_c0)
            continue;
        blit_dst = FIELD + line_addr[ob_l0[o]] + ob_c0[o];
        blit_src = sh_dm[ob_sh[o]];
        blit_h = sh_h[ob_sh[o]];
        blit();
        if (ob_c0[o] < u_c0) u_c0 = ob_c0[o];
        if ((uint8_t)(ob_c0[o] + 4) > u_c1) u_c1 = (uint8_t)(ob_c0[o] + 4);
        if (ob_l0[o] < u_l0) u_l0 = ob_l0[o];
        if (ob_l1[o] > u_l1) u_l1 = ob_l1[o];
    }
}

/* ---- the warden (players 0-3), masked by what's in front of it */
static uint8_t pl_buf[4][PL_H], pl_row, pl_on;
static uint8_t door_covered, door_check;
static int8_t last_px, last_py;
static uint8_t last_facing;

static void warden(void)
{
    static uint8_t i, o, l, l0, l1, sl, top, d, half, m, x, *p0, *p2, psum, pdif, dc;
    static const uint8_t *m1;
    memcpy(pl_buf, player_pm[gs.facing], sizeof pl_buf);
    top = (uint8_t)(CL(gs.px, gs.py) + PL_TOP);     /* the players' first line */
    psum = (uint8_t)(gs.px + gs.py);
    pdif = (uint8_t)(gs.px - gs.py + 6);     /* (+ 7 - 1: dc + 1 below) */
    for (i = 0; i < num_obj; ++i) {
        if (!obj_kind[i] || ob_sum[i] <= psum) continue;   /* gone, behind */
        dc = (uint8_t)(ob_dif[i] - pdif);       /* 0..2: 1 column left .. right */
        if (dc > 2) continue;
        sl = ob_l0[i];
        /* its lines over the warden's: l0..l1-1 (sl > top: it's in front) */
        if (sl >= top + PL_H) continue;
        l0 = (uint8_t)(sl - top);
        l1 = (uint8_t)(ob_l1[i] - top);
        if (l1 > PL_H) l1 = PL_H;
        o = ob_sh[i];
        for (half = 0; half < 2; ++half) {
            /* the object's 8 pixels over this half of the warden */
            d = (uint8_t)(half + 1 - dc);       /* 0: its left half, 1: its right */
            if (d > 1) continue;
            m1 = sh_m1[o] + d;
            p0 = pl_buf[half];
            p2 = pl_buf[half + 2];
            for (l = l0; l < l1; ++l) {
                m = (uint8_t)~*m1;
                m1 += 2;
                p0[l] &= m;
                p2[l] &= m;
            }
        }
    }
    if (pl_on) for (i = 0; i < 4; ++i) memset(PM(i) + pl_row, 0, PL_H);
    pl_row = (uint8_t)(top + PM_TOP);
    for (i = 0; i < 4; ++i) memcpy(PM(i) + pl_row, pl_buf[i], PL_H);
    pl_on = 1;
    x = (uint8_t)(120 + (gs.px - gs.py) * 8);
    (&HPOSP0)[0] = (&HPOSP0)[2] = x;
    (&HPOSP0)[1] = (&HPOSP0)[3] = (uint8_t)(x + 8);
    /* the door's blue, unless a barrel stands on it */
    if (!door_check) return;
    door_check = 0;
    o = 0;
    for (i = 0; i < num_obj; ++i)
        if (obj_kind[i] == O_BARREL && obj_x[i] == 7 && obj_y[i] == 7) o = 1;
    if (o != door_covered) {
        door_covered = o;
        if (o) memset(MISSILES + PM_TOP + CL(7, 7) - 6, 0, 13);
        else memcpy(MISSILES + PM_TOP + CL(7, 7) - 6, door_missiles, 13);
    }
}

/* ---- the HUD */
void fmt_score(uint16_t v, char *b)        /* (subtraction: no 32-bit division) */
{
    static const uint16_t p10[5] = {10000, 1000, 100, 10, 1};
    static uint8_t i;
    static char d;
    *b++ = '0';
    for (i = 0; i < 5; ++i) {
        d = '0';
        while (v >= p10[i]) { v -= p10[i]; ++d; }
        *b++ = d;
    }
    *b = 0;
}

static uint16_t hud_score;
static uint8_t hud_secs, hud_jewels, hud_msg_on;

static uint8_t scode6(char c, uint8_t colour)
{
    uint8_t a = (uint8_t)c;
    if (a >= 0x20 && a < 0x60) a -= 0x20;
    return (uint8_t)((a & 0x3F) | colour);
}

static void hud_put(uint8_t row, uint8_t col, const char *s, uint8_t colour)
{
    uint8_t *d = HUD + row * 20 + col;
    while (*s) *d++ = scode6(*s++, colour);
}

#define C_WHITE 0x00                        /* PF0 under the DLI */
#define C_RED   0x40                        /* PF1: the clock (white, red at the end) */
#define C_GOLD  0x80                        /* PF2 */

void view_hud_msg(const char *s)
{
    memset(HUD + 20, 0, 20);
    hud_msg_on = s != 0;
    if (s) hud_put(1, (uint8_t)((20 - strlen(s)) >> 1), s, C_GOLD);
    else {
        hud_put(1, 0, "JEWELS", C_GOLD);
        hud_put(1, 12, gs.relaxed ? "RELAXED" : "TIME", C_GOLD);
    }
    hud_jewels = 0xFF;
    hud_secs = 0xFF;
}

static void hud(void)
{
    char b[12];
    if (gs.score != hud_score) {
        hud_score = gs.score;
        fmt_score(gs.score, b);
        hud_put(0, 6, b, C_WHITE);
    }
    if (hud_msg_on) return;
    if (gs.jewels_left != hud_jewels) {
        hud_jewels = gs.jewels_left;
        b[0] = (char)('0' + gs.jewels_total - gs.jewels_left);
        b[1] = '/';
        b[2] = (char)('0' + gs.jewels_total);
        b[3] = 0;
        hud_put(1, 7, b, C_WHITE);
    }
    if (gs.seconds != hud_secs && !gs.relaxed) {
        hud_secs = gs.seconds;
        b[0] = (char)('0' + gs.seconds / 10);
        b[1] = (char)('0' + gs.seconds % 10);
        b[2] = 0;
        hud_put(1, 17, b, C_RED);
        dli_clock = gs.seconds <= 10 ? 0x36 : 0x0E;
    }
}

static void hud_labels(void)
{
    char b[8];
    memset(HUD, 0, 40);
    hud_put(0, 0, "SCORE", C_GOLD);
    memcpy(b, "ROOM 1", 7);
    b[5] = (char)('1' + gs.room);
    hud_put(0, 14, b, C_GOLD);
    view_hud_msg(0);
    hud_score = 0xFFFF;
    hud_jewels = hud_secs = 0xFF;
    dli_clock = 0x0E;
}

/* ---- the room */
void view_room(void)
{
    uint8_t x, y, s;
    SDMCTL = 0;
    GRACTL = 0;                             /* (no stripes while the DMA is off) */
    for (x = 0; x < 5; ++x) (&GRAFP0)[x] = 0;
    memset(FIELD, 0, LINES * 40);
    /* the floor, back to front (the door at the front corner), then the
       north and west walls */
    for (s = 0; s < 15; ++s)
        for (x = 0; x < 8; ++x) {
            y = (uint8_t)(s - x);
            if (y < 8) draw(x == 7 && y == 7 ? SH_DOOR : SH_FLOOR, x, y);
        }
    for (s = 0; s < 8; ++s) {
        draw(SH_WALL, s, 0);
        if (s) draw(SH_WALL, 0, s);
    }
    memcpy(bg, FIELD, sizeof bg);
    for (s = 0; s < num_obj; ++s) obj_cache(s);
    sort_objects();
    for (s = 0; s < n_order; ++s) draw(ob_sh[order[s]], obj_x[order[s]], obj_y[order[s]]);
    COLOR0 = room_col[gs.room][0];
    COLOR1 = room_col[gs.room][1];
    COLOR2 = room_col[gs.room][2];
    door_covered = 0xFF;
    door_check = 1;
    for (x = 0; x < 4; ++x) (&HPOSM0)[x] = (uint8_t)(120 + (x << 2));   /* the door (7,7) */
    hud_labels();
    warden();
    last_px = gs.px;
    last_py = gs.py;
    last_facing = gs.facing;
    hud();
    frame_wait();
    SDMCTL = 0x3E;                          /* + PMG, single line */
    GRACTL = 3;
}

void view_frame(void)
{
    uint8_t w = 0;
    if (gs.moved_obj != 0xFF || gs.taken_obj != 0xFF) {
        u_c0 = u_l0 = 0xFF;
        u_c1 = u_l1 = 0;
        if (gs.moved_obj != 0xFF) {
            add_rect(SH_BARREL, gs.moved_ox, gs.moved_oy);
            obj_cache(gs.moved_obj);
            add_rect(SH_BARREL, obj_x[gs.moved_obj], obj_y[gs.moved_obj]);
            gs.moved_obj = 0xFF;
            door_check = 1;
        }
        if (gs.taken_obj != 0xFF) {
            add_rect(SH_JEWEL, gs.taken_x, gs.taken_y);
            gs.taken_obj = 0xFF;
        }
        redraw();
        w = 1;
    }
    if (w || gs.px != last_px || gs.py != last_py || gs.facing != last_facing) {
        last_px = gs.px;
        last_py = gs.py;
        last_facing = gs.facing;
        warden();
    }
    hud();
}

/* ---- the display lists */
static uint8_t *lms(uint8_t *d, uint8_t op, const void *a)
{
    *d++ = op;
    *d++ = (uint8_t)(uint16_t)a;
    *d++ = (uint8_t)((uint16_t)a >> 8);
    return d;
}

static void build_game_dl(void)
{
    uint8_t *d = DL_GAME, l;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    for (l = 0; l < LINES; ++l) {
        uint8_t op = 0x0E;
        if (l == 0 || l == 102) op = 0x4E;  /* (4K: $B000 at line 102) */
        if (l == LINES - 1) op |= 0x80;     /* the HUD's colours */
        if (op & 0x40) d = lms(d, op, FIELD + l * 40);
        else *d++ = op;
    }
    *d++ = 0x00;
    d = lms(d, 0x46, HUD);
    *d++ = 0x06;
    d = lms(d, 0x41, DL_GAME);
}

void view_game_screen(void)
{
    SDMCTL = 0;
    frame_wait();
    build_game_dl();
    memset(MISSILES, 0, 256);
    SDLSTL = (uint16_t)DL_GAME;
    COLOR3 = 0x86;                          /* the door's blue (the missiles) */
    COLOR4 = 0;
}

/* ---- text screens (the title, the end): ANTIC 6 / 7 rows in TEXT */
#define TROWS 22
static uint8_t trow_mode[TROWS];

void view_text(uint8_t row, const char *a)
{
    uint8_t *d = TEXT + row * 20;
    memset(d, 0, 20);
    if (a) {
        uint8_t n = (uint8_t)strlen(a);
        d += (20 - n) >> 1;
        while (*a) {
            char c = *a++;
            uint8_t col = 0;
            /* lower case letters: PF1 (the other colour) */
            if (c >= 'a' && c <= 'z') { c = (char)(c - 32); col = 0x40; }
            else if (c == '~') { c = ' '; }
            *d++ = (uint8_t)(((uint8_t)c - 0x20) & 0x3F) | col;
        }
    }
}

static void build_text_dl(void)
{
    uint8_t *d = DL_TEXT, i;
    *d++ = 0x70;
    *d++ = 0x70;
    *d++ = 0x70;
    for (i = 0; i < TROWS; ++i) d = lms(d, trow_mode[i], TEXT + i * 20);
    d = lms(d, 0x41, DL_TEXT);
}

void view_text_screen(uint8_t bar)
{
    uint8_t i;
    SDMCTL = 0;
    frame_wait();
    for (i = 0; i < 4; ++i) {
        (&HPOSP0)[i] = 0;
        (&HPOSM0)[i] = 0;
    }
    pl_on = 0;
    memset((void *)0x9B00, 0, 0x500);
    memset(TEXT, 0, TROWS * 20);
    for (i = 0; i < TROWS; ++i) trow_mode[i] = 0x46;
    trow_mode[3] = 0x47;                    /* the name: double height */
    build_text_dl();
    COLOR0 = bar;                           /* capitals: the bar's colour */
    COLOR1 = 0x0C;                          /* lower case: white */
    COLOR2 = 0x86;
    COLOR3 = 0x1C;
    COLOR4 = 0;
    SDLSTL = (uint16_t)DL_TEXT;
    SDMCTL = 0x22;
}

void view_init(void)
{
    uint8_t l;
    SDMCTL = 0;                             /* off a frame: the OS screen is in our way */
    frame_wait();
    for (l = 0; l < LINES; ++l) line_addr[l] = (uint16_t)l * 40;
    for (l = 0; l < SH_COUNT; ++l) {
        sh_top[l] = shapes[l].top;
        sh_h[l] = shapes[l].h;
        sh_dm[l] = shapes[l].dm;
        sh_m1[l] = shapes[l].m1;
    }
    memset((void *)0xA000, 0, 0x2000);
    memset((void *)0x9B00, 0, 0x500);
    PMBASE = 0x98;
    GRACTL = 3;
    GPRIOR = 0x11;                          /* players over the room; missiles = a fifth player */
    (&SIZEP0)[0] = (&SIZEP0)[1] = (&SIZEP0)[2] = (&SIZEP0)[3] = 0;
    SIZEM = 0x55;                           /* double: 4 pixels a missile */
    PCOLR0 = 0x34;                          /* the left face: red */
    (&PCOLR0)[1] = 0x1C;                    /* the right: yellow */
    (&PCOLR0)[2] = (&PCOLR0)[3] = 0x0E;     /* the top: white */
    CHBAS = 0xE0;
    irq_install();
}

