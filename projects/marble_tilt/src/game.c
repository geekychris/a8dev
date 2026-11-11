/*
 * game.c - Marble Tilt for the Atari 8-bit: the shell round the rules
 * (mt_core.c) and the board (board.c).
 *
 * The rules run the original's 25 ticks a second (every second PAL
 * frame); the marble (a player) moves every frame; a new board is drawn a
 * row a frame into the hidden buffer and then shown.
 *
 * Controls: the stick tilts the board (up: the marble rolls away from you),
 * START (or P) pauses, B the autopilot, ESC the title. Title: left/right
 * or up/down choose the level to start on, FIRE or START plays.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "mt.h"
#include "board.h"

/* this file's BSS (and -Cl's static locals) in the SDK's LOWRAM */
#pragma bss-name ("LOWRAM")

extern void mt_dli(void);
extern uint8_t mt_sky[];
extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held;
extern uint8_t key_take(void);

enum { GS_TITLE, GS_PLAY, GS_OVER };
#define KEY_B 21

static uint8_t state, start_level = 1, autopilot, paused, state_timer, hud_dirty = 1, msg_timer;
static uint16_t best;
static const char *msg;
/* for the test: state, level, falls, autopilot, over/won, between, gates, paused, the title's level */
uint8_t mt_probe[9];

#ifndef A8_NO_LOG
static void lg(const char *s) { a8_log_s(s); }
static void lgu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
#define lg_end() a8_log_c('\n')
#else
#define lg(s) ((void)0)
#define lgu(k, v) ((void)0)
#define lg_end() ((void)0)
#endif

/* ------------------------------------------------------------ the HUD */
static void put(uint8_t pos, const char *s, uint8_t w)
{
    uint8_t *p = A3D_TEXT + pos, c;
    while (w && (c = (uint8_t)*s++) != 0) {
        *p++ = c < 0x20 ? c + 0x40 : c < 0x60 ? c - 0x20 : c;
        --w;
    }
    while (w--) *p++ = 0;
}

static void numw(uint8_t pos, uint16_t v, uint8_t w)
{
    uint8_t *p = A3D_TEXT + pos + w;
    do { *--p = (uint8_t)(0x10 + v % 10); v /= 10; } while (--w && v);
    while (w--) *--p = 0x10;
}

static void hud(void)
{
    uint16_t s = (uint16_t)(G.time_left / 25);
    if (state == GS_TITLE) {
        put(0, " MARBLE TILT   LEVEL <", 22);
        numw(22, start_level, 1);
        put(23, ">  ", 3);
        put(26, mt_level_names[start_level - 1], 14);
        put(40, " FIRE TO PLAY      BEST", 23);
        numw(64, best, 5);
        put(69, "", 11);
        return;
    }
    put(0, mt_level_names[G.level - 1], 13);
    put(13, "TIME", 5);
    numw(18, s, 3);
    if (s <= 10) A3D_TEXT[18] |= 0x80, A3D_TEXT[19] |= 0x80, A3D_TEXT[20] |= 0x80;
    put(21, " SCORE", 7);
    numw(28, G.score, 5);
    put(33, "", 7);
    put(40, msg_timer ? msg : state == GS_OVER ? (G.won ? "ALL DONE!" : "TIME UP") : "FALLS", 10);
    if (!msg_timer && state != GS_OVER) numw(46, G.falls, 2);
    put(50, G.gates_open || (G.level != 4 && G.level != 8) ? "" : "GATES SHUT", 11);   /* the levels with gates */
    put(61, autopilot ? "AUTO" : paused ? "PAUSED" : "", 7);
    put(68, state == GS_OVER ? "FIRE" : "", 12);
}

static void flash(const char *m)
{
    msg = m;
    msg_timer = 50;
    hud_dirty = 1;
}

/* ------------------------------------------------------------ flow */
static void to_title(void)
{
    state = GS_TITLE;
    mt_new_game(1);
    autopilot = 1;
    board_invalidate();
    hud_dirty = 1;
    lg("mt: title"); lg_end();
}

static void start_game(void)
{
    state = GS_PLAY;
    mt_new_game(start_level);
    autopilot = 0;
    paused = 0;
    board_invalidate();
    hud_dirty = 1;
    lgu("mt: start level=", start_level); lg_end();
}

