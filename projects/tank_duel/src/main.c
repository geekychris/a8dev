/*
 * Tank Duel for the Atari 8-bit - the Atari edition of
 * amiga_mcp/examples/tank_duel (from the Spectrum Next port).
 *
 * The rules (td_core.c, td_ai.c, under the OS ROM) run 12.5 ticks a second
 * in real time (fixed step, catching up to 3 ticks); the picture (view.c)
 * as often as it can. The title, the HUD, game over and the high scores
 * are here.
 *
 * Controls: the joystick drives (up / down forward / back, left / right
 * turn), FIRE fires; OPTION pauses, SELECT toggles the autopilot, ESC goes
 * back to the title. On the title up / down choose the round to start at.
 *
 * Log (tests): "td: ready", "td: title", "td: start round=", "td: kill",
 * "td: hurt hp=", "td: clear round=", "td: round", "td: dead score=",
 * "td: win score=", "td: initials rank=", "td: hiscore rank= name= score=",
 * "td: fps10=".
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "td.h"
#include "view.h"
#include "sound.h"

extern void td_dli(void);
extern uint8_t sky_col;
void os_off(void);
void vbi_install(void);
void radar_init(void);
void radar_update(void);
void radar_on(void);
void radar_off(void);

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

enum { ST_TITLE, ST_PLAY, ST_OVER };
static uint8_t state, autopilot, paused, start_round = 1, rt_last, msg_t;
static uint8_t in_now, in_new;
static uint16_t seed = 1234;
static char hs_name[5][4] = {"TNK", "DUL", "ARM", "SHL", "BUL"};
static int32_t hs_score[5] = {6000, 4000, 2500, 1500, 800};
static uint16_t t_rules, t_draw, n_ticks, n_draws;

static uint8_t joy_in(void)
{
    uint8_t s = input_read(), in = 0;
    if (s & IN_LEFT) in |= TD_IN_LEFT;
    if (s & IN_RIGHT) in |= TD_IN_RIGHT;
    if (s & IN_UP) in |= TD_IN_UP;
    if (s & IN_DOWN) in |= TD_IN_DOWN;
    if (s & IN_FIRE) in |= TD_IN_FIRE;
    return in;
}

uint8_t take_latch(void);               /* os.s: presses latched in the VBI */
extern uint8_t in_held;

static void read_in(void)
{
    in_now = in_held;
    in_new = take_latch();                  /* (a loop can take many frames: presses are latched) */
}

/* ---------- the HUD (two text rows) ---------- */
static void hud_clear(uint8_t row) { memset(A3D_TEXT + row * 40, 0, 40); }

static void bar(uint8_t x, uint8_t row, uint8_t n, uint8_t of)
{
    uint8_t i, *p = A3D_TEXT + row * 40 + x;
    for (i = 0; i < of; ++i) p[i] = i < n ? 0x80 | 0x00 : 0x0E;   /* inverse space: filled; '.' empty */
}

static void hud_play(void)
{
    const Tank *p = &G.tk[0];
    hud_clear(0);
    a3d_print(0, 0, "ROUND");
    a3d_num(6, 0, G.round, 1);
    a3d_print(9, 0, "ENEMIES");
    a3d_num(17, 0, td_enemies_left(), 1);
    a3d_print(22, 0, "SCORE");
    a3d_num(28, 0, (uint16_t)(G.score > 65535L ? 65535U : (uint16_t)G.score), 6);
    if (msg_t) return;                  /* (a message on row 1) */
    hud_clear(1);
    a3d_print(0, 1, "ARMOUR");
    bar(7, 1, (uint8_t)((p->hp + 9) / 10), 10);
    a3d_print(22, 1, "GUN");
    bar(26, 1, (uint8_t)(p->reload ? (TD_RELOAD - p->reload) * 6 / TD_RELOAD : 6), 6);
    if (paused) a3d_print(34, 1, "PAUSE");
    else if (autopilot) a3d_print(34, 1, "AUTO");
}

static void hud_msg(const char *s)
{
    hud_clear(1);
    a3d_print((uint8_t)((40 - strlen(s)) / 2), 1, s);
    msg_t = 30;
}

