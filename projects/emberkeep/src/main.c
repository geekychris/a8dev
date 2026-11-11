/*
 * Emberkeep for the Atari 8-bit - port of amiga_mcp/examples/emberkeep.
 *
 * game.c is the original's rules, view.c draws them; this is the loop (the
 * rules at a fixed 50 Hz: once for every frame that passed, the picture
 * once), the title, the end screens and the high scores (new).
 *
 * Controls: the joystick (or W / A / S / D) walks; FIRE (SPACE, START)
 * begins and leaves the end screens.
 *
 * Log (tests): "ember: ready", "ember: start", "ember: chamber N",
 * "ember: rune score=", "ember: oil score= lantern=", "ember: died lives=",
 * "ember: portal score=", "ember: gameover score=", "ember: win score=",
 * "ember: initials rank=", "ember: hiscore ...", "ember: perf ...".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

static uint8_t in_now, in_new;
static char hs_names[5][4] = {"EMB", "LAN", "OIL", "RUN", "KEY"};
static uint32_t hs_score[5] = {2500, 2000, 1500, 1000, 500};
static uint16_t t_view, t_rules;

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

static uint8_t stick_in(void)
{
    uint8_t s = STICK0, r = 0;
    if (!(s & 1)) r |= INPUT_UP;
    if (!(s & 2)) r |= INPUT_DOWN;
    if (!(s & 4)) r |= INPUT_LEFT;
    if (!(s & 8)) r |= INPUT_RIGHT;
    if (!(SKSTAT & 4)) {
        switch (KBCODE & 0x3F) {
        case KEY_W: r |= INPUT_UP; break;
        case KEY_S: r |= INPUT_DOWN; break;
        case KEY_A: r |= INPUT_LEFT; break;
        case KEY_D: r |= INPUT_RIGHT; break;
        }
    }
    return r;
}

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

static void title(void)
{
    static uint8_t first = 1;
    uint8_t t = 0;
    snd_quiet();
    view_title((const char (*)[4])hs_names, hs_score, 0xFF);
    if (first) { LOG_S("ember: ready\n"); first = 0; }
    else LOG_S("ember: title\n");
    in_now = menu_now();
    for (;;) {
        t += frame_wait();
        if (t > 200) t = 16;
        view_title((const char (*)[4])hs_names, hs_score, (uint8_t)(t >> 4));
        read_menu();
        if (t > 15 && (in_new & (IN_FIRE | IN_START))) return;
    }
}

static void enter_name(int8_t rank)
{
    char name[4] = "A__", shown[8];
    uint8_t pos = 0, rep = 0, k, i;
    view_text(14, 0);
    view_text(16, "NEW HIGH SCORE");
    key_get();
    LOG_N("ember: initials rank=", rank + 1);
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
        view_text(18, shown);
    }
    for (i = 4; i > (uint8_t)rank; --i) {
        memcpy(hs_names[i], hs_names[i - 1], 4);
        hs_score[i] = hs_score[i - 1];
    }
    memcpy(hs_names[rank], name, 3);
    hs_names[rank][3] = 0;
    hs_score[rank] = gs.score;
    LOG_N("ember: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hs_names[rank]);
    LOG_N(" score=", (long)gs.score);
    LOG_END();
}

static void end_screen(void)
{
    uint8_t t = 0, win = gs.mode == GM_WIN;
    int8_t rank = -1, i;
    snd_quiet();
    LOG_S(win ? "ember: win score=" : "ember: gameover score=");
    LOG_N("", (long)gs.score);
    LOG_END();
    view_end(win, gs.score, 0xFF);
    for (i = 0; i < 5; ++i)
        if (gs.score > hs_score[i]) { rank = i; break; }
    if (rank >= 0) {
        /* the original's pause first */
        while (gs.state_timer) { frame_wait(); --gs.state_timer; }
        enter_name(rank);
        return;
    }
    in_now = menu_now();
    for (;;) {
        t += frame_wait();
        if (t > 200) t = 16;
        if (gs.state_timer) --gs.state_timer;
        view_end(win, gs.score, gs.state_timer ? 0 : (uint8_t)(t >> 4));
        read_menu();
        if (!gs.state_timer && (in_new & (IN_FIRE | IN_START))) return;
    }
}

static void play(void)
{
    uint16_t perf_frames = 0, perf_ticks = 0, perf_draws = 0;
    uint8_t ticks, t, ev, lives = 3, level = 0xFF;
    uint32_t sc;
    gs.level_index = 0;
    gs.lives = 3;
    gs.score = 0;
    gs.events = 0;
    game_load_level(0);
    gs.mode = GM_PLAYING;
    view_game_screen();
    LOG_S("ember: start\n");
    for (;;) {
        ticks = frame_wait();
        perf_frames += ticks;
        if (ticks > 8) ticks = 8;
        ev = 0;
        for (t = 0; t < ticks; ++t) {
            uint8_t in = stick_in();
            sc = gs.score;
            T_START();
            game_tick(in);
            T_ADD(t_rules);
            ++perf_ticks;
            ev |= gs.events;
            if (gs.events & EV_RUNE) { LOG_N("ember: rune score=", (long)gs.score); LOG_END(); }
            if (gs.events & EV_OIL) {
                LOG_N("ember: oil score=", (long)gs.score);
                LOG_N(" lantern=", gs.lantern);
                LOG_END();
            }
            if (gs.events & EV_DIE) { LOG_N("ember: died lives=", gs.lives); LOG_END(); }
            if (gs.events & EV_PORTAL) { LOG_N("ember: portal score=", (long)gs.score); LOG_END(); }
            gs.events &= (uint8_t)~(EV_RUNE | EV_OIL | EV_DIE | EV_PORTAL | EV_OPEN);
            if (gs.mode == GM_WIN || gs.mode == GM_LOSE) break;
        }
        snd_events(ev);
        snd_tick(gs.mode == GM_PLAYING && gs.lantern < LANTERN_MAX / 4);
        if (gs.mode == GM_WIN || gs.mode == GM_LOSE) {
            end_screen();
            return;
        }
        if (gs.mode == GM_LEVELWIN) {
            view_hud_msg("THE PORTAL OPENS");
        }
        if (gs.events & EV_LEVEL) {
            gs.events &= (uint8_t)~EV_LEVEL;
            view_level();
            frame_init();                   /* port: drawing the chamber (a few frames) is a
                                               pause, as the original's instant load: the
                                               rules don't catch up on it */
            if (gs.level_index != level) {
                level = gs.level_index;
                LOG_N("ember: chamber ", level + 1);
                LOG_END();
            }
            view_hud_msg(level_name[gs.level_index]);
            lives = gs.lives;
        }
        if (gs.mode == GM_PLAYING && gs.tick > 100 && lives != 0xFF) {
            /* the chamber's name for 2 seconds, then the HUD */
        }
        T_START();
        view_frame();
        T_ADD(t_view);
        ++perf_draws;
        if (perf_frames >= 100) {
            LOG_N("ember: perf frames=", perf_frames);
            LOG_N(" ticks=", perf_ticks);
            LOG_N(" draws=", perf_draws);
            LOG_N(" view=", (long)t_view * 228 / perf_draws);
            LOG_N(" rules=", (long)t_rules * 228 / perf_ticks);
            LOG_END();
            perf_frames = perf_ticks = perf_draws = 0;
            t_view = t_rules = 0;
            view_hud_msg(0);                /* (the chamber's name: gone) */
        }
    }
}

int main(void)
{
    snd_init();
    view_init();
    frame_init();
    game_init();
    for (;;) {
        title();
        play();
    }
}
