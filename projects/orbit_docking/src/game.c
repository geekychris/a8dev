/*
 * game.c - Orbit Docking for the Atari 8-bit: the shell round the rules
 * (od_core.c) and the view (view.c).
 *
 * The rules run the original's 25 ticks a second (every second PAL
 * frame); the view is drawn as often as it can be.
 *
 * Controls: the stick moves the craft across (its own left, right, up,
 * down); with FIRE held, up/down go in and out and left/right roll. Keys:
 * the arrows across, W / S in and out, A / D roll. START (or P) pauses, B
 * the autopilot, ESC the title. Title: left/right or up/down choose the
 * station, FIRE or START flies.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "od.h"
#include "view.h"

#pragma bss-name ("LOWRAM")

extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held, key_held;
extern uint8_t key_take(void);

enum { GS_TITLE, GS_PLAY, GS_OVER };
#define KEY_B 21

static uint8_t state, start_level = 1, autopilot, paused, state_timer, hud_dirty = 1, msg_timer, fire_lock, thrust_t;
static uint16_t best, next_seed = 1;
static const char *msg;
static char msgbuf[40];
/* for the test: state, level, lives, autopilot, over | won << 1, between, paused, the title's station, docks, crashes, last_fail */
uint8_t od_probe[11];

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

static void inverse(uint8_t pos, uint8_t w)
{
    uint8_t *p = A3D_TEXT + pos;
    while (w--) *p++ |= 0x80;
}

static void hud(void)
{
    int16_t range, close, spin;
    uint8_t k, f;
    if (state == GS_TITLE) {
        put(0, " ORBIT DOCKING             BEST ", 32);
        numw(32, best, 5);
        put(37, "", 3);
        put(40, " STATION <", 10);
        numw(50, start_level, 1);
        put(51, ">           FIRE TO FLY", 29);
        return;
    }
    range = (G.z >> 4) - (OD_PORT_Z + OD_NOSE);
    close = (-G.vz * 10) >> 8;                          /* tenths a tick */
    put(0, " STATION", 9);
    numw(9, G.level, 1);
    put(10, " RANGE", 7);
    numw(17, range < 0 ? 0 : (uint16_t)range, 4);
    put(21, " CLOSE ", 7);
    A3D_TEXT[28] = close < 0 ? 0x0D : 0;               /* '-' */
    numw(29, (uint16_t)(close < 0 ? -close : close) / 10, 1);
    A3D_TEXT[30] = 0x0E;                                /* '.' */
    numw(31, (uint16_t)(close < 0 ? -close : close) % 10, 1);
    if (range < 150 && -G.vz > OD_TOL_CLOSE) inverse(28, 4);   /* too fast near the port */
    put(32, " ", 1);
    numw(33, G.score, 5);
    put(38, "", 2);
    if (state == GS_OVER) {
        put(40, G.won ? " ALL DOCKED " : " GAME OVER  ", 12);
        numw(52, G.docks, 1);
        put(53, " DOCKED ", 8);
        numw(61, G.crashes, 1);
        put(62, " CRASHED ", 9);
        put(71, state_timer ? "" : "FIRE", 9);
        return;
    }
    if (paused) { put(40, " PAUSED", 40); return; }
    if (msg_timer) { put(40, " ", 1); put(41, msg, 39); return; }
    if (G.between) { put(40, " DOCKED!", 40); return; }
    put(40, " FUEL ", 6);
    f = (uint8_t)(G.fuel * 10u / G.fuel_max);
    for (k = 0; k < 10; ++k) A3D_TEXT[46 + k] = k < f ? 0x80 : 0x0E;
    spin = (int16_t)((G.roll_rate - G.st_spin) >> 8);
    put(56, spin < -20 ? "  << TURN" : spin > 20 ? "  TURN >>" : "  MATCHED", 9);
    put(65, "  ", 2);
    for (k = 0; k < 3; ++k) A3D_TEXT[67 + k] = k < G.lives ? 0x54 + 0x80 : 0;   /* the lives: inverse T */
    put(70, autopilot ? "    AUTO" : "", 10);
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
    od_new_game(777 + next_seed, 1);
    autopilot = 1;
    paused = 0;
    hud_dirty = 1;
    lg("od: title"); lg_end();
}

static void start_game(void)
{
    state = GS_PLAY;
    od_new_game(next_seed++, start_level);
    autopilot = 0;
    paused = 0;
    fire_lock = 1;
    hud_dirty = 1;
    lgu("od: start station=", start_level); lg_end();
}

