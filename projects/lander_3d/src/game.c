/*
 * game.c - Lander 3D for the Atari 8-bit: the shell round the rules
 * (ld_core.c) and the picture (view.c).
 *
 * The rules run 25 steps a second (every second PAL frame) whatever the
 * drawing takes; the picture is drawn when it can, double buffered.
 *
 * Controls: the stick tips the craft (up: forward, down: back, left,
 * right; letting go holds the angle), FIRE is full thrust, SELECT / OPTION
 * (or E / Q) set a steady throttle in 10 % steps, START (or P) pauses,
 * B the autopilot, ESC the title. Title: left / right choose Moon, Mars,
 * Asteroid or Tour, FIRE or START plays.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "a3d.h"
#include "ld.h"
#include "view.h"

/* all of this file's BSS (its tables and cc65's static locals, -Cl) in LOWRAM */
#pragma bss-name ("LOWRAM")

enum { GS_TITLE, GS_PLAY, GS_RESULT, GS_OVER };

extern void ld_dli(void);
extern void in_install(void);
extern uint8_t in_take(void);
extern uint8_t in_held;
extern uint8_t key_take(void);

static uint8_t state, sel_world, world, level, ships, steady, paused, autopilot;
static uint16_t score, best;
static uint8_t state_timer, hud_tick;
static const char *const world_names[4] = { "MOON", "MARS", "ASTEROID", "TOUR" };
/* for the test: state, outcome, autopilot, ships, level, world, throttle */
uint8_t ld_probe[8];

#define KEY_B 21
#define KEY_E 42
#define KEY_Q 47

/* the log without printf: "key=value" pieces, a line ended by lg_end() */
#ifndef A8_NO_LOG
static void lg(const char *s) { a8_log_s(s); }
static void lgu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void lgi(const char *k, int16_t v)
{
    a8_log_s(k);
    if (v < 0) { a8_log_c('-'); v = -v; }
    a8_log_u((uint16_t)v);
}
#define lg_end() a8_log_c('\n')
#else
#define lg(s) ((void)0)
#define lgu(k, v) ((void)0)
#define lgi(k, v) ((void)0)
#define lg_end() ((void)0)
#endif

static uint32_t level_seed(uint8_t w, uint8_t lv) { return 1000UL * (w + 1) + 7UL * lv + 1; }

/* ------------------------------------------------------------ the HUD */
static void put(uint8_t x, uint8_t row, const char *s, uint8_t inv)
{
    uint8_t *p = A3D_TEXT + row * 40 + x, c;
    while ((c = (uint8_t)*s++) != 0) {
        c = c < 0x20 ? c + 0x40 : c < 0x60 ? c - 0x20 : c;
        *p++ = c | inv;
    }
}

/* numbers straight into the text rows (pos 0..79), right-aligned in w,
   inverse video when hinv is $80 (a reading past its limit) */
static uint8_t hinv;
static void numw(uint8_t pos, uint16_t v, uint8_t w)
{
    uint8_t *p = A3D_TEXT + pos + w;
    do { *--p = (uint8_t)(0x10 + v % 10) | hinv; v /= 10; } while (--w && v);
    while (w--) *--p = hinv;
}

/* v (8.8) as -d.d (5 characters) */
static void dec1(uint8_t pos, int16_t v)
{
    uint16_t a;
    A3D_TEXT[pos] = v < 0 ? 0x0D | hinv : hinv;
    if (v < 0) v = -v;
    a = (uint16_t)a3d_mulf(v, 10);
    numw(pos + 1, a / 10, 2);
    A3D_TEXT[pos + 3] = 0x0E | hinv;
    numw(pos + 4, a % 10, 1);
}

static void hud_labels(void)
{
    memset(A3D_TEXT, 0, 80);
    put(0, 0, "ALT", 0); put(8, 0, "V", 0); put(15, 0, "H", 0); put(22, 0, "T", 0); put(26, 0, "F", 0);
    put(0, 1, world_names[L.world], 0); put(8, 1, "L", 0); put(12, 1, "SCORE", 0); put(24, 1, "SHIPS", 0);
    put(33, 1, "X", 0);
}

