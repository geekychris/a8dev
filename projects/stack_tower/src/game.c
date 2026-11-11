/*
 * game.c - Stack Tower for the Atari 8-bit: the shell round the rules
 * (st_core.c) and the tower (tower.c, panel.c).
 *
 * The rules run at the original's 25 steps a second (40 ms) whatever the
 * drawing takes; the tower turns towards the piece's column a third of the
 * way each step, the short way round. A frame draws only what changed.
 *
 * Atari: ANTIC D double buffered (projects/a8_3d), the tower's core is
 * three quad-width players behind the playfield (so all three playfield
 * colours are free for the bricks), a DLI paints the sky in bands and the
 * text rows' colours, POKEY plays the effects and the title tune.
 *
 * Controls: joystick left/right turn the tower, up rotates clockwise, down
 * soft-drops, FIRE hard-drops; OPTION (or Z) rotates anticlockwise, SELECT
 * (or C) holds, G ghost, P or START pauses, ESC back to the title.
 * Title: left/right mode, up/down start level, FIRE or START plays.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "a3d.h"
#include "st.h"
#include "tower.h"

enum { SS_TITLE, SS_PLAY, SS_OVER };

extern void st_dli(void);
extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held;
extern uint8_t key_take(void);
extern uint8_t st_sky[];

static uint8_t state, mode, start_level = 1, ghost = 1, paused;
static uint8_t view;                    /* facing column * 16 + sixteenths */
static int8_t das_dir;
static uint8_t das_timer, state_timer, flash_timer;
static const char *flash_msg;
static int32_t best[MODE_COUNT];
static uint32_t next_seed = 1;
static uint8_t hud_dirty = 1;
static uint16_t demo_games;
static uint16_t sec_ms, secs;
/* for the test's bot: state, piece type, rot, col, row, over, frames drawn (lo) */
uint8_t st_probe[8];

/* ------------------------------------------------------------ sound */
static const a8_note tune[] = {
    { 60, 12 }, { 64, 12 }, { 67, 12 }, { 72, 24 }, { 67, 12 }, { 69, 12 }, { 65, 24 },
    { 62, 12 }, { 65, 12 }, { 69, 12 }, { 74, 24 }, { 72, 12 }, { 71, 12 }, { 67, 24 },
    { 0, 0 },
};
static const a8_note bass[] = {
    { 48, 24 }, { 48, 24 }, { 53, 24 }, { 53, 24 }, { 50, 24 }, { 50, 24 }, { 55, 24 }, { 55, 24 },
    { 0, 0 },
};

static void sfx(uint8_t e)
{
    switch (e) {
    case STE_MOVE:    snd_sfx(0, 90, SND_PURE, 6, 2, 0); break;
    case STE_ROTATE:
    case STE_KICK:    snd_sfx(0, 60, SND_PURE, 8, 2, -4); break;
    case STE_DROP:    snd_sfx(1, 40, SND_NOISE, 10, 1, 0); break;
    case STE_LOCK:    snd_sfx(1, 140, SND_PURE, 9, 1, 6); break;
    case STE_HOLD:    snd_sfx(0, 50, SND_PURE, 8, 1, 4); break;
    case STE_GARBAGE: snd_sfx(1, 30, SND_NOISE, 12, 1, 2); break;
    case STE_CLEAR:   snd_sfx(2, 60, SND_PURE, 12, 1, -2); break;
    case STE_LEVEL:   snd_sfx(2, 80, SND_PURE, 14, 1, -3); break;
    case STE_OVER:    snd_sfx(2, 120, SND_BUZZ, 14, 1, 3); break;
    case STE_WIN:     snd_sfx(2, 40, SND_PURE, 14, 1, -1); break;
    }
}

/* ------------------------------------------------------------ the HUD */
static void hud_num(uint8_t x, uint8_t row, int32_t v, uint8_t w)
{
    if (v > 999999L) v = 999999L;
    if (v >= 10000) {                   /* a3d_num is 16-bit: two halves */
        a3d_num(x, row, (uint16_t)(v / 1000), (uint8_t)(w - 3));
        a3d_num((uint8_t)(x + w - 3), row, (uint16_t)(v % 1000), 3);
        if (v % 1000 < 100) A3D_TEXT[row * 40 + x + w - 3] = 0x10;
        if (v % 1000 < 10) A3D_TEXT[row * 40 + x + w - 2] = 0x10;
    } else {
        a3d_num(x, row, (uint16_t)v, w);
    }
}

/* the rows are cleared only when the layout changes (title <-> game), so
   an update never shows half-written text */