static void events(void)
{
    uint8_t e, a;
    while ((e = mt_pop_event(&a)) != MTE_NONE) {
        if (state == GS_TITLE) {
            if (e == MTE_OVER || e == MTE_EXIT) state_timer = 50;
            continue;
        }
        switch (e) {
        case MTE_WALL: snd_sfx(0, 100, SND_NOISE, 6 + (a > 8 ? 8 : a), 2, 0); break;
        case MTE_BUMP: snd_sfx(0, 40, SND_PURE, 12, 2, 3); lg("mt: bump"); lg_end(); break;
        case MTE_FALL:
            snd_sfx(1, 30, SND_PURE, 12, 1, 4);
            flash("FELL! -3S");
            lgu("mt: fall falls=", G.falls); lgu(" time=", (uint16_t)(G.time_left / 25)); lg_end();
            break;
        case MTE_RESPAWN: lg("mt: respawn"); lg_end(); break;
        case MTE_CHECKPOINT: snd_sfx(2, 60, SND_PURE, 10, 1, -2); flash("CHECKPOINT"); lg("mt: checkpoint"); lg_end(); break;
        case MTE_GATE: snd_sfx(3, G.gates_open ? 90 : 150, SND_PURE, 8, 1, 0); hud_dirty = 1; break;
        case MTE_EXIT:
            snd_sfx(2, 50, SND_PURE, 14, 1, -1);
            flash("THROUGH!");
            lgu("mt: exit level=", a); lgu(" score=", G.score); lg_end();
            break;
        case MTE_LEVEL: lgu("mt: level=", a); lg_end(); hud_dirty = 1; break;
        case MTE_OVER:
        case MTE_WIN:
            state = GS_OVER;
            state_timer = 50;
            if (G.score > best) best = G.score;
            snd_sfx(2, 120, SND_BUZZ, 14, 1, 3);
            lgu("mt: gameover score=", G.score); lgu(" won=", G.won); lg_end();
            hud_dirty = 1;
            break;
        }
    }
}

static uint8_t input(void)
{
    uint8_t held = in_held, edge = in_take(), k = key_take(), in = 0;
    switch (state) {
    case GS_TITLE:
        if ((edge & (IN_RIGHT | IN_UP)) && start_level < MT_LEVELS) { ++start_level; hud_dirty = 1; }
        if ((edge & (IN_LEFT | IN_DOWN)) && start_level > 1) { --start_level; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case GS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; hud_dirty = 1; lgu("mt: paused=", paused); lg_end(); }
        if (k == KEY_ESC) { to_title(); break; }
        if (k == KEY_B) { autopilot ^= 1; hud_dirty = 1; lgu("mt: autopilot=", autopilot); lg_end(); }
        in = (held & IN_LEFT ? MT_IN_LEFT : 0) | (held & IN_RIGHT ? MT_IN_RIGHT : 0) |
             (held & IN_UP ? MT_IN_UP : 0) | (held & IN_DOWN ? MT_IN_DOWN : 0);
        break;
    case GS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return in;
}

int main(void)
{
    uint8_t n, ms = 0, y, in = 0, ticks = 0, sec_t = 0, bot_in = 0;
    uint16_t tsec = 0;
    uint8_t loops = 0, boards = 0;

    a3d_init(88, 2);
    COLOR0 = A8_COLOR(9, 8);            /* the floor */
    COLOR1 = A8_COLOR(2, 6);            /* the walls */
    COLOR2 = A8_COLOR(12, 10);          /* the exit, checkpoints, bumpers */
    COLOR4 = mt_sky[0];
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = A8_COLOR(9, 2);
    for (y = 7; y < 80; y += 8) a3d_dl_flag(y, 0x80);
    VDSLST = (uint16_t)mt_dli;
    marble_init();
    sound_init();
    in_install();
    frame_init();
    to_title();
    lg("mt: ready"); lg_end();
    for (;;) {
        n = a3d_sync();
        in = input();
        ms += n * (frame_is_pal() ? 20 : 17);
        if (ms > 160) ms = 160;
        while (ms >= 40) {
            ms -= 40;
            if (state == GS_OVER) { if (state_timer) --state_timer; continue; }
            if (paused) continue;
            if (autopilot) { if (!(ticks & 1)) bot_in = mt_bot(); in = bot_in; }   /* port: the bot every second tick */
            mt_tick(in);
            events();
            if (state == GS_TITLE && state_timer && !--state_timer) { mt_new_game(1); board_invalidate(); }
            if (msg_timer && !--msg_timer) hud_dirty = 1;
            ++ticks;
            if (state == GS_PLAY && ++sec_t >= 25) { sec_t = 0; hud_dirty = 1; }   /* the clock */
        }
        boards += board_step();
        ++loops;
        marble_draw();
        if (hud_dirty) { hud(); hud_dirty = 0; }
        mt_probe[0] = state; mt_probe[1] = G.level; mt_probe[2] = (uint8_t)G.falls; mt_probe[3] = autopilot;
        mt_probe[4] = G.over | (G.won << 1); mt_probe[5] = G.between; mt_probe[6] = G.gates_open; mt_probe[7] = paused; mt_probe[8] = start_level;
        if (frame_count - tsec >= 250) {
            lgu("mt: ticks5s=", ticks); lgu(" view=", board_view()); lgu(" loops5s=", loops); lgu(" boards5s=", boards); lg_end();
            ticks = loops = boards = 0;
            tsec = frame_count;
        }
    }
    return 0;
}