static void hud_play(void)
{
    const LdWorld *W = &ld_worlds[L.world];
    int16_t v, a, b;
    uint8_t k, f;
    hinv = 0;
    v = ld_agl() >> 4;
    numw(3, v < 0 ? 0 : (uint16_t)v, 4);
    v = ld_v88[1];
    hinv = -v > W->lim_vs ? 0x80 : 0;
    dec1(9, v);
    a = ld_v88[0]; b = ld_v88[2];                               /* |v| within 7 % */
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    v = a > b ? a + (b >> 2) + (b >> 3) : b + (a >> 2) + (a >> 3);
    hinv = v > W->lim_hs ? 0x80 : 0;
    dec1(16, v);
    k = ld_tilt_deg();
    hinv = k > W->lim_tilt ? 0x80 : 0;
    numw(23, k, 2);
    hinv = 0;
    /* the fuel gauge: 10 cells, half cells by a lower bar */
    f = (uint8_t)a3d_muldiv(L.fuel >> 1, 20, L.fuel_max >> 1);
    for (k = 27; k < 37; ++k, f = f > 2 ? f - 2 : 0) A3D_TEXT[k] = f >= 2 ? 0x80 : f ? 0x8D : 0x0E;
    numw(37, (uint16_t)(L.throttle * 100 / 255), 3);
    numw(49, level, 2);
    numw(58, score, 5);
    numw(70, ships, 1);
    k = (uint8_t)ld_nearest_pad();
    a = L.pads[k].x - ld_m[0];
    b = L.pads[k].z - ld_m[2];
    /* an arrow to the nearest pad: ahead, back, left, right */
    A3D_TEXT[72] = b >= 0 ? (a > b ? 0x5F : -a > b ? 0x5E : 0x5C) : (a > -b ? 0x5F : -a > -b ? 0x5E : 0x5D);
    numw(74, L.pads[k].mult, 1);
    put(36, 1, autopilot ? "AP" : paused ? "PS" : "  ", 0);
}

static void hud_text(const char *a, const char *b)
{
    memset(A3D_TEXT, 0, 80);
    put(0, 0, a, 0);
    put(0, 1, b, 0);
}

static void hud_title(void)
{
    memset(A3D_TEXT, 0, 80);
    put(1, 0, "LANDER 3D", 0);
    put(12, 0, "<", 0);
    put(14, 0, world_names[sel_world], 0);
    put(23, 0, ">  FIRE", 0);
    put(1, 1, "BEST", 0);
    numw(46, best, 5);
}

/* ------------------------------------------------------------ flow */
static void begin_level(void)
{
    world = sel_world == 3 ? (uint8_t)((level - 1) % 3) : sel_world;
    view_world(world);
    ld_new_level(world, level, level_seed(world, level));
    steady = 0;
    autopilot = state == GS_TITLE;
    if (state != GS_TITLE) hud_labels();
    lg("ld: level world="); lg(world_names[world]); lgu(" level=", level); lgu(" fuel=", L.fuel_max);
    lgi(" pad1=", L.pads[0].x); lgi(",", L.pads[0].z); lgi(" y=", ld_m[1]); lg_end();
}

static void to_title(void)
{
    state = GS_TITLE;
    level = 1;
    sel_world = 0;
    begin_level();
    autopilot = 1;
    hud_title();
    lg("ld: title"); lg_end();
}

static void start_game(void)
{
    state = GS_PLAY;
    level = 1;
    ships = 3;
    score = 0;
    paused = 0;
    begin_level();
    lg("ld: start world="); lg(world_names[sel_world]); lg_end();
}

