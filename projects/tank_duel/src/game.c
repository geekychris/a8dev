/*
 * game.c - Tank Duel for the Atari 8-bit: the shell round the rules
 * (td_core.c) and the view (view.c).
 *
 * The rules run the original's 25 ticks a second (every second PAL
 * frame); the view is drawn as often as it can be.
 *
 * Controls: the stick (W A S D, the arrows) drives: up / down forward and
 * back, left / right turn; FIRE (SPACE) fires. START (or P) pauses, B the
 * autopilot, ESC the title. Title: left/right or up/down choose the round,
 * FIRE or START fights.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "td.h"
#include "view.h"

#pragma bss-name ("LOWRAM")

extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held;
extern uint8_t key_take(void);
extern uint8_t bank_ok(void);

enum { GS_TITLE, GS_PLAY, GS_OVER };
#define KEY_B 21

static uint8_t state, start_round = 1, autopilot, paused, state_timer, hud_dirty = 1, msg_timer, fire_lock, hurt_t;
static uint16_t best, next_seed = 1;
static const char *msg;
/* for the test: state, round, armour, autopilot, over | won << 1, between, paused, the title's round, kills, enemies left */
uint8_t td_probe[10];

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

static void bar(uint8_t pos, uint8_t full, uint8_t w)
{
    uint8_t k;
    for (k = 0; k < w; ++k) A3D_TEXT[pos + k] = k < full ? 0x80 : 0x0E;
}

static void hud(void)
{
    if (state == GS_TITLE) {
        put(0, " TANK DUEL                 BEST ", 32);
        numw(32, best, 5);
        put(37, "", 3);
        put(40, " ROUND <", 8);
        numw(48, start_round, 1);
        put(49, ">             FIRE TO FIGHT", 31);
        return;
    }
    put(0, " ROUND", 7);
    numw(7, G.round, 1);
    put(8, "  ENEMIES", 10);
    numw(18, td_enemies_left(), 1);
    put(19, "        SCORE ", 14);
    numw(33, G.score, 5);
    put(38, "", 2);
    if (state == GS_OVER) {
        put(40, G.won ? " VICTORY!  " : " GAME OVER ", 11);
        numw(51, G.kills, 2);
        put(53, " KILLS   ", 9);
        put(62, state_timer ? "" : "FIRE", 18);
        return;
    }
    if (paused) { put(40, " PAUSED", 40); return; }
    if (msg_timer) { put(40, " ", 1); put(41, msg, 39); return; }
    if (G.between) { put(40, " ROUND CLEAR!", 40); return; }
    put(40, " ARMOUR ", 8);
    bar(48, (uint8_t)(G.t_hp[0] / 10), 10);
    put(58, "  GUN ", 6);
    bar(64, (uint8_t)((TD_RELOAD - G.t_reload[0]) / 5), 5);
    put(69, autopilot ? "    AUTO" : "", 11);
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
    td_new_game(777 + next_seed, 1);
    autopilot = 1;
    paused = 0;
    hud_dirty = 1;
    lg("td: title"); lg_end();
}

static void start_game(void)
{
    state = GS_PLAY;
    td_new_game(next_seed++, start_round);
    autopilot = 0;
    paused = 0;
    fire_lock = 1;
    hud_dirty = 1;
    lgu("td: start round=", start_round); lg_end();
}

static const char *const down_msg[4] = { "", "SCOUT DOWN", "MORTAR DOWN", "BULWARK DOWN" };

