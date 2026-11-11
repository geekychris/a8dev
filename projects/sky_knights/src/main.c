/*
 * Sky Knights for the Atari 8-bit - port of amiga_mcp/examples/sky_knights.
 *
 * game.c is the original's rules; view.c draws them; this is the loop (the
 * rules at a fixed 50 Hz: once for every frame that passed, the screen
 * drawn once), the title (1 or 2 players), sound, the high scores (new).
 *
 * Controls: joystick 1 (port 1) is player 1, joystick 2 player 2: left /
 * right fly, FIRE or up flaps. In a one-player game the keys work too
 * (A / D, W or SPACE). Title: up / down (or 1 / 2) picks, FIRE starts.
 *
 * Log (tests): "knights: ready", "knights: start players=N",
 * "knights: wave N", "knights: kill score=N", "knights: pod score=N",
 * "knights: died lives=N", "knights: gameover score=N",
 * "knights: hiscore ...", "knights: perf ...".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

static uint8_t in_now, in_new;
static char hs_names[5][4] = {"ACE", "SKY", "JOU", "KNT", "LAN"};
static uint32_t hs_score[5] = {10000, 7500, 5000, 2500, 1000};

static uint16_t t_view, t_game;     /* (the perf log's; 0 in RELEASE) */

#ifdef A8_NO_LOG
#define LOG_S(s)      ((void)0)
#define LOG_N(k, v)   ((void)0)
#define LOG_END()     ((void)0)
#define T_START()     ((void)0)
#define T_ADD(acc)    ((void)0)
#else
#define LOG_S(s)      a8_log_s(s)
#define LOG_END()     a8_log_c('\n')
/* the time spent drawing and in the rules, for the perf log: in units of 2
   scan lines (228 cycles), from the frame clock and VCOUNT (the VBI that
   moves RTCLOK2 comes at VCOUNT 124) */
static uint8_t f0, v0;
static void now2(uint8_t *f, uint8_t *v)
{
    do {
        *f = RTCLOK2;
        *v = VCOUNT;
    } while (*f != RTCLOK2);
    *v = (uint8_t)(*v >= 124 ? *v - 124 : *v + 32);
}
static void t_add(uint16_t *acc)
{
    uint8_t f, v;
    now2(&f, &v);
    *acc += v;
    *acc -= v0;
    for (f -= f0; f; --f) *acc += 156;
}
#define T_START()     now2(&f0, &v0)
#define T_ADD(acc)    t_add(&acc)

static void LOG_N(const char *k, long v)
{
    char b[12];
    a8_log_s(k);
    ltoa(v, b, 10);
    a8_log_s(b);
}
#endif

/* ---- input */
static uint8_t stick(uint8_t s, uint8_t trig)
{
    uint8_t r = 0;
    if (!(s & 1)) r |= INP_FLAP;            /* up flaps too */
    if (!(s & 4)) r |= INP_LEFT;
    if (!(s & 8)) r |= INP_RIGHT;
    if (!trig) r |= INP_FLAP;
    return r;
}

static uint8_t keys(void)
{
    if (SKSTAT & 4) return 0;
    switch (KBCODE & 0x3F) {
    case KEY_A: case 6: return INP_LEFT;
    case KEY_D: case 7: return INP_RIGHT;
    case KEY_W: case 14: case KEY_SPACE: return INP_FLAP;
    }
    return 0;
}

/* the title's / menus' input (joystick 1, console, keys) */
static uint8_t menu_now(void)
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
        case KEY_W: case 14: r |= IN_UP; break;
        case KEY_S: case 15: r |= IN_DOWN; break;
        case KEY_SPACE: r |= IN_FIRE; break;
        case KEY_RETURN: r |= IN_START; break;
        }
    }
    return r;
}