static uint8_t hud_layout = 0xFF;
static void clear_rows(uint8_t layout)
{
    if (hud_layout != layout) { memset(A3D_TEXT, 0, 80); hud_layout = layout; }
}

/* text padded with spaces to width w */
static void print_w(uint8_t x, uint8_t row, const char *s, uint8_t w)
{
    memset(A3D_TEXT + row * 40 + x, 0, w);
    a3d_print(x, row, s);
}

static const char *mode_up[MODE_COUNT] = { "MARATHON", "RISING", "DIG" };

static void hud_play(void)
{
    uint16_t s = secs;
    clear_rows(1);
    a3d_print(0, 0, "SCORE");
    hud_num(6, 0, game.score, 6);
    a3d_print(14, 0, "LEVEL");
    a3d_num(20, 0, (uint16_t)game.level, 2);
    if (game.mode == MODE_DIG) {
        print_w(24, 0, "RUBBLE", 7);
        a3d_num(31, 0, (uint16_t)game.garbage_left, 3);
    } else {
        print_w(24, 0, "RINGS", 6);
        a3d_num(30, 0, (uint16_t)game.lines, 4);
    }
    print_w(1, 1, flash_timer && flash_msg ? flash_msg : mode_up[game.mode], 10);
    a3d_num(12, 1, s / 60, 2);
    A3D_TEXT[40 + 14] = 0x1A;           /* ':' */
    A3D_TEXT[40 + 15] = (uint8_t)(0x10 + (s % 60) / 10);
    A3D_TEXT[40 + 16] = (uint8_t)(0x10 + s % 10);
    if (paused) print_w(20, 1, "PAUSED", 15);
    else if (state == SS_OVER) print_w(20, 1, game.won ? "CLEARED!  FIRE" : "TOPPED!  FIRE", 15);
    else {
        print_w(20, 1, "BEST", 5);
        hud_num(25, 1, best[game.mode], 6);
    }
}

static void hud_title(void)
{
    clear_rows(0);
    a3d_print(1, 0, "STACK TOWER");
    a3d_print(14, 0, "< ");
    print_w(16, 0, mode_up[mode], 9);
    a3d_print(25, 0, ">  LEVEL");
    a3d_num(34, 0, start_level, 2);
    a3d_print(1, 1, "FIRE TO PLAY");
    a3d_print(16, 1, "BEST");
    hud_num(21, 1, best[mode], 6);
}

/* ------------------------------------------------------------ flow */
static void handle_events(void)
{
    static const char *const names[] = { "", "RING!", "DOUBLE!", "TRIPLE!", "QUAD!", "PENTA!", "MEGA!" };
    StEvent e;
    while (st_pop_event(&e)) {
        if (e.type == STE_LOCK || e.type == STE_GARBAGE) ++board_ver;
        if (state == SS_TITLE) continue;              /* the demo is quiet */
        sfx(e.type);
        switch (e.type) {
        case STE_LOCK:
            a8_log("st: lock type=%d pieces=%d score=%ld\n", e.a, game.pieces, game.score);
            break;
        case STE_CLEAR:
            flash_msg = names[e.a < 7 ? e.a : 6];
            flash_timer = 40;
            a8_log("st: clear rings=%d lines=%d score=%ld\n", e.a, game.lines, game.score);
            break;
        case STE_LEVEL:
            flash_msg = "LEVEL UP";
            flash_timer = 40;
            a8_log("st: level=%d\n", e.a);
            break;
        case STE_GARBAGE:
            a8_log("st: rubble\n");
            break;
        case STE_OVER:
            a8_log("st: over score=%ld pieces=%d\n", game.score, game.pieces);
            break;
        case STE_WIN:
            a8_log("st: won score=%ld\n", game.score);
            break;
        }
        if (e.type >= STE_DROP) hud_dirty = 1;      /* not for moves and turns */
    }
}

static void start_game(void)
{
    st_new_game(mode, next_seed++, start_level, (int8_t)(view >> 4));
    ++board_ver;
    state = SS_PLAY;
    paused = 0;
    das_dir = 0;
    flash_timer = 0;
    secs = sec_ms = 0;
    music_stop();
    hud_dirty = 1;
    a8_log("st: start mode=%s level=%d seed=%ld\n", st_mode_names[mode], start_level, (long)game.seed);
}

static void to_title(void)
{
    state = SS_TITLE;
    st_new_game(MODE_MARATHON, 777 + next_seed, 1, 0);
    ++board_ver;
    music_play(2, tune, 6);
    music_play(3, bass, 4);
    hud_dirty = 1;
    a8_log("st: title\n");
}

/* the title's demo: the original's placement rule, a piece every 6 steps */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dUL;
    x ^= x >> 15; x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