static void sound_frame(void)
{
    /* the engine: noise louder with the throttle */
    if (L.throttle && !L.outcome && state != GS_TITLE)
        snd_tone(1, 0x20, SND_NOISE, (uint8_t)(2 + (L.throttle >> 5)));
    else snd_off(1);
    if (L.rcs && !L.outcome && state != GS_TITLE) snd_tone(0, 0x08, SND_NOISE, 2);
    else snd_off(0);
}

static void outcome(uint8_t e)
{
    switch (e) {
    case LDE_LANDED:
        score += L.res.score;
        snd_sfx(2, 60, SND_PURE, 14, 1, -1);
        lgu("ld: landed pad=x", L.res.pad); lgu(" score=", L.res.score); lgu(" total=", score);
        lgi(" vs=", L.res.vs); lgi(" hs=", L.res.hs); lgu(" tilt=", L.res.tilt); lg_end();
        break;
    case LDE_OFFPAD:
        score += 25;
        snd_sfx(2, 90, SND_PURE, 12, 1, 2);
        lgu("ld: offpad score=25 total=", score); lg_end();
        break;
    case LDE_CRASHED:
    case LDE_LOST:
        snd_sfx(2, 200, SND_NOISE, 15, 1, 4);
        lg(e == LDE_LOST ? "ld: lost" : "ld: crashed"); lgi(" vs=", L.res.vs); lgi(" hs=", L.res.hs);
        lgu(" tilt=", L.res.tilt); lgu(" ships=", ships - 1); lg_end();
        break;
    case LDE_FUEL_OUT:
        snd_sfx(3, 150, SND_PURE, 10, 1, 0);
        lg("ld: fuel out"); lg_end();
        return;
    }
    if (state != GS_PLAY) return;
    state = GS_RESULT;
    state_timer = 38;                                           /* 3 s */
    hud_text(e == LDE_LANDED ? "THE EAGLE HAS LANDED" : e == LDE_OFFPAD ? "DOWN SAFELY, OFF THE PADS" :
             e == LDE_LOST ? "LOST IN SPACE" : "CRASHED",
             e == LDE_LANDED || e == LDE_OFFPAD ? "SCORE" : ships > 1 ? "TRY AGAIN" : "NO SHIPS LEFT");
    if (e == LDE_LANDED || e == LDE_OFFPAD) numw(46, L.res.score, 5);
}

static void result_done(void)
{
    if (L.outcome == OUT_LANDED || L.outcome == OUT_OFFPAD) {
        ++level;
        lgu("ld: next level=", level); lg_end();
    } else if (--ships == 0) {
        state = GS_OVER;
        state_timer = 25;
        if (score > best) best = score;
        hud_text("GAME OVER", "SCORE         FIRE");
        numw(46, score, 5);
        lgu("ld: gameover score=", score); lgu(" best=", best); lg_end();
        return;
    }
    state = GS_PLAY;
    begin_level();
}

#define in ld_in

