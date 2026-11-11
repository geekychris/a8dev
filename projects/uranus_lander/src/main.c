/*
 * Uranus Lander for the Atari 8-bit - port of amiga_mcp/examples/uranus_lander.
 *
 * game.c is the original game (terrain, pads, physics, landing rules,
 * scoring, levels); view.c draws it; this file is the loop: a fixed 50 Hz
 * timestep (the logic runs once for every frame that passed, the screen is
 * drawn once), input, the autopilot, sound and the high-score table.
 *
 * Controls: joystick left/right rotate, FIRE or up thrust; OPTION or Z the
 * autopilot (on/off while flying), M the music.
 *
 * Log (tests): "uranus: ready", "uranus: level=N terrain=SUM pads=x,w,y,m;...",
 * "uranus: state=...", "uranus: landed ...", "uranus: crash ...",
 * "uranus: autopilot=N", "uranus: hiscore ...", "uranus: perf ...".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"
#include "autopilot.h"

static InputState in;
static uint8_t in_now, in_new;
static uint8_t autopilot, music_on = 1;

static const char *hs_name[5];
static char hs_names[5][4] = {"ACE", "ZAP", "BOB", "JOE", "TOM"};
static uint32_t hs_score[5] = {1000, 900, 800, 700, 600};   /* the original's top five */

/* ---- log (no printf: "key=" and a number) */
#ifdef A8_NO_LOG
#define LOG_S(s)      ((void)0)
#define LOG_N(k, v)   ((void)0)
#define LOG_END()     ((void)0)
#else
#define LOG_S(s)      a8_log_s(s)
#define LOG_END()     a8_log_c('\n')
static void LOG_N(const char *k, long v)
{
    char b[12];
    a8_log_s(k);
    ltoa(v, b, 10);
    a8_log_s(b);
}
static const char *const state_name[6] = {"TITLE", "PLAYING", "LANDED", "CRASHING", "GAMEOVER", "ENTER_NAME"};
#endif

static void log_state(void)
{
    LOG_S("uranus: state=");
    LOG_S(state_name[gs.state]);
    LOG_N(" level=", gs.level);
    LOG_N(" score=", gs.score);
    LOG_N(" lives=", gs.lives);
    LOG_END();
}

/* the level's terrain (sum of terrain_y[i] * (i + 1), 16 bits) and pads,
   for the test to check against the original's generator */
static void log_level(void)
{
#ifndef A8_NO_LOG
    uint16_t sum = 0, run = 0, i;
    for (i = TERRAIN_W; i--;) {
        run += (uint16_t)gs.terrain_y[i];
        sum += run;
    }
    LOG_N("uranus: level=", gs.level);
    LOG_N(" terrain=", (long)sum);
    LOG_S(" pads=");
    for (i = 0; i < (uint16_t)gs.num_pads; i++) {
        LandingPad *p = &gs.pads[i];
        LOG_N("", p->x);
        LOG_N(",", p->width);
        LOG_N(",", p->y);
        LOG_N(",", p->multiplier);
        LOG_S(";");
    }
    LOG_N(" gravity=", g_tune.gravity);
    LOG_N(" fuel=", g_tune.fuel_max);
    LOG_END();
#endif
}

/* ---- input: the SDK's mapping (joystick 0, console keys, W/A/S/D, SPACE) */
static uint8_t input_now(void)
{
    uint8_t s = STICK0, r = 0;
    if (!(s & 1)) r |= IN_UP;
    if (!(s & 2)) r |= IN_DOWN;
    if (!(s & 4)) r |= IN_LEFT;
    if (!(s & 8)) r |= IN_RIGHT;
    if (!STRIG0) r |= IN_FIRE;
    r |= (uint8_t)((~CONSOL & 7) << 5);
    if (!(SKSTAT & 4)) {
        switch (KBCODE & 0x3F) {
        case KEY_A: case 6:  r |= IN_LEFT; break;
        case KEY_D: case 7:  r |= IN_RIGHT; break;
        case KEY_W: case 14: r |= IN_UP; break;
        case KEY_S: case 15: r |= IN_DOWN; break;
        case KEY_SPACE:      r |= IN_FIRE; break;
        case KEY_RETURN:     r |= IN_START; break;
        }
    }
    return r;
}

static void read_input(void)
{
    uint8_t old = in_now;
    in_now = input_now();
    in_new = (uint8_t)(in_now & ~old);
}

#define KEY_M 37

/* ---- high scores */
static int8_t hs_rank(uint32_t score)
{
    int8_t i;
    for (i = 0; i < 5; ++i)
        if (score > hs_score[i]) return i;
    return -1;
}

