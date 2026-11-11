/*
 * game.c - Block Breaker 3D for the Atari 8-bit: the shell round the rules
 * (bb_core.c) and the picture (view.c).
 *
 * The rules run the original's 25 ticks a second (every second PAL frame);
 * the picture is drawn every frame: on the frame a tick ran, what moves is
 * shown halfway between the last two ticks, on the next at the tick, so the
 * ball and the paddle move at 50 Hz.
 *
 * Controls: the stick moves the paddle, FIRE serves, START (or P) pauses,
 * B the autopilot, ESC the title. Title: left/right or up/down choose the
 * start level, FIRE or START plays.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "bb.h"
#include "view.h"

#pragma bss-name ("LOWRAM")

extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held;
extern uint8_t key_take(void);

enum { GS_TITLE, GS_PLAY, GS_OVER };
#define KEY_B 21

static uint8_t state, start_level = 1, autopilot, paused, state_timer, hud_dirty = 1, msg_timer, fire_lock;
static uint16_t best, next_seed = 1, t0, bricks_t;
static const char *msg;
/* for the test: state, level, lives, autopilot, over | won << 1, between, paused, the title's level, stuck, left */
uint8_t bb_probe[10];

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
    uint8_t x;
    if (state == GS_TITLE) {
        put(0, " BLOCK BREAKER 3D         BEST ", 31);
        numw(31, best, 5);
        put(36, "", 4);
        put(40, " LEVEL <", 8);
        numw(48, start_level, 1);
        put(49, "> ", 2);
        put(51, bb_level_names[start_level - 1], 13);
        put(64, "FIRE TO PLAY", 16);
        return;
    }
    put(0, " SCORE", 7);
    numw(7, G.score, 5);
    put(12, "  LEVEL", 8);
    numw(20, G.level, 1);
    put(21, "  BALLS", 8);
    numw(29, G.lives, 1);
    put(30, autopilot ? "    AUTO" : "", 10);
    if (state == GS_OVER) {
        put(40, G.won ? " YOU WIN!  " : " GAME OVER ", 11);
        put(51, state_timer ? "" : "FIRE", 29);
    } else if (paused) put(40, " PAUSED", 40);
    else if (msg_timer) { put(40, " ", 1); put(41, msg, 39); }
    else if (G.between) put(40, " CLEARED!", 40);
    else if (G.stuck) {
        put(40, " LEVEL", 7);
        numw(47, G.level, 1);
        put(48, ": ", 2);
        put(50, bb_level_names[G.level - 1], 12);
        put(62, "FIRE TO SERVE", 18);
    } else {
        x = 40;
        put(40, "", 40);
        if (G.wide_t) { put(x + 1, "WIDE", 4); x += 6; }
        if (G.slow_t) { put(x + 1, "SLOW", 4); x += 6; }
        if (G.plough_t) put(x + 1, "PLOUGH", 6);
    }
}

static void flash(const char *m)
{
    msg = m;
    msg_timer = 40;
    hud_dirty = 1;
}

/* ------------------------------------------------------------ flow */
static void to_title(void)
{
    state = GS_TITLE;
    bb_new_game(777 + next_seed, 1);
    autopilot = 1;
    paused = 0;
    view_level();
    hud_dirty = 1;
    lg("bb: title"); lg_end();
}

static void start_game(void)
{
    state = GS_PLAY;
    bb_new_game(next_seed++, start_level);
    autopilot = 0;
    paused = 0;
    fire_lock = 1;
    view_level();
    hud_dirty = 1;
    lgu("bb: start level=", start_level); lg_end();
}

static const char *const cap_msg[CAP_COUNT] = { "WIDE PADDLE", "SLOW BALL", "EXTRA LIFE", "PLOUGH!" };

