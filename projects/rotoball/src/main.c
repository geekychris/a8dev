/*
 * Rotoball for the Atari 8-bit - port of amiga_mcp/examples/rotoball.
 *
 * game.c is the original's rules, view.c draws them. This is the loop
 * (the rules at a fixed 50 Hz: once for every frame that passed, the
 * picture once), the title (the original's kick-off screen: 1 player
 * against the CPU or 2 players), full time and the best wins (new).
 *
 * Controls: joystick 1 is P1 (the bottom view), joystick 2 P2 (the top):
 * up / down drive forward / back, left / right strafe. Keys for P1: W / S,
 * A / D. Title: up / down picks, FIRE or START starts.
 *
 * Log (tests): "roto: ready", "roto: start players=N", "roto: pickup",
 * "roto: tackle", "roto: goal p1=N p2=N pts=N", "roto: fulltime p1= p2=",
 * "roto: initials rank=", "roto: hiscore ...", "roto: title",
 * "roto: perf frames= ticks= draws= view= rules=".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

static uint8_t in_now, in_new;

/* the best wins against the CPU: the goal difference, then the goals */
static char hs_names[5][4] = {"ACE", "ROT", "SKM", "BAL", "CPU"};
static uint8_t hs_diff[5] = {5, 4, 3, 2, 1};
static uint8_t hs_goals[5] = {6, 5, 4, 3, 2};

static uint16_t t_view, t_rules;    /* the perf log's (0 in RELEASE) */

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
/* the time drawing and the rules take, in 2 scan lines (228 cycles), from
   the frame clock and VCOUNT (the VBI comes at VCOUNT 124: PAL) */
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

/* ---- input */
static uint8_t stick(uint8_t s)
{
    uint8_t r = 0;
    if (!(s & 1)) r |= C_FWD;
    if (!(s & 2)) r |= C_BACK;
    if (!(s & 4)) r |= C_LEFT;
    if (!(s & 8)) r |= C_RIGHT;
    return r;
}

static uint8_t keys(void)
{
    if (SKSTAT & 4) return 0;
    switch (KBCODE & 0x3F) {
    case KEY_W: return C_FWD;
    case KEY_S: return C_BACK;
    case KEY_A: return C_LEFT;
    case KEY_D: return C_RIGHT;
    }
    return 0;
}