/* port: where a placement lands comes from the columns' heights (a piece
   dropped from the top stops on the highest block of each of its columns),
   not by dropping it a row at a time: 64 placements in ~20K cycles */
static void demo_step(void)
{
    static uint8_t t;
    static int8_t height[ST_COLS];
    int8_t cells[5][2], land, top;
    uint8_t r, c, k, n, best_col = 0, best_rot = 0;
    int16_t h, besth = 32767;
    Piece p;
    if (++t < 6) return;
    t = 0;
    if (game.over) {
        st_new_game(MODE_MARATHON, hash32(game.seed), 1, 0);
        ++board_ver;
        ++demo_games;
        return;
    }
    for (c = 0; c < ST_COLS; c++) {
        for (k = ST_TOTAL; k > 0 && !game.board[(k - 1) * ST_COLS + c]; --k)
            ;
        height[c] = (int8_t)k;
    }
    p = game.piece;
    for (r = 0; r < 4; r++)
        for (c = 0; c < ST_COLS; c++) {
            p.rot = r;
            p.col = c;
            p.row = 0;
            n = st_cells_of(&p, cells);
            land = -100;
            for (k = 0; k < n; k++) {
                top = height[(uint8_t)cells[k][0]] - cells[k][1];
                if (top > land) land = top;
            }
            if (land + 2 > ST_TOTAL - 1) continue;
            for (h = 0, k = 0; k < n; k++) h += (land + cells[k][1]) * 4;
            h += (int16_t)(hash32((uint32_t)(c * 4 + r + game.pieces * 64)) & 3);
            if (h < besth) { besth = h; best_col = c; best_rot = r; }
        }
    p = game.piece;
    p.rot = best_rot;
    p.col = best_col;
    if (!st_collides(&p)) {
        game.piece = p;
        game.last_col = best_col;
    }
    st_hard_drop();
}

static void step(void)
{
    int8_t d;
    if (state == SS_TITLE) demo_step();
    if (state == SS_PLAY && !paused) {
        if (das_dir && --das_timer == 0) {             /* held: repeat after 0.28 s */
            st_move(das_dir);
            das_timer = 1;
        }
    }
    if (!paused) st_tick(40);
    handle_events();
    if (flash_timer && !--flash_timer) hud_dirty = 1;
    if (state_timer) --state_timer;
    if (state == SS_PLAY && (game.over || game.won)) {
        state = SS_OVER;
        state_timer = 40;
        if (game.score > best[game.mode]) best[game.mode] = game.score;
        hud_dirty = 1;
        a8_log("st: gameover score=%ld best=%ld\n", game.score, best[game.mode]);
    }
    /* the tower turns towards the piece's column, the short way round */
    d = (int8_t)((uint8_t)(game.piece.col * 16 - view + 128) - 128);
    if (d) view += (d / 3) ? d / 3 : d;
    if (state == SS_PLAY && !paused && !game.over && (sec_ms += 40) >= 1000) {   /* the clock */
        sec_ms -= 1000;
        ++secs;
        hud_dirty = 1;
    }
}

