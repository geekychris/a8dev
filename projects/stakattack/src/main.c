/*
 * StakAttack (Atari 8-bit). Port of amiga_mcp/examples/stakattack (by way
 * of the ZX Spectrum port): game.c is the original's logic; this is the
 * Atari front end.
 *
 * The well is 12 x 19 ANTIC 2 characters shown in GTIA mode 10, so the
 * seven gems keep seven colours of their own (plus the copper walls and
 * the background: GTIA 10's nine colours, all used). A cell is one
 * character: 4 colour clocks x 8 scan lines, two GTIA pixels across. The
 * text above (and the pause/game-over band, which takes the place of two
 * well rows) is ANTIC 6 in the normal mode; display-list interrupts switch
 * between the two (display.s). Only cells that change are written: the
 * piece and its landing marks move by rewriting at most 16 characters.
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "music.h"

#define CHARSET   ((uint8_t *)0xA000)     /* 0-63 ROM font, 64.. the tiles */
#define WELL      ((uint8_t *)0xA400)     /* 20 rows x 48: well, next piece, floor */
#define HUD       ((uint8_t *)0xA800)     /* 2 x 24 (ANTIC 6) */
#define MSG       ((uint8_t *)0xA830)     /* 2 x 24: pause / game over band */
#define TITLE     ((uint8_t *)0xA900)     /* 24 (ANTIC 7) */
#define GEMROW    ((uint8_t *)0xA918)     /* 48 (ANTIC 2, GTIA) */
#define TTEXT     ((uint8_t *)0xA960)     /* 14 x 24 (ANTIC 6) */
#define DL_GAME   ((uint8_t *)0xAC00)
#define DL_TITLE  ((uint8_t *)0xAD00)
#define DL_BAND   ((uint8_t *)0xAD80)     /* the game with the message band */
#define TROWS     14
#define WELL_COL  18                      /* screen column of the well's cell 0 */
#define NEXT_COL  35
#define MSG_ROW   8                       /* the band covers well rows 8-11 */

/* tiles (character codes). A gem's nibble = its colour register in GTIA 10:
   0 = PCOLR0 (copper: walls), 1..7 = PCOLR1-3, COLPF0-3 (the gems), 8 = COLBK */
#define T_EMPTY   64                      /* T_EMPTY + field value = the cell */
#define T_GHOST   72                      /* + type: landing marks */
#define T_LWALL   79
#define T_RWALL   80
#define T_FLOOR   81
#define T_LFLOOR  82
#define T_RFLOOR  83
#define T_TRAY    84
#define T_FLASH   85

extern void display_install(void);
extern void __fastcall__ dec7(uint32_t v);         /* dec.s: 7 digits into dec_out */
extern char dec_out[7];
extern uint8_t piece_cell_r[], piece_cell_c[];
extern void __fastcall__ well_copy(const uint8_t *field);   /* well.s: the 19 x 12 cells to the screen */     /* game.c: a piece's 4 cells by type * 4 + rot */
extern uint8_t gem_col[9];

static GameState gs;
static uint8_t music_on = 1;

/* the probe a test reads: the state after the last game_update */
struct {
    uint16_t frames, ticks, overruns;
    uint16_t locks, cleared;
    uint16_t late_lock, late_clear, late_other;   /* late frames, by what the frame before did */
} stak_probe;

