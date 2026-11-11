/*
 * Bullion Dash - the Atari screen.
 *
 * Scan lines: 16-23 the status line (ANTIC 6), 32-159 the grid (16 ANTIC 4
 * rows, one character a tile, columns 6..33), 168-183 two message lines
 * (ANTIC 6). One character set: the ROM font's codes 0..63 for the text,
 * the tiles from 64 (tools/mkart.py). Colours: brick COLPF0, solid COLPF1,
 * ladders and bars COLPF2, gold COLPF3 (inverse characters).
 *
 * A tile change writes one byte. The runner is player 0, the guards
 * players 1-3, missiles 2+3 side by side (the fourth guard: their colours
 * are players 2 and 3's) and missile 1 at double width (the fifth, which
 * few levels have).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "game.h"
#include "coords.h"
#include "art.h"
#include "render.h"

/* bullion.cfg: the character set under the players ($9800, codes 0-83
   only: $9B00 on is the missiles), the screen's memory at $9000 (SCRBSS:
   the display lists first, so they don't cross a 1K boundary) */
#define CHARSET  ((uint8_t *)0x9800)
#pragma bss-name (push, "SCRBSS")
static uint8_t dl_game[32], dl_title[32], status_mem[20], msg_mem[40], grid_mem[16 * 40], title_mem[21 * 20];
#pragma bss-name (pop)
#define GRID     grid_mem
#define STATUS   status_mem
#define MSG      msg_mem
#define DL_GAME  dl_game
#define DL_TITLE dl_title
#define TITLE    title_mem
#define GRID_COL 6
#define SCAN_GRID 32
#define PL(n)    ((uint8_t *)(0x9C00 + ((n) << 8)))
#define MISSILE  ((uint8_t *)0x9B00)

#define C_RUNNER A8_COLOR(12, 12)
#define C_GUARD  A8_COLOR(4, 8)
#define C_LOOT   A8_COLOR(1, 14)

uint8_t render_editor_look;
static uint8_t *row_addr[GRID_ROWS];
static uint8_t frames_r[10 * 8], frames_l[10 * 8];   /* right / left facing */

static uint8_t scode(char c)
{
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return c;
}

/* ------------------------------------------------------------ tiles */

static uint8_t tile_code(uint8_t t)
{
    uint8_t c = (render_editor_look ? CH_EDIT : CH_TILE) + t;
    if (t == TILE_EMPTY || (t == TILE_HIDDEN_LADDER && !render_editor_look)) return 0;
    if (t == TILE_GOLD) c |= 0x80;          /* COLPF3 */
    return c;
}

void render_tile_changed(uint8_t x, uint8_t y)
{
    row_addr[y][x] = tile_code(gs.tiles[y][x]);
}

void render_room(uint8_t editor)
{
    static uint8_t x, y, codes[NUM_TILES], *d;
    static const uint8_t *t;
    render_editor_look = editor;
    for (x = 0; x < NUM_TILES; ++x) codes[x] = tile_code(x);
    t = &gs.tiles[0][0];
    for (y = 0; y < GRID_ROWS; ++y) {
        d = row_addr[y];
        for (x = 0; x < GRID_COLS; ++x) d[x] = codes[*t++];
    }
    render_status(1);
}

/* ------------------------------------------------------------ sprites */

static uint8_t pl_y[4], pl_on[4];             /* drawn: players */

static uint8_t player_frame(const Player *p)
{
    uint8_t alt = (p->anim >> 2) & 1;
    switch (p->state) {
    case PS_RUNNING:  return alt ? FR_RUN2 : FR_RUN1;
    case PS_CLIMBING: return alt ? FR_CLIMB2 : FR_CLIMB1;
    case PS_HANGING:  return FR_HANG;
    case PS_DIGGING:  return FR_DIG;
    case PS_FALLING:  return FR_FALL;
    }
    return FR_STAND;
}

static uint8_t enemy_frame(const Enemy *e)
{
    uint8_t alt = (e->anim >> 2) & 1;
    switch (e->state) {
    case ES_TRAPPED:  return FR_TRAPPED;
    case ES_RUNNING:  return alt ? FR_RUN2 : FR_RUN1;
    case ES_CLIMBING: return alt ? FR_CLIMB2 : FR_CLIMB1;
    case ES_HANGING:  return FR_HANG;
    case ES_FALLING:  return FR_FALL;
    }
    return FR_STAND;
}

#define HPOS(px)  ((uint8_t)(PMG_X0 + GRID_COL * 4 + (ROOM_X(px) >> 1)))
#define SCAN(py)  ((uint8_t)(SCAN_GRID + ROOM_Y(py)))

static const uint8_t *pl_gfx[4];        /* the picture drawn */