static void enter_name(int8_t rank)
{
    char name[4] = "A__";
    char shown[8];
    uint8_t pos = 0, rep = 0, k, i;
    LOG_N("uranus: initials rank=", rank + 1);
    LOG_END();
    key_get();
    while (pos < 3) {
        frame_wait();
        read_input();
        k = key_get();
        if (k != KEY_NONE) {
            char c = key_ascii(k);
            if (c >= 'A' && c <= 'Z') {
                name[pos++] = c;
                if (pos < 3) name[pos] = 'A';
            }
        } else if (in_now & (IN_UP | IN_DOWN)) {
            if (!rep) {
                char c = name[pos];
                if (in_now & IN_UP) c = c >= 'Z' ? 'A' : c + 1;
                else c = c <= 'A' ? 'Z' : c - 1;
                name[pos] = c;
                rep = 8;
            } else {
                --rep;
            }
        } else {
            rep = 0;
        }
        if (in_new & (IN_FIRE | IN_RIGHT | IN_START)) {
            ++pos;
            if (pos < 3) name[pos] = 'A';
        }
        for (i = 0; i < 3; ++i) {
            shown[i * 2] = name[i];
            shown[i * 2 + 1] = ' ';
        }
        shown[5] = 0;
        view_msg("NEW HIGH SCORE", shown, 2);
    }
    for (i = 4; i > (uint8_t)rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_score[i] = hs_score[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_score[rank] = (uint32_t)gs.score;
    LOG_N("uranus: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_N(" score=", gs.score);
    LOG_END();
}

/* ---- the title */
static void title(void)
{
    static uint8_t first = 1;
    uint8_t t = 0, i;
    for (i = 0; i < 5; ++i) hs_name[i] = hs_names[i];
    autopilot = 0;
    snd_quiet();
    snd_music(music_on);
    view_title(hs_name, hs_score);
    gs.state = STATE_TITLE;
    log_state();
    if (first) {
        LOG_S("uranus: ready\n");
        first = 0;
    }
    in_now = input_now();
    for (;;) {
        frame_wait();
        view_title_frame(++t);
        snd_frame(0, 0, 0, 0);
        read_input();
        if (key_peek() == KEY_M) {
            key_get();
            snd_music(music_on ^= 1);
        }
        if (t > 15 && (in_new & (IN_FIRE | IN_UP | IN_START))) break;
    }
}

/* ---- a game */
static void enter_state(WORD prev)
{
    log_state();
    switch (gs.state) {
    case STATE_PLAYING:
        if (prev == STATE_LANDED) {
            view_world();               /* the next level */
            view_hud_reset();
            log_level();
            autopilot_reset();
        }
        view_msg(0, 0, 0);
        break;
    case STATE_LANDED: {
        char a[12] = "LANDED  X0";
        char b[12] = "BONUS ";
        a[9] = (char)('0' + gs.pads[gs.landed_pad].multiplier);
        ltoa(gs.land_bonus, b + 6, 10);
        view_msg(a, b, 2);                 /* yellow */
        LOG_N("uranus: landed level=", gs.level);
        LOG_N(" pad=", gs.landed_pad);
        LOG_N(" x", gs.pads[gs.landed_pad].multiplier);
        LOG_N(" bonus=", gs.land_bonus);
        LOG_N(" fuel=", gs.ship.fuel);
        LOG_N(" score=", gs.score);
        LOG_END();
        break;
    }
    case STATE_CRASHING:
        view_msg("CRASHED", gs.lives > 1 ? "" : "LAST SHIP LOST", 3);   /* red */
        LOG_N("uranus: crash level=", gs.level);
        LOG_N(" score=", gs.score);
        LOG_N(" lives=", gs.lives);
        LOG_END();
        break;
    case STATE_GAMEOVER:
        view_msg("GAME OVER", "PRESS FIRE", 3);
        LOG_N("uranus: gameover score=", gs.score);
        LOG_END();
        break;
    }
}

static void play(void)
{
    uint16_t perf_frames = 0, perf_ticks = 0, perf_draws = 0;
    uint8_t ticks, t;
    int8_t r;

    game_init();
    gs.state = STATE_PLAYING;
    autopilot = 0;
    autopilot_reset();
    view_game();
    view_autopilot(0);
    log_state();
    log_level();
    in_now = input_now();
    frame_wait();
    for (;;) {
        uint8_t thrust = 0, crash = 0, land = 0, low = 0;
        ticks = frame_wait();
        view_frame();
        ++perf_draws;
        perf_frames += ticks;
        if (ticks > 4) ticks = 4;
        read_input();
        if (gs.state == STATE_PLAYING &&
            ((in_new & IN_OPTION) || key_peek() == KEY_Z)) {
            if (key_peek() == KEY_Z) key_get();
            autopilot ^= 1;
            autopilot_reset();
            view_autopilot(autopilot);
            LOG_N("uranus: autopilot=", autopilot);
            LOG_END();
        }
        if (key_peek() == KEY_M) {
            key_get();
            snd_music(music_on ^= 1);
        }
        for (t = 0; t < ticks; ++t) {
            WORD prev = gs.state;
            in.left = (in_now & IN_LEFT) != 0;
            in.right = (in_now & IN_RIGHT) != 0;
            in.thrust = (in_now & IN_FIRE) != 0;
            in.up = (in_now & IN_UP) != 0;
            if (gs.state == STATE_GAMEOVER) in.thrust = (in_new & IN_FIRE) != 0, in.up = 0;
            if (autopilot && gs.state == STATE_PLAYING) autopilot_fly(&in);
            game_update(&in);
            ++perf_ticks;
            thrust |= gs.ev_thrust;
            crash |= gs.ev_crash;
            land |= gs.ev_land;
            low |= gs.ev_low_fuel;
            if (gs.state != prev) {
                if (gs.state == STATE_ENTER_NAME) {
                    snd_quiet();
                    r = hs_rank((uint32_t)gs.score);
                    if (r >= 0) enter_name(r);
                    return;
                }
                enter_state(prev);
                break;
            }
        }
        snd_frame(thrust, crash, land, low);
        if (perf_frames >= 100) {
            LOG_N("uranus: perf frames=", perf_frames);
            LOG_N(" ticks=", perf_ticks);
            LOG_N(" draws=", perf_draws);
            LOG_END();
            perf_frames = perf_ticks = perf_draws = 0;
        }
    }
}

int main(void)
{
    snd_init();
    view_init();
    frame_init();
    for (;;) {
        title();
        play();
    }
}