static void events(void)
{
    uint8_t e, a, b;
    while ((e = bb_pop_event(&a, &b)) != BBE_NONE) {
        if (e == BBE_LEVEL) view_level();
        if (e == BBE_BREAK || e == BBE_HIT) {
            t0 = a3d_ticks();
            view_cell(a);
            bricks_t = a3d_since(t0);
        }
        if (state == GS_TITLE) {
            if (e == BBE_OVER || e == BBE_WIN) state_timer = 50;
            continue;
        }
        switch (e) {
        case BBE_WALL: snd_sfx(0, 120, SND_PURE, 5, 2, 0); break;
        case BBE_PADDLE: snd_sfx(0, 55, SND_PURE, 10, 1, -1); hud_dirty = 1; break;
        case BBE_BREAK:
            snd_sfx(1, 24, SND_NOISE, 10, 1, -1);
            hud_dirty = 1;
            lgu("bb: break cell=", a); lgu(" type=", b); lgu(" score=", G.score); lgu(" left=", G.left); lg_end();
            break;
        case BBE_HIT: snd_sfx(1, 30, SND_PURE, 9, 1, 2); hud_dirty = 1; lgu("bb: hit cell=", a); lg_end(); break;
        case BBE_STEEL: snd_sfx(1, 15, SND_PURE, 9, 1, 0); break;
        case BBE_LOST:
            snd_sfx(2, 50, SND_PURE, 12, 1, 8);
            if (G.lives) flash("BALL LOST");
            lgu("bb: lost lives=", G.lives); lg_end();
            break;
        case BBE_SERVE: snd_sfx(0, 80, SND_PURE, 9, 1, -4); hud_dirty = 1; lg("bb: serve"); lg_end(); break;
        case BBE_CAPSULE:
            snd_sfx(2, 48, SND_PURE, 12, 1, -2);
            flash(cap_msg[a]);
            lgu("bb: capsule type=", a); lgu(" score=", G.score); lg_end();
            break;
        case BBE_CAP_MISS: lgu("bb: capsule missed type=", a); lg_end(); break;
        case BBE_CLEAR:
            snd_sfx(2, 60, SND_PURE, 14, 1, -2);
            hud_dirty = 1;
            lgu("bb: clear level=", a); lgu(" score=", G.score); lg_end();
            break;
        case BBE_LEVEL: hud_dirty = 1; lgu("bb: level=", a); lg_end(); break;
        case BBE_OVER:
        case BBE_WIN:
            state = GS_OVER;
            state_timer = 50;
            if (G.score > best) best = G.score;
            snd_sfx(2, e == BBE_WIN ? 30 : 70, SND_PURE, 14, 1, e == BBE_WIN ? -1 : 6);
            lgu("bb: gameover score=", G.score); lgu(" won=", G.won); lg_end();
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
        if ((edge & (IN_RIGHT | IN_UP)) && start_level < BB_LEVELS) { ++start_level; hud_dirty = 1; }
        if ((edge & (IN_LEFT | IN_DOWN)) && start_level > 1) { --start_level; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case GS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; hud_dirty = 1; lgu("bb: paused=", paused); lg_end(); }
        if (k == KEY_ESC) { to_title(); break; }
        if (k == KEY_B) { autopilot ^= 1; hud_dirty = 1; lgu("bb: autopilot=", autopilot); lg_end(); }
        if (!(held & IN_FIRE)) fire_lock = 0;   /* the fire that started the game doesn't also serve */
        in = (held & IN_LEFT ? BB_IN_LEFT : 0) | (held & IN_RIGHT ? BB_IN_RIGHT : 0) |
             (held & IN_UP ? BB_IN_UP : 0) | (held & IN_DOWN ? BB_IN_DOWN : 0) |
             ((edge | held) & IN_FIRE && !fire_lock ? BB_IN_FIRE : 0);
        break;
    case GS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return in;
}

int main(void)
{
    uint8_t n, ms = 0, in, half, ticks = 0, frames = 0, bot_in = 0;
    uint16_t tsec = 0, t1, work_t = 0;
    int16_t pbx = 0, pby = 0, pbz = 0, ppx = 0, ppy = 0;

    view_init();
    sound_init();
    in_install();
    frame_init();
    to_title();
    lg("bb: ready"); lg_end();
    for (;;) {
        view_wait();
        t1 = a3d_ticks();
        n = frame_wait();
        in = input();
        ms += n * (frame_is_pal() ? 20 : 17);
        if (ms > 120) ms = 120;
        half = 0;
        while (ms >= 40) {
            ms -= 40;
            if (state == GS_OVER) {
                if (state_timer && !--state_timer) hud_dirty = 1;
                continue;
            }
            if (paused) continue;
            if (autopilot) { if (!(ticks & 1)) bot_in = bb_bot(); in = bot_in; }   /* port: the bot every second tick */
            pbx = G.bx; pby = G.by; pbz = G.bz; ppx = G.px; ppy = G.py;
            bb_tick(in);
            in &= ~BB_IN_FIRE;
            half = 1;
            events();
            if (state == GS_TITLE && state_timer && !--state_timer) { bb_new_game(777 + next_seed, 1); view_level(); }
            if (msg_timer && !--msg_timer) hud_dirty = 1;
            ++ticks;
        }
        view_hide_ball = (G.between && G.stuck) || G.over;
        vf_bx[1] = G.bx; vf_by[1] = G.by; vf_bz[1] = G.bz; vf_px[1] = G.px; vf_py[1] = G.py;
        if (half) {
            vf_bx[0] = (pbx + G.bx) >> 1; vf_by[0] = (pby + G.by) >> 1; vf_bz[0] = G.stuck ? G.bz : (pbz + G.bz) >> 1;
            vf_px[0] = (ppx + G.px) >> 1; vf_py[0] = (ppy + G.py) >> 1;
        } else {
            vf_bx[0] = G.bx; vf_by[0] = G.by; vf_bz[0] = G.bz; vf_px[0] = G.px; vf_py[0] = G.py;
        }
        view_frame();
        work_t += a3d_since(t1);
        ++frames;
        if (hud_dirty) { hud(); hud_dirty = 0; }
        bb_probe[0] = state; bb_probe[1] = G.level; bb_probe[2] = G.lives; bb_probe[3] = autopilot;
        bb_probe[4] = G.over | (G.won << 1); bb_probe[5] = G.between; bb_probe[6] = paused; bb_probe[7] = start_level;
        bb_probe[8] = G.stuck; bb_probe[9] = G.left;
        if (frame_count - tsec >= 250) {
            lgu("bb: ticks5s=", ticks); lgu(" frames5s=", frames); lgu(" bricks=", bricks_t * 2); lgu(" work=", work_t / frames * 2); lg_end();
            ticks = frames = 0; work_t = 0;
            tsec = frame_count;
        }
    }

}