/* ---------- the rules ---------- */
static void events(void)
{
    TDEvent e;
    while (td_pop_event(&e)) {
        snd_event(e.type);
        switch (e.type) {
        case TDE_KILL:
            view_boom(e.x, e.z);
            LOG_S("td: kill "); LOG_S(td_tank_names[e.b]); LOG_N(" score=", G.score); LOG_END();
            break;
        case TDE_CRATE_BREAK: view_boom(e.x, e.z); break;
        case TDE_HURT: LOG_N("td: hurt hp=", e.b); LOG_END(); break;
        case TDE_ROUND_CLEAR:
            LOG_N("td: clear round=", e.a); LOG_N(" score=", G.score); LOG_END();
            if (state == ST_PLAY) hud_msg("ROUND CLEAR");
            break;
        case TDE_ROUND:
            LOG_N("td: round ", e.a); LOG_END();
            if (state == ST_PLAY) hud_msg("NEXT ROUND");
            break;
        case TDE_DEAD: LOG_N("td: dead score=", G.score); LOG_END(); break;
        case TDE_WIN: LOG_N("td: win score=", G.score); LOG_END(); break;
        }
    }
}

/* a tick for every 4 frames passed (at most 3) */
static void catch_up(void)
{
    uint8_t n = 0, now = RTCLOK2, in;
    while ((uint8_t)(now - rt_last) >= 4 && n < 3) {
        rt_last += 4;
        ++n;
        if (paused) continue;
        in = autopilot ? td_bot() : joy_in();
        {
            uint16_t t = a3d_ticks();
            td_tick(in);
            t_rules += a3d_since(t);
            ++n_ticks;
        }
        events();
        if (msg_t) --msg_t;
    }
    if ((uint8_t)(now - rt_last) >= 12) rt_last = now - 4;     /* (too far behind: let go) */
}

/* ---------- the screens ---------- */
static void to_title(void)
{
    state = ST_TITLE;
    autopilot = 1;
    paused = 0;
    td_new_game(seed, 1);
    hud_clear(0);
    hud_clear(1);
    a3d_print(0, 0, "TANK DUEL");
    a3d_print(14, 0, "FIRE: START  ROUND");
    a3d_num(33, 0, start_round, 1);
    LOG_S("td: title\n");
}

static void title_hs(uint8_t i)
{
    hud_clear(1);
    a3d_print(4, 1, "HIGH SCORES");
    a3d_num(17, 1, (uint16_t)(i + 1), 1);
    a3d_print(19, 1, hs_name[i]);
    a3d_num(24, 1, (uint16_t)hs_score[i], 6);
}

static void start_game(void)
{
    seed = (uint16_t)(seed * 25173 + 13849 + RTCLOK2);
    td_new_game(seed, start_round);
    state = ST_PLAY;
    autopilot = 0;
    paused = 0;
    msg_t = 0;
    radar_on();
    LOG_N("td: start round=", start_round); LOG_END();
}

static void enter_name(int8_t rank)
{
    char name[4] = "A__";
    uint8_t pos = 0, k;
    hud_clear(0);
    hud_clear(1);
    a3d_print(0, 0, "NEW HIGH SCORE: TYPE YOUR INITIALS");
    LOG_N("td: initials rank=", rank + 1); LOG_END();
    key_get();
    while (pos < 3) {
        a3d_sync();
        a3d_show();
        read_in();
        k = key_get();
        if (k != KEY_NONE) {
            char c = key_ascii(k);
            if (c >= 'A' && c <= 'Z') { name[pos++] = c; if (pos < 3) name[pos] = 'A'; }
        } else if (in_new & IN_UP) name[pos] = name[pos] >= 'Z' ? 'A' : (char)(name[pos] + 1);
        else if (in_new & IN_DOWN) name[pos] = name[pos] <= 'A' ? 'Z' : (char)(name[pos] - 1);
        else if (in_new & (IN_FIRE | IN_RIGHT)) { if (++pos < 3) name[pos] = 'A'; }
        hud_clear(1);
        a3d_print(18, 1, name);
    }
    for (k = 4; k > (uint8_t)rank; --k) { memcpy(hs_name[k], hs_name[k - 1], 4); hs_score[k] = hs_score[k - 1]; }
    memcpy(hs_name[rank], name, 4);
    hs_name[rank][3] = 0;
    hs_score[rank] = G.score;
    LOG_N("td: hiscore rank=", rank + 1); LOG_S(" name="); LOG_S(hs_name[rank]); LOG_N(" score=", G.score); LOG_END();
}

