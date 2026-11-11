/*
 * Warden Keep for the Atari 8-bit - port of amiga_mcp/examples/warden_keep.
 *
 * game.c is the original's rules, view.c draws them; this is the loop (the
 * rules once for every frame that passed, the picture once), the input
 * (one step a press, then the original's auto-repeat: 12 frames, then
 * every 6), the title, the end screens and the high scores (new).
 *
 * Controls: the joystick (or the arrow keys / W A S D) steps: up is north
 * (up-right on the screen), down south, left west, right east. FIRE (SPACE,
 * START) begins and leaves the end screens; SELECT on the title picks the
 * timed or the relaxed game; OPTION restarts the room; ESC ends the game.
 *
 * Log (tests): "warden: ready", "warden: title", "warden: start mode=",
 * "warden: room N", "warden: at X,Y", "warden: push X,Y", "warden: bump",
 * "warden: jewel score=", "warden: cleared room= bonus= score=",
 * "warden: restart room=", "warden: timeup score=", "warden: win score=",
 * "warden: quit", "warden: initials rank=", "warden: hiscore rank= name=
 * score=", "warden: mode relaxed|timed", "warden: perf ...".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

static uint8_t in_now, in_new;
static char hs_names[5][4] = {"WAR", "DEN", "KEE", "PJW", "ELS"};
static uint16_t hs_score[5] = {2500, 2000, 1500, 1000, 500};
static uint8_t relaxed;

#ifdef A8_NO_LOG
#define LOG_S(s)      ((void)0)
#define LOG_N(k, v)   ((void)0)
#define LOG_END()     ((void)0)
#define T_START()     ((void)0)
#define T_ADD(acc)    ((void)0)
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
#endif

static void read_in(void)
{
    uint8_t old = in_now;
    in_now = input_read();
    in_new = (uint8_t)(in_now & ~old);
}

static void wait_fire(uint8_t row, const char *msg, uint8_t hold)
{
    uint8_t t = 0;
    in_now = input_read();
    for (;;) {
        t += frame_wait();
        if (hold) --hold;
        if (t > 200) t = 16;
        view_text(row, (!hold && ((t >> 4) & 1)) ? msg : 0);
        read_in();
        if (!hold && (in_new & (IN_FIRE | IN_START))) return;
    }
}

static void title(void)
{
    static uint8_t first = 1;
    uint8_t i, t = 0;
    char line[21];
    snd_title();
    view_text_screen(0x1C);
    view_text(3, "WARDEN KEEP");
    view_text(5, "push the barrels,");
    view_text(6, "take the jewels");
    view_text(8, "HIGH SCORES");
    for (i = 0; i < 5; ++i) {
        memcpy(line, "1 ABC 000000", 13);
        line[0] = (char)('1' + i);
        memcpy(line + 2, hs_names[i], 3);
        fmt_score(hs_score[i], line + 6);
        view_text((uint8_t)(9 + i), line);
    }
    view_text(16, "stick: step");
    view_text(17, "option: restart room");
    if (first) { LOG_S("warden: ready\n"); first = 0; }
    else LOG_S("warden: title\n");
    in_now = input_read();
    for (;;) {
        view_text(19, relaxed ? "RELAXED~GAME" : "TIMED~GAME");
        view_text(20, relaxed ? "select: no clock" : "select: 60 s a room");
        t += frame_wait();
        if (t > 200) t = 16;
        view_text(22 - 1, (t >> 4) & 1 ? "PRESS FIRE" : 0);
        read_in();
        if (in_new & IN_SELECT) {
            relaxed ^= 1;
            LOG_S(relaxed ? "warden: mode relaxed\n" : "warden: mode timed\n");
        }
        if (t > 15 && (in_new & (IN_FIRE | IN_START))) return;
    }
}

static void enter_name(int8_t rank)
{
    char name[4] = "A__", shown[8];
    uint8_t pos = 0, rep = 0, k, i;
    view_text(15, "NEW HIGH SCORE");
    key_get();
    LOG_N("warden: initials rank=", rank + 1);
    LOG_END();
    while (pos < 3) {
        frame_wait();
        read_in();
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
            } else --rep;
        } else rep = 0;
        if (in_new & (IN_FIRE | IN_RIGHT | IN_START)) {
            ++pos;
            if (pos < 3) name[pos] = 'A';
        }
        for (i = 0; i < 3; ++i) {
            shown[i * 2] = name[i];
            shown[i * 2 + 1] = ' ';
        }
        shown[5] = 0;
        view_text(17, shown);
    }
    for (i = 4; i > (uint8_t)rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_score[i] = hs_score[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_score[rank] = gs.score;
    LOG_N("warden: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_N(" score=", (long)gs.score);
    LOG_END();
}

static void end_screen(uint8_t how)        /* GM_WIN, GM_OVER, 0: ESC */
{
    char line[21];
    int8_t rank = -1, i;
    snd_quiet();
    view_text_screen(how == GM_WIN ? 0x1C : 0x36);
    view_text(3, how == GM_WIN ? "WELL DONE" : how == GM_OVER ? "TIME UP" : "GAME OVER");
    view_text(6, how == GM_WIN ? "EVERY JEWEL CLAIMED" : how == GM_OVER ? "the clock ran out" : 0);
    memcpy(line, "FINAL SCORE ", 12);
    fmt_score(gs.score, line + 12);
    view_text(9, line);
    if (relaxed) view_text(11, "relaxed: no table");
    if (!relaxed)
        for (i = 0; i < 5; ++i)
            if (gs.score > hs_score[i]) { rank = i; break; }
    if (rank >= 0) {
        uint8_t w = how == GM_WIN ? 200 : 50;
        while (w) { frame_wait(); --w; }
        enter_name(rank);
        return;
    }
    /* the original's 200 frames before SPACE on the win screen */
    wait_fire(14, "PRESS FIRE", how == GM_WIN ? 200 : 50);
}