/* ------------------------------------------------------------ log */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logl(const char *k, uint32_t v)
{
    static uint8_t i;
    a8_log_s(k);
    dec7(v > 9999999UL ? 9999999UL : v);
    for (i = 0; i < 6 && dec_out[i] == '0'; ++i) ;
    for (; i < 7; ++i) a8_log_c(dec_out[i]);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logl(k, v) ((void)0)
#define LOGNL() ((void)0)
#endif

/* ------------------------------------------------------------ text */

static uint8_t scode(char c)
{
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return c;
}

static void put(uint8_t *d, const char *s, uint8_t colour)
{
    while (*s) *d++ = scode(*s++) | colour;
}

static void put_num(uint8_t *d, uint32_t v, uint8_t digits, uint8_t colour)
{
    static uint8_t i;
    dec7(v > 9999999UL ? 9999999UL : v);
    for (i = 7 - digits; i < 7; ++i) *d++ = (dec_out[i] - '0' + 0x10) | colour;
}

/* ------------------------------------------------------------ tiles, colours */

static void tiles_init(void)
{
    static uint8_t k, r, *p, b;
    memcpy(CHARSET, (void *)0xE000, 512);
    p = CHARSET + T_EMPTY * 8;
    memset(p, 0x88, 32 * 8);
    for (k = 1; k <= 7; ++k) {            /* a gem: 7 lines, a gap under it */
        p = CHARSET + (T_EMPTY + k) * 8;
        b = (k << 4) | k;
        for (r = 0; r < 7; ++r) p[r] = b;
        p[0] = (k << 4) | 8;              /* the top right corner cut: cells read as gems */
        p[7] = 0x88;
        p = CHARSET + (T_GHOST + k - 1) * 8;   /* landing mark: a line at the gem's foot */
        p[6] = b;
    }
    p = CHARSET + T_LWALL * 8;  memset(p, 0x80, 8);
    p = CHARSET + T_RWALL * 8;  memset(p, 0x08, 8);
    p = CHARSET + T_FLOOR * 8;  memset(p, 0x00, 3);
    p = CHARSET + T_LFLOOR * 8; memset(p, 0x80, 3);
    p = CHARSET + T_RFLOOR * 8; memset(p, 0x08, 3);
    p = CHARSET + T_TRAY * 8;   p[0] = 0x00;
    p = CHARSET + T_FLASH * 8;  memset(p, 0x00, 7);
}

/* the original's 12-bit colours, as near as the Atari palette goes */
static const uint8_t well_colours[8] = {   /* (the nearest in Atari800's PAL palette) */
    0x1A,                                 /* 0 copper: walls */
    0x4A,                                 /* 1 rose:  I */
    0x9A,                                 /* 2 teal:  O */
    0x1C,                                 /* 3 ochre: T */
    0x6A,                                 /* 4 lavender: S */
    0xBC,                                 /* 5 jade:  Z */
    0x8A,                                 /* 6 sky:   L */
    0x2C,                                 /* 7 coral: J */
};
static const uint8_t well_colours_ntsc[8] = { /* (NTSC: the hues sit a step later) */
    0x3A, 0x5A, 0xBA, 0x2C, 0x7A, 0xCC, 0xAA, 0x4C };
static uint8_t copper, gold;
#define COL_BACK  A8_COLOR(8, 0)          /* 8: the background (COLBK) */

static void colours(void)
{
    static uint8_t i;
    static const uint8_t *c;
    c = frame_is_pal() ? well_colours : well_colours_ntsc;
    copper = c[0];
    gold = frame_is_pal() ? 0x1E : 0x2E;
    for (i = 0; i < 8; ++i) gem_col[i] = c[i];
    gem_col[8] = COL_BACK;
    for (i = 0; i < 4; ++i) ((uint8_t *)0x2C0)[i] = c[i];   /* PCOLR0-3 */
    COLOR0 = copper;                      /* text: copper */
    COLOR1 = gold;                        /* pale gold */
    COLOR2 = 0x08;                        /* dim */
    COLOR3 = 0x0E;                        /* white */
    COLOR4 = COL_BACK;
}

/* ------------------------------------------------------------ screens */

/* The playfield is wide (48 characters of ANTIC 2, 24 of ANTIC 6): in GTIA
   mode 10 the border shows COLPM0 (the walls' copper), so the band covers it
   with background pixels. Text and the well keep to the middle 40 (20). */
#define RB 48                             /* bytes an ANTIC 2 row */
#define TB 24                             /* bytes an ANTIC 6/7 row */
#define M6 2                              /* ANTIC 6 column 0 of the normal width */

static uint8_t *const rows[20] = {
    WELL + 0 * RB, WELL + 1 * RB, WELL + 2 * RB, WELL + 3 * RB, WELL + 4 * RB,
    WELL + 5 * RB, WELL + 6 * RB, WELL + 7 * RB, WELL + 8 * RB, WELL + 9 * RB,
    WELL + 10 * RB, WELL + 11 * RB, WELL + 12 * RB, WELL + 13 * RB, WELL + 14 * RB,
    WELL + 15 * RB, WELL + 16 * RB, WELL + 17 * RB, WELL + 18 * RB, WELL + 19 * RB };
#define row_at(r) (rows[r])

static void show(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x23;                        /* wide, no player DMA */
}

static void dl_row(uint8_t *p, uint8_t mode, const uint8_t *addr)
{
    p[0] = mode;
    p[1] = (uint8_t)(uint16_t)addr;
    p[2] = (uint16_t)addr >> 8;
}

/* the game's display list; band: rows 8-11 are the message band */
static uint8_t *game_dl(uint8_t *p, uint8_t band)
{
    static uint8_t *start, r;
    start = p;
    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;
    dl_row(p, 0x46, HUD); p += 3;
    dl_row(p, 0xC6, HUD + TB); p += 3;            /* DLI on */
    *p++ = 0x10;                                  /* 2 blank lines */
    for (r = 0; r < 20; ++r) {
        if (band && r == MSG_ROW) {
            *p++ = 0xF0;                          /* DLI off */
            dl_row(p, 0x46, MSG); p += 3;
            dl_row(p, 0xC6, MSG + TB); p += 3;    /* DLI on */
            *p++ = 0x70;
            r += 3;
            continue;
        }
        /* row 0: DLI copper; the floor: DLI prep (and the band's) */
        dl_row(p, (r == 0 || r == 19 || (band && (r == MSG_ROW - 1 || r == MSG_ROW + 4))) ? 0xC2 : 0x42,
               row_at(r));
        p += 3;
    }
    *p++ = 0x90;                                  /* 2 blank lines, DLI off */
    dl_row(p, 0x41, start);
    return p + 3;
}

static void screens_init(void)
{
    static uint8_t *p, r;
    tiles_init();
    game_dl(DL_GAME, 0);
    game_dl(DL_BAND, 1);
    p = DL_TITLE;
    *p++ = 0x70; *p++ = 0x70; *p++ = 0x70; *p++ = 0x70;
    dl_row(p, 0x47, TITLE); p += 3;
    *p++ = 0xF0;                                  /* DLI on */
    *p++ = 0x10;                                  /* 2 blank lines */
    dl_row(p, 0xC2, GEMROW); p += 3;              /* DLI copper (the background here) */
    *p++ = 0x80;                                  /* DLI prep */
    *p++ = 0x80;                                  /* DLI off */
    *p++ = 0x40;
    dl_row(p, 0x46, TTEXT); p += 3;
    for (r = 1; r < TROWS; ++r) *p++ = 0x06;
    dl_row(p, 0x41, DL_TITLE);
    CHBAS = 0xA0;
    GPRIOR = 0;
    colours();
    display_install();
}

/* the pause / game-over band over well rows 8-11 (0: the well again) */
static void band(const char *l1, const char *l2)
{
    if (!l1) {
        SDLSTL = (uint16_t)DL_GAME;
        return;
    }
    memset(MSG, 0, 2 * TB);
    put(MSG + M6 + ((20 - strlen(l1)) >> 1), l1, 0xC0);
    put(MSG + TB + M6 + ((20 - strlen(l2)) >> 1), l2, 0x40);
    SDLSTL = (uint16_t)DL_BAND;                   /* (the OS sets it in the vertical blank) */
    frame_wait();
}

/* ------------------------------------------------------------ the well */

static uint8_t shown_n;                           /* piece + marks on screen */
static uint8_t key_type = 0xFF, key_rot, key_ghost;
static uint8_t ghost_dirty = 1, g_type, g_rot, g_ghost;
static int8_t g_x;
static int8_t key_x, key_y;

static void well_draw(void)
{
    well_copy(gs.field[0]);                       /* (well.s) */
    shown_n = 0;
    key_type = 0xFF;
    ghost_dirty = 1;
}

static void well_frame(void)
{
    static uint8_t r, *d;
    memset(WELL, T_EMPTY, 20 * RB);
    for (r = 0; r < FIELD_H; ++r) {
        d = row_at(r);
        d[WELL_COL - 1] = T_LWALL;
        d[WELL_COL + FIELD_W] = T_RWALL;
    }
    WELL[WELL_COL - 1] = T_EMPTY;              /* (row 0: colour 0 is the background) */
    WELL[WELL_COL + FIELD_W] = T_EMPTY;
    d = row_at(FIELD_H);
    d[WELL_COL - 1] = T_LFLOOR;
    memset(d + WELL_COL, T_FLOOR, FIELD_W);
    d[WELL_COL + FIELD_W] = T_RFLOOR;
    memset(row_at(4) + NEXT_COL - 1, T_TRAY, 6);
}

/* the falling piece and its landing marks, where they changed */
static uint8_t *shown_p[8], *shown_f[8];          /* screen cell, field cell */
static const uint8_t row12[20] = {
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168, 180, 192, 204, 216, 228 };

static void piece_draw(uint8_t force)
{
    static uint8_t i, n, n0, ghost, show, y, x, r, c, tile;
    show = (gs.state == STATE_PLAYING || gs.state == STATE_PAUSED) && gs.clear_timer == 0;
    if (show) {
        /* the landing row only changes with x, the rotation, the piece or
           the well (the search is up to 19 collision tests) */
        if (ghost_dirty || g_type != gs.current.type || g_rot != gs.current.rotation ||
            g_x != gs.current.x || g_ghost < gs.current.y) {
            g_ghost = (uint8_t)game_ghost_y(&gs);
            g_type = gs.current.type;
            g_rot = gs.current.rotation;
            g_x = gs.current.x;
            ghost_dirty = 0;
        }
        ghost = g_ghost;
    } else ghost = 0;
    if (!force && show && key_type == gs.current.type && key_rot == gs.current.rotation &&
        key_x == gs.current.x && key_y == gs.current.y && key_ghost == ghost) return;
    for (i = 0; i < shown_n; ++i) *shown_p[i] = T_EMPTY + *shown_f[i];
    shown_n = 0;
    key_type = 0xFF;
    if (!show) return;
    key_type = gs.current.type;
    key_rot = gs.current.rotation;
    key_x = gs.current.x;
    key_y = gs.current.y;
    key_ghost = ghost;
    n0 = ((key_type << 2) | (key_rot & 3)) << 2;
    x = (uint8_t)key_x;
    /* the marks, then the piece over them */
    for (tile = (ghost == (uint8_t)key_y); tile < 2; ++tile) {
        y = tile ? (uint8_t)key_y : ghost;
        for (n = n0, i = 0; i < 4; ++i, ++n) {
            r = y + piece_cell_r[n];
            c = x + piece_cell_c[n];
            shown_p[shown_n] = row_at(r) + WELL_COL + c;
            shown_f[shown_n] = gs.field[0] + row12[r] + c;
            *shown_p[shown_n] = tile ? T_EMPTY + 1 + key_type : T_GHOST + key_type;
            ++shown_n;
        }
    }
}

static void next_draw(void)
{
    static const PieceShape *sh;
    static uint8_t r, c, *d;
    sh = piece_get_shape(gs.next_type, 0);
    for (r = 0; r < 2; ++r) {
        d = row_at(r + 2) + NEXT_COL;
        for (c = 0; c < 4; ++c) d[c] = sh->cells[r][c] ? T_EMPTY + 1 + gs.next_type : T_EMPTY;
    }
}

/* the rows being cleared blink in copper */
static void clear_flash(void)
{
    static uint8_t i, r, c, on, *d;
    on = (gs.clear_timer & 2) != 0;
    for (i = 0; i < (uint8_t)gs.clear_count; ++i) {
        r = (uint8_t)gs.clear_lines[i];
        d = row_at(r) + WELL_COL;
        for (c = 0; c < FIELD_W; ++c) d[c] = on ? T_FLASH : T_EMPTY + gs.field[r][c];
    }
}

/* ------------------------------------------------------------ HUD */

static uint32_t hud_score;
static int16_t hud_lines, hud_level;

static void hud_draw(uint8_t force)
{
    if (force) {
        memset(HUD, 0, 2 * TB);
        put(HUD + M6, "POINTS", 0x80);
        put(HUD + M6 + 16, "NEXT", 0x80);
        put(HUD + TB + M6, "ROWS", 0x80);
        put(HUD + TB + M6 + 11, "STAGE", 0x80);
        hud_lines = hud_level = -1;
        hud_score = 0xFFFFFFFFUL;
    }
    if (gs.score != hud_score) {
        hud_score = gs.score;
        put_num(HUD + M6 + 7, hud_score, 7, 0x40);
    }
    if (gs.lines != hud_lines) {
        hud_lines = gs.lines;
        put_num(HUD + TB + M6 + 5, (uint32_t)hud_lines, 4, 0xC0);
    }
    if (gs.level != hud_level) {
        hud_level = gs.level;
        put_num(HUD + TB + M6 + 17, (uint32_t)hud_level, 2, 0xC0);
    }
}

/* ------------------------------------------------------------ sound */

static void music_start(void)
{
    if (!music_on) return;
    music_play(2, music_lead, 5);
    music_play(3, music_bass, 4);
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 3000, "GEM" }, { 2000, "ROW" }, { 1200, "BOX" }, { 600, "TOP" }, { 200, "ACE" } };