/* port (speed): a player is only redrawn when its line or picture changed */
static void player_put(uint8_t n, uint8_t on, int16_t px, int16_t py, uint8_t frame, uint8_t left)
{
    static uint8_t y;
    static const uint8_t *g;
    if (!on) {
        if (pl_on[n]) memset(PL(n) + pl_y[n], 0, 8);
        pl_on[n] = 0;
        return;
    }
    y = SCAN(py);
    g = (left ? frames_l : frames_r) + frame * 8;
    pmg_x(n, HPOS(px));
    if (pl_on[n] && y == pl_y[n] && g == pl_gfx[n]) return;
    if (pl_on[n]) memset(PL(n) + pl_y[n], 0, 8);
    pl_on[n] = 1;
    pl_y[n] = y;
    pl_gfx[n] = g;
    memcpy(PL(n) + y, g, 8);
}

/* missile guards: 3 = missiles 2+3 (two bits each: the frame's high and
   low pair of pixels), 4 = missile 1 at double width (the frame's two
   halves as two wide pixels) */
static uint8_t mg_y[2], mg_on[2];

static void missile_put(uint8_t k, uint8_t on, int16_t px, int16_t py, uint8_t frame, uint8_t left)
{
    uint8_t i, y, b, *m;
    const uint8_t *f;
    uint8_t keep = k == 0 ? 0x0F : 0xF3;
    if (mg_on[k]) {
        m = MISSILE + mg_y[k];
        for (i = 0; i < 8; ++i) m[i] &= keep;
    }
    mg_on[k] = on;
    if (!on) return;
    y = SCAN(py);
    mg_y[k] = y;
    f = (left ? frames_l : frames_r) + frame * 8;
    m = MISSILE + y;
    if (k == 0) {
        pmg_missile_x(2, HPOS(px));
        pmg_missile_x(3, HPOS(px) + 2);
        for (i = 0; i < 8; ++i) {
            b = f[i];                       /* pixels 0-1 -> missile 2, 2-3 -> missile 3 */
            m[i] = (m[i] & keep) | ((b & 0xC0) >> 2) | ((b & 0x30) << 2);
        }
    } else {
        pmg_missile_x(1, HPOS(px));
        for (i = 0; i < 8; ++i) {
            b = f[i];
            m[i] = (m[i] & keep) | ((b & 0xC0) ? 0x08 : 0) | ((b & 0x30) ? 0x04 : 0);
        }
    }
}

void render_frame(uint8_t entities)
{
    static uint8_t i;
    static const Enemy *e;
    if (entities) {
        const Player *p = &gs.player;
        player_put(0, 1, p->px, p->py, player_frame(p), p->dir == DIR_LEFT);
    } else {
        player_put(0, 0, 0, 0, 0, 0);
    }
    e = gs.enemies;
    for (i = 0; i < MAX_ENEMIES; ++i, ++e) {
        uint8_t on = entities && i < gs.num_enemies && e->active && e->state != ES_DEAD;
        uint8_t f = on ? enemy_frame(e) : 0, left = e->dir == DIR_LEFT;
        if (i < 3) {
            if (on) pmg_color(1 + i, e->has_gold ? C_LOOT : C_GUARD);
            player_put(1 + i, on, e->px, e->py, f, left);
        } else {
            missile_put(i - 3, on, e->px, e->py, f, left);
        }
    }
}

void render_cursor(uint8_t x, uint8_t y, uint8_t on)
{
    player_put(0, on, x * TILE_W, y * TILE_H, FR_CURSOR, 0);
}

/* ------------------------------------------------------------ text */

static void put(uint8_t *d, const char *s, uint8_t colour)
{
    while (*s) *d++ = scode(*s++) | colour;
}

static void num(uint8_t *d, uint32_t v, uint8_t digits, uint8_t colour)
{
    static char buf[8];
    uint8_t i = 7;
    buf[7] = 0;
    do { buf[--i] = '0' + (char)(v % 10); v /= 10; } while (v && i);
    while (7 - i < digits) buf[--i] = '0';
    put(d, buf + 7 - digits, colour);
}

static uint32_t shown_score;
static int16_t shown_level, shown_lives, shown_gold, shown_total;

void render_status(uint8_t force)
{
    if (force) {
        memset(STATUS, 0, 20);
        shown_score = 0xFFFFFFFFUL;
        shown_level = shown_lives = shown_gold = shown_total = -1;
    }
    if (gs.score != shown_score) {
        num(STATUS, gs.score, 6, 0xC0);
        shown_score = gs.score;
    }
    if (gs.level_num != shown_level) {
        put(STATUS + 7, "L", 0x40);
        num(STATUS + 8, gs.level_num, 3, 0x40);
        shown_level = gs.level_num;
    }
    if (gs.gold_collected != shown_gold || gs.gold_total != shown_total) {
        put(STATUS + 12, "$", 0xC0);
        num(STATUS + 13, gs.gold_collected, 2, 0xC0);
        put(STATUS + 15, "/", 0xC0);
        num(STATUS + 16, gs.gold_total, 2, 0xC0);
        shown_gold = gs.gold_collected;
        shown_total = gs.gold_total;
    }
    if (gs.lives != shown_lives) {
        num(STATUS + 19, gs.lives > 0 ? gs.lives : 0, 1, 0x00);
        shown_lives = gs.lives;
    }
}