static void events(void)
{
    uint8_t e, a;
    uint16_t b;
    while ((e = od_pop_event(&a, &b)) != ODE_NONE) {
        if (state == GS_TITLE) {
            if (e == ODE_OVER || e == ODE_WIN) state_timer = 50;
            continue;
        }
        switch (e) {
        case ODE_DOCK:
            snd_sfx(2, 60, SND_PURE, 14, 1, -2);
            hud_dirty = 1;
            lgu("od: dock station=", a); lgu(" bonus=", b); lgu(" score=", G.score); lg_end();
            break;
        case ODE_CRASH:
        case ODE_DEBRIS:
            snd_sfx(1, 40, SND_NOISE, 15, 1, 2);
            strcpy(msgbuf, "CRASH: ");
            strcat(msgbuf, od_fail_names[a]);
            flash(msgbuf);
            lgu("od: crash why=", a); lgu(" lives=", G.lives); lg_end();
            break;
        case ODE_FUEL_OUT: snd_sfx(2, 35, SND_PURE, 12, 1, 0); flash("OUT OF FUEL"); lg("od: fuel out"); lg_end(); break;
        case ODE_LEVEL: hud_dirty = 1; lgu("od: station=", a); lg_end(); break;
        case ODE_OVER:
        case ODE_WIN:
            state = GS_OVER;
            state_timer = 50;
            if (G.score > best) best = G.score;
            snd_sfx(2, e == ODE_WIN ? 30 : 70, SND_PURE, 14, 1, e == ODE_WIN ? -1 : 6);
            lgu("od: gameover score=", G.score); lgu(" won=", G.won); lgu(" docks=", G.docks); lg_end();
            hud_dirty = 1;
            break;
        }
    }
}

static uint8_t input(void)
{
    uint8_t held = in_held, keys = key_held, edge = in_take(), k = key_take(), in = 0;
    switch (state) {
    case GS_TITLE:
        if ((edge & (IN_RIGHT | IN_UP)) && start_level < OD_LEVELS) { ++start_level; hud_dirty = 1; }
        if ((edge & (IN_LEFT | IN_DOWN)) && start_level > 1) { --start_level; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case GS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; hud_dirty = 1; lgu("od: paused=", paused); lg_end(); }
        if (k == KEY_ESC) { to_title(); break; }
        if (k == KEY_B) { autopilot ^= 1; hud_dirty = 1; lgu("od: autopilot=", autopilot); lg_end(); }
        if (!(held & IN_FIRE)) fire_lock = 0;
        if ((held & IN_FIRE) && !fire_lock)     /* fire shifts the stick: up/down in and out, left/right roll */
            in = (held & IN_UP ? OD_IN_FWD : 0) | (held & IN_DOWN ? OD_IN_BACK : 0) |
                 (held & IN_LEFT ? OD_IN_ROLL_L : 0) | (held & IN_RIGHT ? OD_IN_ROLL_R : 0);
        else
            in = (held & IN_LEFT ? OD_IN_LEFT : 0) | (held & IN_RIGHT ? OD_IN_RIGHT : 0) |
                 (held & IN_UP ? OD_IN_UP : 0) | (held & IN_DOWN ? OD_IN_DOWN : 0);
        in |= (keys & 1 ? OD_IN_FWD : 0) | (keys & 2 ? OD_IN_BACK : 0) | (keys & 4 ? OD_IN_ROLL_L : 0) | (keys & 8 ? OD_IN_ROLL_R : 0);
        break;
    case GS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return in;
}

int main(void)
{
    uint8_t n, in, ticks = 0, frames = 0, hud_t = 0;
    uint16_t tsec = 0, t0, draw_t = 0, ms = 0;

    view_init();
    sound_init();
    in_install();
    frame_init();
    to_title();
    lg("od: ready"); lg_end();
    for (;;) {
        n = a3d_sync();
        in = input();
        ms += n * (frame_is_pal() ? 20 : 17);
        if (ms > 320) ms = 320;                         /* port: at most 8 ticks a pass (the original 4) */
        while (ms >= 40) {
            ms -= 40;
            if (state == GS_OVER) {
                if (state_timer && !--state_timer) hud_dirty = 1;
                continue;
            }
            if (paused) continue;
            if (autopilot) in = od_bot();
            od_tick(in);
            if (in && G.fuel && state == GS_PLAY && !thrust_t) { snd_sfx(0, 20, SND_NOISE, 5, 1, 0); thrust_t = 6; }
            if (thrust_t) --thrust_t;
            events();
            if (state == GS_TITLE && state_timer && !--state_timer) od_new_game(777 + next_seed, 1);
            if (msg_timer && !--msg_timer) hud_dirty = 1;
            ++ticks;
            if (++hud_t >= 5) { hud_t = 0; hud_dirty = 1; }   /* range and closing rate, 5 times a second */
        }
        t0 = a3d_ticks();
        view_draw();
        draw_t += a3d_since(t0);
        a3d_show();
        ++frames;
        view_instruments();
        if (hud_dirty) { hud(); hud_dirty = 0; }
        od_probe[0] = state; od_probe[1] = G.level; od_probe[2] = G.lives; od_probe[3] = autopilot;
        od_probe[4] = G.over | (G.won << 1); od_probe[5] = G.between; od_probe[6] = paused; od_probe[7] = start_level;
        od_probe[8] = G.docks; od_probe[9] = G.crashes; od_probe[10] = G.last_fail;
        if (frame_count - tsec >= 250) {
            lgu("od: ticks5s=", ticks); lgu(" frames5s=", frames); lgu(" draw=", draw_t / frames * 2); lg_end();
            ticks = frames = 0; draw_t = 0;
            tsec = frame_count;
        }
    }
}