static uint8_t hi_rank(uint32_t s)
{
    static uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

static void enter_initials(uint8_t rank, uint32_t score)
{
    static char name[4], line[12];
    static uint8_t pos, p, k, i;
    static char c;
    strcpy(name, "A  ");
    pos = 0;
    logu("stak: hiscore rank=", rank + 1);
    LOGNL();
    for (;;) {
        strcpy(line, "NAME ");
        strcat(line, name);
        band("HIGH SCORE!", line);
        input_read();
        p = input_pressed();
        k = key_get();
        c = k != KEY_NONE ? key_ascii(k) : 0;
        if (c >= 'A' && c <= 'Z') { name[pos] = c; p = IN_FIRE; }
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & IN_FIRE) {
            if (++pos == 3) break;
            name[pos] = name[pos - 1];
        }
    }
    for (i = 4; i > rank; --i) hiscores[i] = hiscores[i - 1];
    hiscores[rank].score = score;
    memcpy(hiscores[rank].name, name, 3);
    hiscores[rank].name[3] = 0;
    a8_log_s("stak: name=");
    a8_log_s(hiscores[rank].name);
    LOGNL();
}

/* ------------------------------------------------------------ title */

static void title_print(uint8_t row, uint8_t col, const char *s, uint8_t colour)
{
    put(TTEXT + row * TB + M6 + col, s, colour);
}

