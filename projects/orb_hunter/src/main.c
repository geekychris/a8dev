/*
 * Orb Hunter for the Atari 8-bit - port of amiga_mcp/examples/orb_hunter.
 *
 * game.c is the original's rules; view.c draws them; this is the loop (a
 * fixed 50 Hz step, the original's: the rules run once for every frame
 * that passed, the screen is drawn once), input, the title, the item and
 * game over screens, and the high scores (new: a table of five).
 *
 * Controls: joystick left/right run, up jump (up + FIRE: a rocket, once
 * found), down roll (once found), FIRE shoot (rolled: a bomb).
 * Keys: A/D run, W jump, S roll, SPACE fire.
 *
 * Log (tests): "orb: ready", "orb: start room=2,2", "orb: room x,y",
 * "orb: item NAME", "orb: score=N", "orb: hit health=N", "orb: dead",
 * "orb: gameover score=N", "orb: hiscore ...", "orb: perf ...".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

static uint8_t in_now, in_new;
static char hs_names[5][4] = {"ORB", "ZAP", "MOL", "TEK", "GEO"};
static uint32_t hs_score[5] = {800, 600, 400, 200, 100};

/* for the test's bot (emu.read_mem("orb_probe", 20)) */
typedef struct {
    uint8_t state, room_x, room_y, alive;
    int16_t hx, hy, health;
    int32_t score;
    uint8_t enemies, rolled, facing, on_ground, invuln;
    int16_t ex, ey;
} probe_t;
probe_t orb_probe;

/* ---- log */
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
#endif

static void probe(void)
{
#ifndef A8_NO_LOG
    uint8_t i, n = 0;
    orb_probe.state = (uint8_t)gs.state;
    orb_probe.room_x = (uint8_t)gs.room_x;
    orb_probe.room_y = (uint8_t)gs.room_y;
    orb_probe.alive = h_alive;
    orb_probe.hx = FIX_INT(h_x);
    orb_probe.hy = FIX_INT(h_y);
    orb_probe.health = h_health;
    orb_probe.score = gs.score;
    orb_probe.rolled = h_rolled;
    orb_probe.facing = h_facing;
    orb_probe.on_ground = h_on_ground;
    orb_probe.invuln = h_invuln_timer;
    for (i = 0; i < MAX_ENEMIES; ++i)
        if (e_on[i]) {
            if (!n) {
                orb_probe.ex = FIX_INT(e_x[i]);
                orb_probe.ey = FIX_INT(e_y[i]);
            }
            ++n;
        }
    orb_probe.enemies = n;
#endif
}

/* ---- input: joystick 0, the console keys, W/A/S/D and SPACE */
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
    char shown[20];
    uint8_t pos = 0, rep = 0, k, i;
    LOG_N("orb: initials rank=", rank + 1);
    LOG_END();
    key_get();
    while (pos < 3) {
        frame_wait();
        snd_tick();
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
        memcpy(shown, "HIGH SCORE  ", 12);
        for (i = 0; i < 3; ++i) shown[12 + i] = name[i];
        shown[15] = 0;
        view_message(shown);
    }
    for (i = 4; i > (uint8_t)rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_score[i] = hs_score[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_score[rank] = (uint32_t)gs.score;
    LOG_N("orb: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_N(" score=", gs.score);
    LOG_END();
}

/* ---- the title */
static void title(void)
{
    static uint8_t first = 1;
    uint8_t t = 0;
    snd_quiet();
    gs.state = STATE_TITLE;
    view_title(hs_names, hs_score);
    if (first) {
        LOG_S("orb: ready\n");
        first = 0;
    } else {
        LOG_S("orb: title\n");
    }
    in_now = input_now();
    for (;;) {
        frame_wait();
        view_title_frame(++t);
        read_input();
        if (t > 15 && (in_new & (IN_FIRE | IN_START))) break;
    }
}

/* ---- a game */
static void play(void)
{
    uint16_t perf_frames = 0, perf_ticks = 0, perf_draws = 0;
    uint8_t ticks, t;
    int8_t last_rx = -1, last_ry = -1;
    int32_t last_score = 0;
    WORD last_health;
    int8_t r;

    game_init();
    view_game();
    snd_music(1);
    LOG_S("orb: start room=2,2\n");
    last_rx = gs.room_x;
    last_ry = gs.room_y;
    last_health = h_health;
    in_now = input_now();
    frame_wait();
    for (;;) {
        ticks = frame_wait();
        view_frame();
        snd_tick();
        ++perf_draws;
        perf_frames += ticks;
        if (ticks > 8) ticks = 8;           /* (a room takes ~5 frames to draw) */
        read_input();
        for (t = 0; t < ticks; ++t) {
            WORD prev = gs.state;
            game_update((in_now & IN_LEFT) != 0, (in_now & IN_RIGHT) != 0, (in_now & IN_UP) != 0,
                        (in_now & IN_DOWN) != 0, (in_now & IN_UP) != 0, (in_now & IN_FIRE) != 0);
            ++perf_ticks;
            if (gs.state != prev) {
                switch (gs.state) {
                case STATE_ITEM_GET:
                    view_message(gs.item_name);
                    LOG_S("orb: item ");
                    LOG_S(gs.item_name);
                    LOG_END();
                    break;
                case STATE_PLAYING:
                    view_message(0);
                    break;
                case STATE_DEAD:
                    view_message("ENERGY DEPLETED");
                    LOG_S("orb: dead\n");
                    break;
                case STATE_GAMEOVER: {
                    /* the score with it (the original's fix: never shown) */
                    static char go[17] = "GAME OVER ";
                    dec6(go + 10, (uint32_t)gs.score);
                    view_message(go);
                    snd_music(0);
                    LOG_N("orb: gameover score=", gs.score);
                    LOG_END();
                    break;
                }
                }
                break;
            }
            /* port: game over waits for a new press of fire (or its time) */
            if (gs.state == STATE_GAMEOVER && gs.state_timer < 130 && (in_new & IN_FIRE))
                gs.state = STATE_TITLE;
            /* the item's message: its 90 frames, or a new press of fire
               (the original's fix) */
            if (gs.state == STATE_ITEM_GET && (in_new & (IN_FIRE | IN_START))) {
                gs.state = STATE_PLAYING;
                view_message(0);
            }
        }
        if (gs.state == STATE_TITLE) {
            view_hide();
            snd_quiet();
            r = hs_rank((uint32_t)gs.score);
            if (r >= 0) enter_name(r);
            return;
        }
        if (gs.room_x != last_rx || gs.room_y != last_ry) {
            last_rx = gs.room_x;
            last_ry = gs.room_y;
            LOG_N("orb: room ", gs.room_x);
            LOG_N(",", gs.room_y);
            LOG_END();
        }
        if (gs.score != last_score) {
            last_score = gs.score;
            LOG_N("orb: score=", gs.score);
            LOG_END();
        }
        if (h_health < last_health) {
            LOG_N("orb: hit health=", h_health);
            LOG_END();
        }
        last_health = h_health;
        probe();
        if (perf_frames >= 100) {
            LOG_N("orb: perf frames=", perf_frames);
            LOG_N(" ticks=", perf_ticks);
            LOG_N(" draws=", perf_draws);
            LOG_N(" room=", gs.room_y * WORLD_W + gs.room_x);
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