/* the step this frame: a press, then the original's auto-repeat */
static uint8_t rep;

static uint8_t step_in(void)
{
    uint8_t d = in_now & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT);
    if (!d) { rep = 0; return 0; }
    if (in_new & d) { rep = 12; return d; }
    if (--rep == 0) { rep = 6; return d; }
    return 0;
}

static uint16_t perf_frames, perf_draws, t_view, wmax;

static uint8_t play_room(void)
{
    uint16_t work;
    uint8_t ticks, t, ev, msg = 0, pend = 0;
    game_load_room(gs.room);
    view_room();
    frame_init();
    LOG_N("warden: room ", gs.room + 1);
    LOG_END();
    view_hud_msg(room_name[gs.room]);
    msg = 100;
    in_now = input_read();
    rep = 0;
    for (;;) {
        ticks = frame_wait();
        T_START();
        work = 0;
        perf_frames += ticks;
        if (ticks > 8) ticks = 8;
        read_in();
        if (key_peek() == KEY_ESC) {
            key_get();
            LOG_S("warden: quit\n");
            return 0;
        }
        if ((in_new & IN_OPTION) && gs.mode == GM_PLAYING) {
            /* port: the room again (the barrels may be stuck); the clock runs on */
            uint8_t s = gs.seconds, sub = gs.sub;
            gs.score = gs.room_score;
            game_load_room(gs.room);
            gs.seconds = s;
            gs.sub = sub;
            view_room();
            LOG_N("warden: restart room=", gs.room + 1);
            LOG_END();
            frame_init();
            continue;
        }
        pend = step_in();
        ev = 0;
        for (t = 0; t < ticks; ++t) {
            game_tick(pend);
            pend = 0;
            ev |= gs.events;
            if (gs.events & EV_PUSH) {
                LOG_N("warden: push ", obj_x[gs.moved_obj]);
                LOG_N(",", obj_y[gs.moved_obj]);
                LOG_END();
            }
            if (gs.events & EV_STEP) {
                LOG_N("warden: at ", gs.px);
                LOG_N(",", gs.py);
                LOG_END();
            }
            if (gs.events & EV_BUMP) LOG_S("warden: bump\n");
            if (gs.events & EV_JEWEL) { LOG_N("warden: jewel score=", (long)gs.score); LOG_END(); }
            if (gs.events & (EV_STEP | EV_PUSH | EV_JEWEL)) {
                /* the picture now: a later tick could move something else */
                view_frame();
            }
            if (gs.mode != GM_PLAYING) break;
        }
        snd_events(ev, gs.seconds);
        if (gs.mode == GM_CLEARED) {
            char b[21];
            LOG_N("warden: cleared room=", gs.room + 1);
            LOG_N(" bonus=", gs.bonus);
            LOG_N(" score=", (long)gs.score);
            LOG_END();
            view_frame();
            memcpy(b, "ROOM CLEARED +", 15);
            utoa(gs.bonus, b + 14, 10);
            view_hud_msg(gs.relaxed ? "ROOM CLEARED" : b);
            for (t = 0; t < 100; ++t) frame_wait();
            return GM_CLEARED;
        }
        if (gs.mode == GM_OVER) {
            LOG_N("warden: timeup score=", (long)gs.score);
            LOG_END();
            view_hud_msg("TIME UP");
            for (t = 0; t < 100; ++t) frame_wait();
            return GM_OVER;
        }
        if (msg && !--msg) view_hud_msg(0);
        view_frame();
        T_ADD(work);
        t_view += work;
        if (work > wmax) wmax = work;
        ++perf_draws;
        if (perf_frames >= 250) {
            LOG_N("warden: perf frames=", perf_frames);
            LOG_N(" loops=", perf_draws);
            LOG_N(" cycles=", (long)t_view * 228 / perf_draws);
            LOG_N(" max=", (long)wmax * 228);
            LOG_END();
            perf_frames = perf_draws = t_view = wmax = 0;
        }
    }
}

static void play(void)
{
    uint8_t r;
    gs.score = 0;
    gs.relaxed = relaxed;
    gs.tps = frame_is_pal() ? 50 : 60;
    LOG_S(relaxed ? "warden: start mode=relaxed\n" : "warden: start mode=timed\n");
    snd_quiet();
    view_game_screen();
    for (gs.room = 0; gs.room < NUM_ROOMS; ++gs.room) {
        r = play_room();
        if (r != GM_CLEARED) {
            if (r == 0) { snd_quiet(); return; }    /* ESC: back to the title */
            end_screen(GM_OVER);
            return;
        }
    }
    LOG_N("warden: win score=", (long)gs.score);
    LOG_END();
    end_screen(GM_WIN);
}

int main(void)
{
    view_init();
    frame_init();
    for (;;) {
        title();
        play();
    }
}