void render_msg(const char *l1, const char *l2)
{
    memset(MSG, 0, 40);
    if (l1) put(MSG + ((20 - strlen(l1)) >> 1), l1, 0xC0);
    if (l2) put(MSG + 20 + ((20 - strlen(l2)) >> 1), l2, 0x80);
}

void render_msg_text(uint8_t line, uint8_t col, const char *s)
{
    put(MSG + line * 20 + col, s, line ? 0x80 : 0xC0);
}

void render_msg_num(uint8_t line, uint8_t col, uint32_t v, uint8_t digits)
{
    num(MSG + line * 20 + col, v, digits, line ? 0x80 : 0xC0);
}

/* ------------------------------------------------------------ setup */

static void show(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x1C;
}

static void game_colours(void)
{
    COLOR4 = 0;
    COLOR0 = A8_COLOR(3, 6);                /* brick */
    COLOR1 = A8_COLOR(0, 8);                /* solid; status: grey */
    COLOR2 = A8_COLOR(10, 10);              /* ladders, bars */
    COLOR3 = A8_COLOR(1, 14);               /* gold; text */
    CHBAS = 0x98;
}

void render_init(void)
{
    uint8_t i, *p, b, k;
    memcpy(CHARSET, (void *)0xE000, 512);
    memcpy(CHARSET + 512, tile_chars, sizeof(tile_chars));
    memset(dl_game, 0, (uint16_t)(title_mem + sizeof(title_mem) - dl_game));
    for (i = 0; i < GRID_ROWS; ++i) row_addr[i] = GRID + i * 40 + GRID_COL;
    /* the frames, and mirrored (4 pixels in the high nibble) */
    memcpy(frames_r, frame_gfx, sizeof(frames_r));
    for (i = 0; i < sizeof(frames_r); ++i) {
        b = frames_r[i];
        k = 0;
        if (b & 0x80) k |= 0x10;
        if (b & 0x40) k |= 0x20;
        if (b & 0x20) k |= 0x40;
        if (b & 0x10) k |= 0x80;
        frames_l[i] = k;
    }
    p = DL_GAME;
    *p++ = 0x70; *p++ = 0x70;                               /* 8-23 */
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)STATUS; *p++ = (uint16_t)STATUS >> 8;   /* 24-31 */
    *p++ = 0x00;                                            /* 32 */
    *p++ = 0x44; *p++ = (uint8_t)(uint16_t)GRID; *p++ = (uint16_t)GRID >> 8;       /* 33-40 */
    for (i = 1; i < GRID_ROWS; ++i) *p++ = 0x04;            /* .. -160 */
    *p++ = 0x70;
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)MSG; *p++ = (uint16_t)MSG >> 8;
    *p++ = 0x06;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_GAME; *p = (uint16_t)DL_GAME >> 8;
    pmg_init();
    GPRIOR = 1;
    SIZEM = 0x04;                           /* missile 1 double width */
}

void render_game_screen(void)
{
    memset((void *)0x9B00, 0, 0x500);
    memset(pl_on, 0, sizeof(pl_on));
    memset(mg_on, 0, sizeof(mg_on));
    pmg_color(0, C_RUNNER);
    pmg_color(1, C_GUARD);
    pmg_color(2, C_GUARD);
    pmg_color(3, C_GUARD);
    game_colours();
    render_msg(0, 0);
    show(DL_GAME);
}

/* ------------------------------------------------------------ title */

void title_clear(void) { memset(TITLE, 0, sizeof(title_mem)); }

/* row 0: ANTIC 7 (the name), 1..: ANTIC 6, 20 columns; colour 0..3 */
void title_print(uint8_t row, uint8_t col, const char *s, uint8_t colour)
{
    put(TITLE + row * 20 + col, s, colour << 6);
}

void render_title(void)
{
    uint8_t *p = DL_TITLE, i;
    memset((void *)0x9B00, 0, 0x500);
    memset(pl_on, 0, sizeof(pl_on));
    memset(mg_on, 0, sizeof(mg_on));
    *p++ = 0x70; *p++ = 0x70;
    *p++ = 0x47; *p++ = (uint8_t)(uint16_t)TITLE; *p++ = (uint16_t)TITLE >> 8;
    *p++ = 0x70;
    for (i = 1; i < 21; ++i) *p++ = 0x06;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_TITLE; *p = (uint16_t)DL_TITLE >> 8;
    game_colours();
    show(DL_TITLE);
}