static void title_screen(void)
{
    static uint8_t i;
    static char buf[8];
    memset(TITLE, 0, TB);
    put(TITLE + M6 + 5, "STAKATTACK", 0x00);
    memset(GEMROW, T_EMPTY, RB);
    for (i = 0; i < 7; ++i) GEMROW[17 + i * 2] = T_EMPTY + 1 + i;
    memset(TTEXT, 0, TROWS * TB);
    title_print(1, 0, "PRESS FIRE TO START", 0xC0);
    title_print(3, 4, "HIGH SCORES", 0x00);
    for (i = 0; i < 5; ++i) {
        buf[0] = '1' + i;
        buf[1] = 0;
        title_print(4 + i, 3, buf, 0x80);
        title_print(4 + i, 5, hiscores[i].name, 0xC0);
        put_num(TTEXT + (4 + i) * TB + M6 + 9, hiscores[i].score, 7, 0x40);
    }
    title_print(10, 0, "STICK MOVES, UP", 0x80);
    title_print(11, 0, "ROTATES, DOWN DROPS,", 0x80);
    title_print(12, 0, "FIRE SLAMS. START", 0x80);
    title_print(13, 0, "PAUSES, OPTION MUSIC", 0x80);
    gem_col[0] = COL_BACK;                        /* no walls: colour 0 is the background */
    show(DL_TITLE);
}