static void read_menu(void)
{
    uint8_t old = in_now;
    in_now = menu_now();
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

static void enter_name(int8_t rank, uint32_t score)
{
    char name[4] = "A__";
    char shown[8];
    uint8_t pos = 0, rep = 0, k, i;
    LOG_N("knights: initials rank=", rank + 1);
    LOG_END();
    key_get();
    while (pos < 3) {
        frame_wait();
        read_menu();
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
        view_msg("NEW HIGH SCORE", shown);
    }
    for (i = 4; i > (uint8_t)rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_score[i] = hs_score[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_score[rank] = score;
    LOG_N("knights: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_N(" score=", (long)score);
    LOG_END();
}

/* ---- the title: 1 or 2 players */
static uint8_t title(void)
{
    static uint8_t first = 1;
    uint8_t t = 0, sel = 0, k;
    snd_quiet();
    gs.state = STATE_TITLE;
    view_title(hs_names, hs_score, sel);
    if (first) {
        LOG_S("knights: ready\n");
        first = 0;
    } else {
        LOG_S("knights: title\n");
    }
    in_now = menu_now();
    for (;;) {
        frame_wait();
        view_title_frame(++t);
        read_menu();
        k = key_get();
        if (k == KEY_1 || k == KEY_2) return k == KEY_1 ? 1 : 2;
        if (in_new & (IN_UP | IN_DOWN | IN_SELECT)) {
            sel ^= 1;
            view_title(hs_names, hs_score, sel);
        }
        if (t > 15 && (in_new & (IN_FIRE | IN_START))) return (uint8_t)(1 + sel);
    }
}

/* ---- a game */
static void play(uint8_t players)
{
    uint16_t perf_frames = 0, perf_ticks = 0, perf_draws = 0;
    uint8_t ticks, t, ev, i;
    WORD prev_wave = 0;
    uint8_t prev_state = 0xFF;
    int8_t lives0 = 3;
    int8_t r;

    game_init();
    gs.num_players = players;
    game_start();
    view_game();
    LOG_N("knights: start players=", players);
    LOG_END();
    frame_wait();
    for (;;) {
        ticks = frame_wait();
        T_START();
        view_frame();
        T_ADD(t_view);
        ++perf_draws;
        perf_frames += ticks;
        if (ticks > 8) ticks = 8;
        ev = 0;
        for (t = 0; t < ticks; ++t) {
            uint8_t in1 = stick(STICK0, STRIG0), in2 = stick(STICK1, STRIG1);
            if (players == 1) in1 |= keys();
            T_START();
            game_update(in1, in2);
            T_ADD(t_game);
            ev |= (uint8_t)gs.ev_flags;
            if (gs.ev_flags & EV_KILL) { LOG_N("knights: kill score=", gs.score[0]); LOG_END(); }
            if (gs.ev_flags & EV_EGG) { LOG_N("knights: pod score=", gs.score[0]); LOG_END(); }
            if ((gs.ev_flags & EV_DIE) && p_lives[0] != lives0) {
                lives0 = p_lives[0];
                LOG_N("knights: died lives=", p_lives[0]);
                LOG_END();
            }
            gs.ev_flags = 0;
            if (gs.state == STATE_GAMEOVER || gs.state == STATE_TITLE) break;
        }
        snd_events(ev);
        if (gs.wave != prev_wave) {
            prev_wave = gs.wave;
            LOG_N("knights: wave ", gs.wave);
            LOG_END();
        }
        /* the wave's number while it starts */
        if (gs.state != prev_state) {
            prev_state = (uint8_t)gs.state;
            if (prev_state == STATE_WAVE_INTRO) {
                char w[8] = "WAVE 00";
                w[5] = (char)('0' + gs.wave / 10);
                w[6] = (char)('0' + gs.wave % 10);
                view_msg(w, "GET READY!");
            } else if (prev_state == STATE_PLAYING) {
                view_msg(0, 0);
            }
        }
        if (gs.state == STATE_GAMEOVER) {
            snd_quiet();
            {
                char w[16] = "WAVE REACHED 00";
                w[13] = (char)('0' + gs.wave / 10);
                w[14] = (char)('0' + gs.wave % 10);
                view_msg("GAME OVER", w);
            }
            LOG_N("knights: gameover score=", gs.score[0]);
            LOG_END();
            /* the original waits 200 frames; a press of FIRE after one ends it */
            for (i = 0; i < 200; ++i) {
                frame_wait();
                view_frame();
                read_menu();
                if (i > 50 && (in_new & IN_FIRE)) break;
            }
            for (i = 0; i < gs.num_players; ++i) {
                r = hs_rank((uint32_t)gs.score[i]);
                if (r >= 0) enter_name(r, (uint32_t)gs.score[i]);
            }
            return;
        }
        perf_ticks += ticks;
        if (perf_frames >= 100) {
            LOG_N("knights: perf frames=", perf_frames);
            LOG_N(" ticks=", perf_ticks);
            LOG_N(" draws=", perf_draws);
            LOG_N(" enemies=", gs.enemies_alive);
            LOG_N(" view=", (long)t_view * 228 / perf_draws);
            LOG_N(" rules=", (long)t_game * 228 / perf_ticks);
            t_view = t_game = 0;
            LOG_END();
            perf_frames = perf_ticks = perf_draws = 0;
        }
    }
}

int main(void)
{
    rnd_seed(12345);
    snd_init();
    view_init();
    frame_init();
    for (;;) play(title());
}