static void game_over(void)
{
    uint16_t t0 = frame_count;
    int8_t rank = -1, i;
    state = ST_OVER;
    hud_clear(1);
    a3d_print(G.won ? 15 : 15, 1, G.won ? "VICTORY!" : "GAME OVER");
    while ((uint16_t)(frame_count - t0) < 150) {   /* 3 s: the arena goes on */
        a3d_sync();
        catch_up();
        view_draw();
        a3d_show();
    }
    for (i = 0; i < 5; ++i) if (G.score > hs_score[i]) { rank = i; break; }
    if (rank >= 0) enter_name(rank);
    to_title();
}

int main(void)
{
    uint16_t frames = 0, t0 = 0, hst = 0;
    os_off();                               /* the rules' code is under the ROM */
    a3d_init(V_LINES, 2);
    CHBAS = 0x98;                           /* (os_off's copy of the font) */
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = 0x02;
    a3d_dl_flag(V_HORIZON - 1, 0x80);
    VDSLST = (uint16_t)td_dli;
    COLOR4 = sky_col;
    COLOR0 = 0x04;                          /* dark */
    COLOR1 = 0x26;                          /* tan */
    COLOR2 = 0x0C;                          /* light */
    view_init();
    radar_init();
    sound_init();
    vbi_install();
    frame_init();
    to_title();
    rt_last = RTCLOK2;
    LOG_S("td: ready\n");
    for (;;) {
        read_in();
        if (state == ST_TITLE) {
            if (in_new & IN_UP && start_round < TD_ROUNDS) { ++start_round; a3d_num(33, 0, start_round, 1); }
            if (in_new & IN_DOWN && start_round > 1) { --start_round; a3d_num(33, 0, start_round, 1); }
            if (in_new & (IN_FIRE | IN_START)) start_game();
            if (G.over || G.won) to_title();   /* (the demo ended) */
            if ((uint16_t)(frame_count - hst) >= 100) { hst = frame_count; title_hs((uint8_t)((hst / 100) % 5)); }
        } else if (state == ST_PLAY) {
            if (in_new & IN_OPTION) paused ^= 1;
            if (in_new & IN_SELECT) autopilot ^= 1;
            if (key_peek() == KEY_ESC) { key_get(); to_title(); }
            else if (G.over || G.won) { game_over(); continue; }
        }
        a3d_sync();
        catch_up();
        {
            uint16_t t = a3d_ticks();
            view_draw();
            t_draw += a3d_since(t);
            ++n_draws;
        }
        radar_update();
        if (state == ST_PLAY) hud_play();
        a3d_show();
        ++frames;
        if (frame_count - t0 >= 250) {
            LOG_N("td: fps10=", (uint16_t)((uint32_t)frames * 500 / (frame_count - t0)));
            LOG_N(" round=", G.round);
            LOG_N(" tick=", (long)t_rules * 228 / (n_ticks ? n_ticks : 1));
            LOG_N(" draw=", (long)t_draw * 228 / (n_draws ? n_draws : 1));
            {
                extern uint16_t vt_nobj;        /* (view.c: objects in view, a frame) */
                LOG_N(" inview10=", (uint16_t)((uint32_t)vt_nobj * 10 / (n_draws ? n_draws : 1)));
                vt_nobj = 0;
            }
#ifdef TT
            {
                extern uint16_t tt_part[6];
                uint8_t q;
                extern uint16_t tt_ai[4];
                for (q = 0; q < 6; ++q) { LOG_N(" p", q); LOG_N("=", (long)tt_part[q] * 228 / (n_ticks ? n_ticks : 1)); tt_part[q] = 0; }
                for (q = 0; q < 4; ++q) { LOG_N(" a", q); LOG_N("=", (long)tt_ai[q] * 228 / (n_ticks ? n_ticks : 1)); tt_ai[q] = 0; }
            }
#endif
            LOG_END();
            t_rules = t_draw = n_ticks = n_draws = 0;
            frames = 0;
            t0 = frame_count;
        }
    }
}
