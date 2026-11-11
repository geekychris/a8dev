/*
 * Tank Duel for the Atari 8-bit - the Atari edition of
 * amiga_mcp/examples/tank_duel (from the Spectrum Next port).
 *
 * The rules (td_core.c, td_ai.c) run 12.5 ticks a second in real time
 * (fixed step, catching up to 3 ticks); the picture (view.c) as often as
 * it can.
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"
#include "td.h"
#include "view.h"

extern void td_dli(void);
void os_off(void);
extern uint8_t sky_col;

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

static uint8_t autopilot = 1, in_latch;
static uint8_t rt_last;
static uint16_t t_rules, t_draw, n_ticks, n_draws;

static uint8_t read_in(void)
{
    uint8_t s = input_read(), in = 0;
    if (s & IN_LEFT) in |= TD_IN_LEFT;
    if (s & IN_RIGHT) in |= TD_IN_RIGHT;
    if (s & IN_UP) in |= TD_IN_UP;
    if (s & IN_DOWN) in |= TD_IN_DOWN;
    if (s & IN_FIRE) in |= TD_IN_FIRE;
    return in;
}

/* the rules: a tick for every 4 frames passed (at most 3) */
static void catch_up(void)
{
    uint8_t n = 0, now;
    TDEvent e;
    now = RTCLOK2;
    while ((uint8_t)(now - rt_last) >= 4 && n < 3) {
        uint8_t in = autopilot ? td_bot() : (uint8_t)(read_in() | in_latch);
        in_latch = 0;
        {
            uint16_t t = a3d_ticks();
            td_tick(in);
            t_rules += a3d_since(t);
            ++n_ticks;
        }
        rt_last += 4;
        ++n;
        while (td_pop_event(&e)) {
            switch (e.type) {
            case TDE_KILL:
                view_boom(e.x, e.z);
                LOG_S("td: kill "); LOG_S(td_tank_names[e.b]); LOG_N(" score=", G.score); LOG_END();
                break;
            case TDE_HURT: LOG_N("td: hurt hp=", e.b); LOG_END(); break;
            case TDE_ROUND_CLEAR: LOG_N("td: clear round=", e.a); LOG_END(); break;
            case TDE_ROUND: LOG_N("td: round ", e.a); LOG_END(); break;
            case TDE_DEAD: LOG_N("td: dead score=", G.score); LOG_END(); break;
            case TDE_CRATE_BREAK: view_boom(e.x, e.z); break;
            }
        }
    }
    if ((uint8_t)(now - rt_last) >= 12) rt_last = now - 4;    /* (too far behind: let go) */
}

int main(void)
{
    uint16_t frames = 0, t0 = 0;
    os_off();                               /* the rules' code is under the ROM */
    a3d_init(V_LINES, 2);
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = 0x02;
    a3d_dl_flag(V_HORIZON - 1, 0x80);
    VDSLST = (uint16_t)td_dli;
    COLOR4 = sky_col;
    COLOR0 = 0x04;                          /* dark */
    COLOR1 = 0x26;                          /* tan */
    COLOR2 = 0x0C;                          /* light */
    view_init();
    frame_init();
    td_new_game(1234, 1);
#ifdef BENCH
    {
        uint16_t t; uint8_t i; volatile int16_t r = 0;
        extern void bench_view(void);
        t = a3d_ticks();
        for (i = 0; i < 100; ++i) r += a3d_mulf(1234, 77);
        LOG_N("bench: mulf x100 cycles=", (long)a3d_since(t) * 228); LOG_END();
        t = a3d_ticks();
        for (i = 0; i < 100; ++i) r += td_len(1234, 777);
        LOG_N("bench: td_len x100 cycles=", (long)a3d_since(t) * 228); LOG_END();
        bench_view();
    }
#endif
    rt_last = RTCLOK2;
    LOG_S("td: ready\n");
    for (;;) {
        a3d_sync();
        catch_up();
        {
            uint16_t t = a3d_ticks();
            view_draw();
            t_draw += a3d_since(t);
            ++n_draws;
        }
        a3d_show();
        ++frames;
        if (frame_count - t0 >= 250) {
            LOG_N("td: fps10=", (uint16_t)((uint32_t)frames * 500 / (frame_count - t0)));
            LOG_N(" round=", G.round);
            LOG_N(" kills=", G.kills);
            LOG_N(" tick=", (long)t_rules * 228 / (n_ticks ? n_ticks : 1));
            LOG_N(" draw=", (long)t_draw * 228 / (n_draws ? n_draws : 1));
            {
                extern uint16_t vt_clear, vt_walls, vt_obj, vt_miss, vt_nobj;
                uint8_t n = n_draws ? n_draws : 1;
                LOG_N(" clear=", (long)vt_clear * 228 / n);
                LOG_N(" walls=", (long)vt_walls * 228 / n);
                LOG_N(" obj=", (long)vt_obj * 228 / n);
                LOG_N(" nobj10=", (long)vt_nobj * 10 / n);
                LOG_N(" miss=", (long)vt_miss * 228 / n);
                vt_clear = vt_walls = vt_obj = vt_miss = vt_nobj = 0;
            }
            LOG_END();
            t_rules = t_draw = n_ticks = n_draws = 0;
            frames = 0;
            t0 = frame_count;
        }
    }
}