static void input(void)
{
    uint8_t held = in_held, edge = in_take(), k = key_take();
    switch (state) {
    case GS_TITLE:
        if (edge & IN_LEFT) { sel_world = (sel_world + 3) & 3; hud_title(); }
        if (edge & IN_RIGHT) { sel_world = (sel_world + 1) & 3; hud_title(); }
        if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case GS_PLAY:
        if ((edge & IN_START) || k == KEY_P) { paused ^= 1; lgu("ld: paused=", paused); lg_end(); }
        if (k == KEY_ESC) { to_title(); break; }
        if (k == KEY_B) { autopilot ^= 1; lgu("ld: autopilot=", autopilot); lg_end(); }
        if (((edge & IN_SELECT) || k == KEY_E) && steady < 10) ++steady;
        if (((edge & IN_OPTION) || k == KEY_Q) && steady > 0) --steady;
        if (autopilot) break;                                   /* it holds the controls */
        in.aim = 0;
        in.pitch = (held & IN_UP) ? 1 : (held & IN_DOWN) ? -1 : 0;
        in.roll = (held & IN_LEFT) ? 1 : (held & IN_RIGHT) ? -1 : 0;
        in.throttle = (held & IN_FIRE) ? 255 : (uint8_t)(steady * 25 + (steady == 10 ? 5 : 0));
        break;
    case GS_OVER:
        if (!state_timer && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
}

static void step(void)
{
    uint8_t e;
    if (state == GS_RESULT) {
        if (!--state_timer) result_done();
        return;
    }
    if (state == GS_OVER) {
        if (state_timer) --state_timer;
        return;
    }
    if (paused) return;
    if (autopilot && !(L.t & 1)) ld_autopilot();             /* port: it decides 6 times a second */
    ld_step();
    while ((e = ld_pop_event()) != LDE_NONE) outcome(e);
    if (state == GS_TITLE && L.outcome && L.t > 0) {
        if (!state_timer) state_timer = 25;
        else if (!--state_timer) { level = (uint8_t)(level % 9 + 1); begin_level(); autopilot = 1; }
    }
}

/* the steps due by the clock (12.5 a second, up to 3 behind); mid-draw
   (between the ground's rows) only while playing, so a new level is never
   made under the drawing */
static uint8_t rt_last;
static uint16_t ms;
static void catch_up(uint8_t mid)
{
    uint8_t n = (uint8_t)(RTCLOK2 - rt_last);
    rt_last += n;
    ms += n * (frame_is_pal() ? 20 : 17);
    if (ms > 240) ms = 240;
    while (ms >= 80) {
        if (mid && (state != GS_PLAY || paused)) break;
        ms -= 80;
        step();
    }
}

/* port: called by view.c between the ground's rows, so the controls, the
   physics and the craft's sprites keep up while a frame is drawn */
void ld_service(void)
{
    uint8_t held;
    if (state != GS_PLAY) return;
    if (!autopilot) {
        held = in_held;
        in.pitch = (held & IN_UP) ? 1 : (held & IN_DOWN) ? -1 : 0;
        in.roll = (held & IN_LEFT) ? 1 : (held & IN_RIGHT) ? -1 : 0;
        in.throttle = (held & IN_FIRE) ? 255 : (uint8_t)(steady * 25 + (steady == 10 ? 5 : 0));
    }
    catch_up(1);
    view_sprites(L.outcome ? 0 : L.throttle);
}

int main(void)
{
    uint8_t y;
    uint16_t frames = 0, fps_t0 = 0;

    a3d_double = 1;
    a3d_init(V_LINES, 2);
    a3d_hud_c1 = 0x0E;
    a3d_hud_c2 = 0x02;
    for (y = 0; y < V_HORIZON - 1; ++y) a3d_dl_flag(y, 0x80);
    VDSLST = (uint16_t)ld_dli;
    view_init();
    sound_init();
    in_install();
    frame_init();
    to_title();
    rt_last = RTCLOK2;
    lg("ld: ready"); lg_end();
    for (;;) {
        input();
        a3d_sync();
        catch_up(0);                                            /* 12.5 steps a second */
        sound_frame();
        view_draw();
        view_sprites(L.outcome ? 0 : L.throttle);
        if (state == GS_PLAY && !(++hud_tick & 3)) hud_play();
        a3d_show();
        ld_probe[0] = state; ld_probe[1] = L.outcome; ld_probe[2] = autopilot; ld_probe[3] = ships;
        ld_probe[4] = level; ld_probe[5] = world; ld_probe[6] = L.throttle; ld_probe[7] = paused;
        ++frames;
        if (frame_count - fps_t0 >= 250) {
            lgu("ld: fps10=", (uint16_t)((uint32_t)frames * 500 / (frame_count - fps_t0))); lgu(" draw=", view_cycles); lgi(" alt=", ld_agl() >> 4); lg_end();
            frames = 0;
            fps_t0 = frame_count;
        }
    }
    return 0;
}