static void game_screen(void)
{
    well_frame();
    well_draw();
    next_draw();
    hud_draw(1);
    gem_col[0] = copper;
    show(DL_GAME);
}

/* ------------------------------------------------------------ main */

#ifndef A8_NO_LOG
static const char *const state_names[4] = { "title", "playing", "paused", "gameover" };
#endif

int main(void)
{
    static uint8_t ticks, t, in, pr, k, gi, edges, ntsc, prev_state, prev_clear, prev_level;
    static uint8_t field_changed, ev_lock, ev_drop, ev_clear, last_kind;

    screens_init();
    sound_init();
    frame_init();
    game_init(&gs);
    a8_log_s("stak: ready\n");

title:
    title_screen();
    a8_log_s("stak: title\n");
    music_start();
    for (;;) {
        frame_wait();
        input_read();
        pr = input_pressed();
        if (pr & IN_OPTION) { music_on ^= 1; if (music_on) music_start(); else music_stop(); }
        if (pr & (IN_FIRE | IN_START)) break;
    }
    game_update(&gs, GINPUT_START);               /* the original's title -> game_start */
    logu("stak: start next=", gs.next_type);
    logu(" type=", gs.current.type);
    LOGNL();
    game_screen();
    prev_state = gs.state;
    prev_level = gs.level;
    edges = 0;
    ntsc = 0;

    for (;;) {
        ticks = frame_wait();
        if (ticks > 3) { stak_probe.overruns += ticks - 3; ticks = 3; }
        stak_probe.frames++;
        if (ticks > 1) {
            stak_probe.overruns += ticks - 1;
            if (last_kind == 1) stak_probe.late_lock += ticks - 1;
            else if (last_kind == 2) stak_probe.late_clear += ticks - 1;
            else stak_probe.late_other += ticks - 1;
        }
        in = input_read();
        pr = input_pressed();
        k = key_get();
        if (pr & IN_OPTION) { music_on ^= 1; if (music_on) music_start(); else music_stop(); }
        if (k == KEY_ESC && gs.state != STATE_GAMEOVER) {     /* port: ESC gives up */
            a8_log_s("stak: quit\n");
            gs.state = STATE_GAMEOVER;
        }
        /* the original's mapping: UP rotates, FIRE slams (starts, when not
           playing), START or P pauses; all on the press */
        if (pr & IN_UP) edges |= GINPUT_ROTATE;
        if (pr & IN_FIRE) edges |= gs.state == STATE_PLAYING ? GINPUT_DROP : GINPUT_START;
        if ((pr & IN_START) || k == KEY_P) edges |= GINPUT_START;
        if (gs.state == STATE_GAMEOVER && !(pr & IN_FIRE)) edges &= ~GINPUT_START;
        gi = 0;
        if (in & IN_LEFT) gi |= GINPUT_LEFT;
        if (in & IN_RIGHT) gi |= GINPUT_RIGHT;
        if (in & IN_DOWN) gi |= GINPUT_DOWN;

        if (!frame_is_pal() && ++ntsc == 6) {      /* 60 Hz: the logic runs 5 frames in 6 */
            ntsc = 0;
            ticks = 0;
        }
        field_changed = ev_lock = ev_drop = ev_clear = 0;
        for (t = 0; t < ticks; ++t) {
            prev_clear = gs.clear_timer;
            k = gs.state;
            game_update(&gs, gi | edges);
            edges = 0;
            stak_probe.ticks++;
            if (k != STATE_PLAYING) break;        /* (the flags are a playing tick's) */
            if (gs.just_locked) { ev_lock = 1; field_changed = 1; stak_probe.locks++; }
            if (gs.just_dropped) ev_drop = 1;
            if (gs.just_cleared) ev_clear = 1;
            if (prev_clear && !gs.clear_timer) field_changed = 1;
            if (gs.state != STATE_PLAYING) break;
        }

        if (ev_lock) {
            logu("stak: lock type=", key_type == 0xFF ? 0 : key_type);
            logl(" score=", gs.score);
            LOGNL();
            if (ev_drop) snd_sfx(0, 140, SND_NOISE, 12, 1, 4);
            else snd_sfx(0, 70, SND_PURE, 9, 1, 3);
        }
        if (ev_clear) {
            stak_probe.cleared += gs.clear_count;
            logu("stak: clear rows=", gs.clear_count);
            LOGNL();
            snd_sfx(1, 60 - gs.clear_count * 8, SND_PURE, 13, 1, -2);
        }
        if (field_changed && !ev_clear) {
            if (prev_clear) {
                logu("stak: cleared lines=", gs.lines);
                logu(" level=", gs.level);
                logl(" score=", gs.score);
                LOGNL();
                well_draw();                      /* the rows came down */
            } else
                ghost_dirty = 1;                  /* a lock: the piece's cells are
                                                     shown already (piece_draw
                                                     restores them from the field) */
            next_draw();
        }
        if (ev_clear) well_draw();
        if (gs.level != prev_level) {
            prev_level = gs.level;
            logu("stak: stage level=", gs.level);
            LOGNL();
            snd_sfx(1, 30, SND_PURE, 14, 0, -1);
        }
        last_kind = (field_changed && prev_clear) || ev_clear ? 2 : ev_lock ? 1 : 0;
        if (gs.clear_timer) clear_flash();
        piece_draw(0);
        hud_draw(0);

        if (gs.state != prev_state) {
            a8_log_s("stak: state=");
            a8_log_s(state_names[gs.state]);
            logl(" score=", gs.score);
            logu(" lines=", gs.lines);
            logu(" level=", gs.level);
            LOGNL();
            if (gs.state == STATE_PAUSED) band("PAUSED", "PRESS START");
            else if (gs.state == STATE_PLAYING) band(0, 0);
            else if (gs.state == STATE_GAMEOVER) {
                band("GAME OVER", "PRESS FIRE");
                music_stop();
                snd_sfx(1, 90, SND_BUZZ, 14, 0, 2);
                logu("stak: perf frames=", stak_probe.frames);
                logu(" ticks=", stak_probe.ticks);
                logu(" overruns=", stak_probe.overruns);
                LOGNL();
            } else if (gs.state == STATE_TITLE) {
                k = hi_rank(gs.score);
                if (k < 5) enter_initials(k, gs.score);
                goto title;
            }
            prev_state = gs.state;
        }
        if ((stak_probe.frames & 255) == 0 && gs.state == STATE_PLAYING) {
            logu("stak: perf frames=", stak_probe.frames);
            logu(" ticks=", stak_probe.ticks);
            logu(" overruns=", stak_probe.overruns);
            LOGNL();
        }
    }
}