static void input(void)
{
    uint8_t in = in_held, edge = in_take(), k = key_take();
    int8_t dir;
    switch (state) {
    case SS_TITLE:
        if (edge & IN_LEFT) { mode = (uint8_t)((mode + MODE_COUNT - 1) % MODE_COUNT); hud_dirty = 1; }
        if (edge & IN_RIGHT) { mode = (uint8_t)((mode + 1) % MODE_COUNT); hud_dirty = 1; }
        if ((edge & IN_UP) && start_level < 10) { ++start_level; hud_dirty = 1; }
        if ((edge & IN_DOWN) && start_level > 1) { --start_level; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case SS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; hud_dirty = 1; a8_log("st: paused=%u\n", paused); }
        if (k == KEY_ESC) { to_title(); break; }
        if (paused) break;
        if (k == 61) { ghost ^= 1; a8_log("st: ghost=%u\n", ghost); }        /* G */
        if (edge & IN_UP) st_rotate(1);
        if ((edge & IN_OPTION) || k == KEY_Z) st_rotate(-1);
        if ((edge & IN_SELECT) || k == 18) st_hold();                  /* C */
        if (edge & IN_FIRE) st_hard_drop();
        st_soft_drop((in & IN_DOWN) != 0);
        dir = (in & IN_LEFT) ? -1 : (in & IN_RIGHT) ? 1 : 0;
        if (dir && (edge & (IN_LEFT | IN_RIGHT))) { st_move(dir); das_dir = dir; das_timer = 7; }
        else if (dir != das_dir) das_dir = 0;
        break;
    case SS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
}

/* the four games the browser played (tests/vectors.h): the rules must end
   each with the same pieces, score, board, queue and hold */
#ifndef A8_NO_LOG
#include "vectors.h"
static void run_vectors(void)
{
    uint8_t v, k, i, r, ok, q[5];
    uint16_t j;
    int8_t m;
    uint32_t h;
    for (v = 0; v < 4; ++v) {
        st_new_game(vectors[v].mode, vectors[v].seed, 1, 0);
        for (k = 0; k < 120 && !game.over && !game.won; k++) {
            r = (uint8_t)((k * 7 + vectors[v].seed) % 5);
            m = (int8_t)((k * 13 + vectors[v].seed) % 9) - 4;
            if (k % 11 == 5) st_hold();
            for (i = 0; i < r; i++) st_rotate(i % 2 ? -1 : 1);
            for (i = 0; i < (m < 0 ? -m : m); i++) st_move(m < 0 ? -1 : 1);
            st_hard_drop();
        }
        st_preview(5, q);
        h = 2166136261UL;
        for (j = 0; j < ST_COLS * ST_TOTAL; j++) { h ^= game.board[j]; h *= 16777619UL; }
        ok = k == vectors[v].pieces && game.score == vectors[v].score && game.lines == vectors[v].lines &&
             game.over == vectors[v].over && game.won == vectors[v].won && h == vectors[v].board &&
             game.hold == vectors[v].hold;
        for (i = 0; i < 5; i++) if (q[i] != vectors[v].queue[i]) ok = 0;
        a8_log("st: vector %u ok=%u pieces=%u score=%ld\n", v, ok, k, game.score);
    }
}
#endif

/* ------------------------------------------------------------ set-up */
static void core_players(void)
{
    uint8_t i, *p;
    pmg_init();
    for (i = 0; i < 3; ++i) {
        p = pmg_player_mem(i);
        memset(p + 32, i == 2 ? 0xFC : 0xFF, 2 * TW_ROWS * 4);
        pmg_size(i, 3);
        pmg_color(i, A8_COLOR(9, 2));
        pmg_x(i, (uint8_t)(48 + TW_CX - TW_HALF + 32 * i));
    }
    pmg_priority(4);                    /* playfield over players: the core shows in pixel 0 */
}

int main(void)
{
    uint8_t n, ms = 0, drawn, y, fps_t0 = 0;
    uint16_t frames_drawn = 0, full_draws = 0;

    a3d_init(88, 2);
    COLOR0 = A8_COLOR(3, 8);            /* red */
    COLOR1 = A8_COLOR(1, 12);           /* gold */
    COLOR2 = A8_COLOR(8, 10);           /* blue */
    COLOR4 = st_sky[0];
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = A8_COLOR(9, 2);
    for (y = 7; y < 80; y += 8) a3d_dl_flag(y, 0x80);
    VDSLST = (uint16_t)st_dli;
    core_players();
    sound_init();
    in_install();
    st_init_cells();
    tower_init();
    panel_init();
#ifndef A8_NO_LOG
    if (a8_in_harness()) run_vectors();
#endif
    to_title();
    frame_init();
    a8_log("st: ready\n");
    for (;;) {
        input();
        n = a3d_sync();                 /* the last frame is on screen: draw the other buffer */
        ms += n * (frame_is_pal() ? 20 : 17);
        if (ms > 160) ms = 160;
        while (ms >= 40) {
            ms -= 40;
            step();
        }
        tower_build(ghost && state == SS_PLAY, state != SS_OVER || !game.over);
        drawn = tower_draw(view);
        panel_draw(state != SS_TITLE);
        if (hud_dirty) {
            if (state == SS_TITLE) hud_title(); else hud_play();
            hud_dirty = 0;
        }
        a3d_show();
        st_probe[0] = state; st_probe[1] = (uint8_t)game.piece.type; st_probe[2] = (uint8_t)game.piece.rot;
        st_probe[3] = (uint8_t)game.piece.col; st_probe[4] = (uint8_t)game.piece.row; st_probe[5] = game.over | game.won;
        st_probe[6] = paused; st_probe[7] = (uint8_t)game.hold;
        if (drawn) ++frames_drawn;
        if (drawn == TW_ROWS) ++full_draws;
        if ((uint8_t)(frame_count - fps_t0) >= 250) {      /* every 5 s: drawn frames a second x 10 */
            a8_log("st: fps10=%u full=%u\n", frames_drawn * 2, full_draws);
            frames_drawn = full_draws = 0;
            fps_t0 = (uint8_t)frame_count;
        }
    }
    return 0;
}