static void events(void)
{
    uint8_t e, a, b;
    int16_t x, z;
    while ((e = td_pop_event(&a, &b, &x, &z)) != TDE_NONE) {
        if (e == TDE_KILL) view_blast(x, z, 60);
        if (e == TDE_HIT || e == TDE_CRATE_BREAK) view_blast(x, z, 30);
        if (e == TDE_SPLASH) view_blast(x, z, a == 2 ? 70 : 16);
        if (state == GS_TITLE) {
            if (e == TDE_DEAD || e == TDE_WIN) state_timer = 50;
            continue;
        }
        switch (e) {
        case TDE_FIRE: snd_sfx(0, 40, SND_NOISE, 14, 2, 1); hud_dirty = 1; lg("td: fire"); lg_end(); break;
        case TDE_EFIRE: snd_sfx(0, 60, SND_NOISE, 9, 2, 1); break;
        case TDE_LOB: snd_sfx(1, 90, SND_PURE, 8, 1, -2); flash("INCOMING MORTAR"); break;
        case TDE_KILL:
            snd_sfx(1, 30, SND_NOISE, 15, 1, 2);
            flash(down_msg[b]);
            lgu("td: kill type=", b); lgu(" score=", G.score); lg_end();
            break;
        case TDE_HIT: snd_sfx(1, 50, SND_NOISE, 12, 1, 3); lgu("td: hit tank=", a); lg_end(); break;
        case TDE_GLANCE: snd_sfx(2, 20, SND_PURE, 12, 1, 1); flash("GLANCED OFF THE SHIELD"); lg("td: glance"); lg_end(); break;
        case TDE_BOUNCE: snd_sfx(2, 30, SND_PURE, 8, 2, 2); break;
        case TDE_HURT:
            snd_sfx(1, 70, SND_NOISE, 15, 1, 4);
            hurt_t = 8;
            hud_dirty = 1;
            lgu("td: hurt armour=", b); lg_end();
            break;
        case TDE_SPLASH: if (a == 2) snd_sfx(1, 40, SND_NOISE, 13, 1, 3); break;
        case TDE_CRATE_HIT: case TDE_CRATE_BREAK: snd_sfx(1, 60, SND_NOISE, 10, 2, 0); hud_dirty = 1; break;
        case TDE_ROUND_CLEAR:
            snd_sfx(2, 60, SND_PURE, 14, 1, -2);
            hud_dirty = 1;
            lgu("td: clear round=", a); lgu(" score=", G.score); lg_end();
            break;
        case TDE_ROUND: hud_dirty = 1; lgu("td: round=", a); lg_end(); break;
        case TDE_DEAD:
        case TDE_WIN:
            state = GS_OVER;
            state_timer = 50;
            if (G.score > best) best = G.score;
            snd_sfx(2, e == TDE_WIN ? 30 : 80, SND_PURE, 14, 1, e == TDE_WIN ? -1 : 6);
            lgu("td: gameover score=", G.score); lgu(" won=", G.won); lgu(" kills=", G.kills); lg_end();
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
        if ((edge & (IN_RIGHT | IN_UP)) && start_round < TD_ROUNDS) { ++start_round; hud_dirty = 1; }
        if ((edge & (IN_LEFT | IN_DOWN)) && start_round > 1) { --start_round; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case GS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; hud_dirty = 1; lgu("td: paused=", paused); lg_end(); }
        if (k == KEY_ESC) { to_title(); break; }
        if (k == KEY_B) { autopilot ^= 1; hud_dirty = 1; lgu("td: autopilot=", autopilot); lg_end(); }
        if (!(held & IN_FIRE)) fire_lock = 0;   /* the fire that started the game doesn't also shoot */
        in = (held & IN_LEFT ? TD_IN_LEFT : 0) | (held & IN_RIGHT ? TD_IN_RIGHT : 0) |
             (held & IN_UP ? TD_IN_UP : 0) | (held & IN_DOWN ? TD_IN_DOWN : 0) |
             ((edge | held) & IN_FIRE && !fire_lock ? TD_IN_FIRE : 0);
        break;
    case GS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return in;
}

int main(void)
{
    uint8_t n, in, ticks = 0, frames = 0;
    uint16_t tsec = 0, t0, draw_t = 0, ms = 0;

    view_init();
    if (!bank_ok()) {                                   /* the rules live in a 130XE's bank */
        put(0, " TANK DUEL NEEDS A 130XE (128K)", 40);
        put(40, " (THE RULES LIVE IN ITS EXTRA BANK)", 40);
        lg("td: needs 130xe"); lg_end();
        for (;;) a3d_sync();
    }
    sound_init();
    in_install();
    frame_init();
    to_title();
    lg("td: ready"); lg_end();
    for (;;) {
        n = a3d_sync();
        in = input();
        ms += n * (frame_is_pal() ? 20 : 17);
        if (ms > 320) ms = 320;                         /* port: at most 8 ticks a pass */
        while (ms >= 40) {
            ms -= 40;
            if (state == GS_OVER) {
                if (state_timer && !--state_timer) hud_dirty = 1;
                continue;
            }
            if (paused) continue;
            if (autopilot) in = td_bot();
            td_tick(in);
            in &= ~TD_IN_FIRE;
            events();
            if (state == GS_TITLE && state_timer && !--state_timer) td_new_game(777 + next_seed, 1);
            if (msg_timer && !--msg_timer) hud_dirty = 1;
            if (G.t_reload[0]) hud_dirty = 1;           /* the gun bar fills */
            ++ticks;
        }
        t0 = a3d_ticks();
        view_draw();
        draw_t += a3d_since(t0);
        a3d_show();
        ++frames;
        view_gun(G.t_reload[0] > TD_RELOAD - 4);
        a3d_hud_c2 = hurt_t ? 0x34 : 0x02;              /* hurt: the text rows flash red */
        if (hurt_t) --hurt_t;
        if (hud_dirty) { hud(); hud_dirty = 0; }
        td_probe[0] = state; td_probe[1] = G.round; td_probe[2] = (uint8_t)G.t_hp[0]; td_probe[3] = autopilot;
        td_probe[4] = G.over | (G.won << 1); td_probe[5] = G.between; td_probe[6] = paused; td_probe[7] = start_round;
        td_probe[8] = (uint8_t)G.kills; td_probe[9] = td_enemies_left();
        if (frame_count - tsec >= 250) {
            lgu("td: ticks5s=", ticks); lgu(" frames5s=", frames); lgu(" draw=", draw_t / frames * 2); lg_end();
            ticks = frames = 0; draw_t = 0;
            tsec = frame_count;
        }
    }
}