static uint8_t menu_now(void)
{
    uint8_t s = STICK0, r = 0;
    if (!(s & 1)) r |= IN_UP;
    if (!(s & 2)) r |= IN_DOWN;
    if (!(s & 4)) r |= IN_LEFT;
    if (!(s & 8)) r |= IN_RIGHT;
    if (!STRIG0 || !STRIG1) r |= IN_FIRE;
    r |= (uint8_t)((~CONSOL & 7) << 5);
    if (!(SKSTAT & 4)) {
        switch (KBCODE & 0x3F) {
        case KEY_W: r |= IN_UP; break;
        case KEY_S: r |= IN_DOWN; break;
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

/* ---- text */
static void centre(uint8_t row, const char *s)
{
    view_text_clear(row);
    view_text(row, (uint8_t)((40 - strlen(s)) >> 1), s);
}

static void num2(char *d, uint8_t v)
{
    d[0] = (char)(v >= 10 ? '0' + v / 10 : ' ');
    d[1] = (char)('0' + v % 10);
}

/* ---- the title: the kick-off positions under the menu */
static const uint8_t ov_title_top[] = {39, 4, 51, 5, 59, 6, 67, 7, 75, 8, 83, 9, 0xFF};
static const uint8_t ov_title_bot[] = {35, 10, 51, 11, 63, 12, 79, 13, 0xFF};

static void title_text(uint8_t sel)
{
    uint8_t i;
    char line[24];
    centre(4, "BEST WINS AGAINST THE CPU");
    for (i = 0; i < 5; ++i) {
        memcpy(line, "1  ABC   WON  5 :  1", 21);
        line[0] = (char)('1' + i);
        memcpy(line + 3, hs_names[i], 3);
        num2(line + 13, hs_goals[i]);
        num2(line + 18, (uint8_t)(hs_goals[i] - hs_diff[i]));
        centre((uint8_t)(5 + i), line);
    }
    centre(10, "R O T O B A L L");
    centre(11, sel ? "  1 PLAYER V CPU   > 2 PLAYERS  " : "> 1 PLAYER V CPU     2 PLAYERS  ");
    centre(12, "STICK: UP/DOWN DRIVE  LEFT/RIGHT STRAFE");
    centre(13, "PRESS FIRE TO START");
}

static uint8_t title(void)
{
    static uint8_t first = 1;
    uint8_t t = 0, sel = 0, k;
    snd_quiet();
    game_init();
    view_hud_reset();
    title_text(sel);
    view_overlay(ov_title_top, ov_title_bot);
    if (first) {
        LOG_S("roto: ready\n");
        first = 0;
    } else {
        LOG_S("roto: title\n");
    }
    in_now = menu_now();
    for (;;) {
        t += frame_wait();
        if (t > 200) t = 200;
        view_frame();
        read_menu();
        k = key_get();
        if (k == KEY_1 || k == KEY_2) return k == KEY_1 ? 1 : 2;
        if (in_new & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_SELECT)) {
            sel ^= 1;
            title_text(sel);
        }
        if (t > 15 && (in_new & (IN_FIRE | IN_START))) return (uint8_t)(1 + sel);
    }
}

/* ---- the best wins */
static void enter_name(uint8_t rank)
{
    char name[4] = "A__", shown[8];
    uint8_t pos = 0, rep = 0, k, i;
    static const uint8_t ov_top[] = {83, 4, 0xFF};
    static const uint8_t ov_bot[] = {43, 10, 55, 11, 0xFF};
    centre(4, "A BEST WIN!");
    centre(10, "YOUR INITIALS");
    view_overlay(ov_top, ov_bot);
    key_get();
    LOG_N("roto: initials rank=", rank + 1);         /* (ready for the keys) */
    LOG_END();
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
        centre(11, shown);
    }
    for (i = 4; i > rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_diff[i] = hs_diff[i - 1];
        hs_goals[i] = hs_goals[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_diff[rank] = (uint8_t)(G.score[0] - G.score[1]);
    hs_goals[rank] = G.score[0];
    LOG_N("roto: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_END();
}

/* full time: play frozen under the result (the original's banner) */
static void full_time(void)
{
    static const uint8_t ov_top[] = {83, 4, 0xFF};
    static const uint8_t ov_bot[] = {43, 10, 0xFF};
    uint8_t i, rank = 0xFF, d;
    const char *res;
    LOG_N("roto: fulltime p1=", G.score[0]);
    LOG_N(" p2=", G.score[1]);
    LOG_END();
    if (G.players == 1)
        res = G.score[0] > G.score[1] ? "YOU WIN!" : G.score[1] > G.score[0] ? "CPU WINS" : "DRAW";
    else
        res = G.score[0] > G.score[1] ? "P1 WINS!" : G.score[1] > G.score[0] ? "P2 WINS!" : "DRAW";
    centre(4, res);
    centre(10, "PRESS FIRE");
    view_overlay(ov_top, ov_bot);
    for (i = 0; i < 250; ++i) {                 /* FIRE after a second ends it */
        frame_wait();
        snd_tick();
        view_frame();
        read_menu();
        if (i > 50 && (in_new & (IN_FIRE | IN_START))) break;
    }
    if (G.players == 1 && G.score[0] > G.score[1]) {
        d = (uint8_t)(G.score[0] - G.score[1]);
        for (i = 0; i < 5; ++i)
            if (d > hs_diff[i] || (d == hs_diff[i] && G.score[0] > hs_goals[i])) { rank = i; break; }
        if (rank != 0xFF) enter_name(rank);
    }
}

/* ---- a match */
static void match(uint8_t players)
{
    uint16_t perf_frames = 0, perf_ticks = 0, perf_draws = 0;
    uint8_t ticks, t, ev;
    game_start(players);
    view_hud_reset();
    view_overlay(0, 0);
    LOG_N("roto: start players=", players);
    LOG_END();
    for (;;) {
        ticks = frame_wait();
        perf_frames += ticks;
        if (ticks > 4) ticks = 4;
        ev = 0;
        for (t = 0; t < ticks; ++t) {
            uint8_t c1 = stick(STICK0), c2 = stick(STICK1);
            c1 |= keys();
            T_START();
            game_step(c1, c2);
            T_ADD(t_rules);
            ++perf_ticks;
            ev |= G.events;
            if (G.events & EV_PICKUP) LOG_S("roto: pickup\n");
            if (G.events & EV_TACKLE) LOG_S("roto: tackle\n");
            if (G.events & (EV_GOAL1 | EV_GOAL2)) {
                LOG_N("roto: goal p1=", G.score[0]);
                LOG_N(" p2=", G.score[1]);
                LOG_N(" pts=", G.last_pts);
                LOG_END();
            }
            if (G.state != MS_PLAYING) break;
        }
        snd_events(ev);
        snd_tick();
        T_START();
        view_frame();
        T_ADD(t_view);
        ++perf_draws;
        if (G.state == MS_FULLTIME) {
            full_time();
            return;
        }
        if (perf_frames >= 100) {
            LOG_N("roto: perf frames=", perf_frames);
            LOG_N(" ticks=", perf_ticks);
            LOG_N(" draws=", perf_draws);
            LOG_N(" view=", (long)t_view * 228 / perf_draws);
            LOG_N(" rules=", (long)t_rules * 228 / perf_ticks);
            LOG_END();
            perf_frames = perf_ticks = perf_draws = 0;
            t_view = t_rules = 0;
        }
    }
}

int main(void)
{
    snd_init();
    view_init();
    frame_init();
    for (;;) match(title());
}
