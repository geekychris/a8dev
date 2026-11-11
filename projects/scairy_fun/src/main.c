/*
 * ScAIry Fun (Atari edition): the front end. The simulation (world.c) runs
 * at the original's fixed 25 steps a second whatever the frame rate; the
 * road (render.c) and the sprites (sprites.c) are drawn as often as the
 * CPU allows. Text: the HUD above the road (ANTIC 6), the AI's lines and
 * the status below it (ANTIC 2).
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "sc.h"
#include "render.h"
#include "sprites.h"
#include "music.h"

extern void display_install(void);
extern void os_off(void);

#define HUD   render_hud()                /* 20 (ANTIC 6) */
#define MSG   render_msg()                /* 2 x 40 (ANTIC 2) */

static uint8_t music_on = 1, paused;
static uint8_t page;                      /* the title's text page */
static uint16_t page_t;

/* the probe a test reads */
struct {
    uint16_t frames, steps, renders, late;
    uint8_t state, route, ap, cover;
    int32_t score;
    int16_t seg, d, v;
} sc_probe;

/* ------------------------------------------------------------ log */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logi(const char *k, int16_t v)
{
    a8_log_s(k);
    if (v < 0) { a8_log_c('-'); v = -v; }
    a8_log_u((uint16_t)v);
}
static void logl(const char *k, int32_t v)
{
    static char b[11];
    static uint8_t i;
    a8_log_s(k);
    i = 10;
    b[10] = 0;
    if (v < 0) v = 0;
    do { b[--i] = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    a8_log_s(b + i);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logi(k, v) ((void)0)
#define logl(k, v) ((void)0)
#define LOGNL() ((void)0)
#endif

/* ------------------------------------------------------------ text */

static uint8_t scode(char c)
{
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return c;
}

static void put(uint8_t *d, const char *s, uint8_t colour)
{
    while (*s) *d++ = scode(*s++) | colour;
}

static void put_centre(uint8_t *d, uint8_t width, const char *s, uint8_t colour)
{
    uint8_t n = (uint8_t)strlen(s);
    memset(d, 0, width);
    put(d + (n < width ? (width - n) >> 1 : 0), s, colour);
}

static void put_num(uint8_t *d, uint32_t v, uint8_t digits, uint8_t colour)
{
    static const uint32_t p10[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    static uint8_t i, c;
    for (i = 6 - digits; i < 6; ++i) {
        c = 0x10;
        while (v >= p10[i]) { v -= p10[i]; ++c; }
        *d++ = c | colour;
    }
}

/* ------------------------------------------------------------ sound */

static void sfx(uint8_t ev)
{
    switch (ev) {
    case EV_AP_ON:     snd_sfx(0, 40, SND_PURE, 10, 1, -2); break;
    case EV_AP_OFF:    snd_sfx(0, 60, SND_PURE, 10, 1, 3); break;
    case EV_SAVE:      snd_sfx(1, 30, SND_PURE, 14, 0, -1); break;
    case EV_NEEDLESS:  snd_sfx(1, 120, SND_BUZZ, 10, 1, 2); break;
    case EV_SCRAPE:    snd_sfx(0, 20, SND_NOISE, 12, 1, 0); break;
    case EV_CONE:      snd_sfx(0, 80, SND_PURE, 8, 2, 0); break;
    case EV_DEBRIS:
    case EV_REAREND:
    case EV_CRASH:
    case EV_HIT_ANIMAL:
    case EV_ROLL:      snd_sfx(1, 200, SND_NOISE, 15, 0, 1); break;
    case EV_SPLASH:    snd_sfx(1, 10, SND_NOISE, 15, 0, 1); break;
    case EV_TICKET:    snd_sfx(1, 90, SND_BUZZ, 12, 1, 0); break;
    case EV_PHANTOM:   snd_sfx(0, 50, SND_BUZZ, 12, 1, 0); break;
    case EV_BLINDED:   snd_sfx(0, 25, SND_PURE, 12, 1, -1); break;
    case EV_MULT_UP:   snd_sfx(0, 35, SND_PURE, 8, 1, -3); break;
    case EV_ARRIVED:   snd_sfx(1, 45, SND_PURE, 14, 0, -1); break;
    case EV_GAME_OVER: snd_sfx(1, 100, SND_BUZZ, 15, 0, 2); break;
    case EV_UNAVAILABLE: snd_sfx(0, 150, SND_BUZZ, 8, 1, 0); break;
    }
}

static void music_start(void)
{
    if (!music_on) return;
    music_play(2, music_lead, 4);
    music_play(3, music_bass, 4);
}

/* ------------------------------------------------------------ high scores */

typedef struct { int32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 30000, "AI4" }, { 20000, "AI3" }, { 12000, "AI2" }, { 6000, "AI1" }, { 2000, "BOT" } };

static uint8_t hi_rank(int32_t s)
{
    static uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

static void enter_initials(uint8_t rank)
{
    static char name[4], line[24];
    static uint8_t pos, p, k, i;
    static char c;
    strcpy(name, "A  ");
    pos = 0;
    logu("scairy: hiscore rank=", rank + 1);
    LOGNL();
    for (;;) {
        put_centre(MSG, 40, "A HIGH SCORE! YOUR INITIALS:", 0);
        strcpy(line, "    ");
        strcat(line, name);
        put_centre(MSG + 40, 40, line, 0);
        frame_wait();
        input_read();
        p = input_pressed();
        k = key_get();
        c = k != KEY_NONE ? key_ascii(k) : 0;
        if (c >= 'A' && c <= 'Z') { name[pos] = c; p = IN_FIRE; }
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & IN_FIRE) {
            if (++pos == 3) break;
            name[pos] = name[pos - 1];
        }
    }
    for (i = 4; i > rank; --i) hiscores[i] = hiscores[i - 1];
    hiscores[rank].score = w.score;
    memcpy(hiscores[rank].name, name, 3);
    hiscores[rank].name[3] = 0;
    a8_log_s("scairy: name=");
    a8_log_s(hiscores[rank].name);
    LOGNL();
}

/* ------------------------------------------------------------ the HUD */

static const char *const pages[][2] = {
    { "YOU RIDE IN A CAR THAT DRIVES ITSELF.", "POINTS FOR EVERY METRE ON AUTOPILOT." },
    { "IT HAS BLIND SPOTS: TAKE THE WHEEL", "JUST BEFORE IT GETS SOMETHING WRONG." },
    { "STICK STEERS AND BRAKES (TAKES OVER),", "FIRE HANDS IT BACK. START PAUSES." },
    { "NO WARNINGS: READ THE ROAD, AND ITS", "PATH (THE BLUE LINE) AHEAD." },
};

static void hud(void)
{
    static char buf[24];
    static int32_t s_score;
    static int16_t s_cover, s_mult, s_kmh;
    static uint8_t s_msg = 0xFF, s_state = 0xFF, s_ap = 0xFF, s_route = 0xFF;
    static int16_t kmh;
    uint8_t *h = HUD;

    if (w.state == GS_TITLE) {
        if (s_state != GS_TITLE) {
            put_centre(h, 20, "SCAIRY FUN", 0x00);
            s_state = GS_TITLE;
            page_t = 0;
            page = 0;
        }
        if (page_t == 0) {
            if (page < 4) {
                put_centre(MSG, 40, pages[page][0], 0);
                put_centre(MSG + 40, 40, pages[page][1], 0);
            } else if (page == 4) {
                put_centre(MSG, 40, "HIGH SCORES", 0);
                memset(MSG + 40, 0, 40);
                for (buf[0] = 0; buf[0] < 3; ++buf[0]) {
                    put(MSG + 40 + 1 + buf[0] * 13, hiscores[buf[0]].name, 0);
                    put_num(MSG + 40 + 5 + buf[0] * 13, (uint32_t)hiscores[buf[0]].score, 6, 0);
                }
            } else {
                put_centre(MSG, 40, "PRESS FIRE OR START", 0);
                put_centre(MSG + 40, 40, "OPTION: MUSIC ON/OFF", 0);
            }
        }
        if (++page_t == 150) { page_t = 0; page = page == 5 ? 0 : page + 1; }
        return;
    }
    if (s_state != w.state || s_route != w.route) {
        s_state = w.state;
        s_route = w.route;
        memset(h, 0, 20);
        s_score = -1;
        s_cover = s_mult = s_kmh = -1;
        s_msg = s_ap = 0xFF;
    }
    if (w.score != s_score) { s_score = w.score; put_num(h, (uint32_t)w.score, 6, 0x40); }
    if (w.mult != s_mult) {
        s_mult = w.mult;
        h[7] = scode('X') | 0x80;
        h[8] = (0x10 + w.mult) | 0x80;
    }
    if (w.cover != s_cover) {
        s_cover = w.cover;
        put(h + 11, "COVER", 0xC0);
        put_num(h + 17, (uint32_t)w.cover, 3, w.cover < 30 ? 0x80 : 0xC0);
    }
    /* below the road: the AI's line, then the status */
    if (w.state == GS_INTRO || w.state == GS_ARRIVED || w.state == GS_UPDATE ||
        w.state == GS_GAMEOVER || w.state == GS_WIN) {
        if (s_msg != 0xFE) {
            s_msg = 0xFE;
            if (w.state == GS_INTRO) {
                strcpy(buf, route_info[w.route].name);
                put_centre(MSG, 40, buf, 0);
                strcpy(buf, "TO ");
                strcat(buf, route_info[w.route].dest);
                put_centre(MSG + 40, 40, buf, 0);
            } else if (w.state == GS_ARRIVED) {
                put_centre(MSG, 40, msg_text[MSG_ARRIVED], 0);
                memset(MSG + 40, 0, 40);
                put(MSG + 41, "AUTOPILOT", 0);
                put_num(MSG + 51, (uint32_t)world_ap_pct(), 3, 0);
                MSG[54] = scode('%');
                put(MSG + 57, "BONUS", 0);
                put_num(MSG + 63, (uint32_t)w.route_bonus, 6, 0);
            } else if (w.state == GS_UPDATE) {
                strcpy(buf, "SOFTWARE UPDATE ");
                strcat(buf, route_info[w.route + 1].ai_name);
                put_centre(MSG, 40, buf, 0);
                put_centre(MSG + 40, 40, msg_text[MSG_UPDATED], 0);
            } else {
                put_centre(MSG, 40, w.state == GS_WIN ? "YOU ARE HOME. WELL SUPERVISED!" : msg_text[MSG_CANCELLED], 0);
                put_centre(MSG + 40, 40, "PRESS FIRE", 0);
            }
        }
        return;
    }
    if (w.msg != s_msg) {
        s_msg = w.msg;
        put_centre(MSG, 40, msg_text[w.msg], 0);
    }
    kmh = KMH(w.car.v);
    if (w.ai.on != s_ap || kmh != s_kmh) {
        s_ap = w.ai.on;
        s_kmh = kmh;
        memset(MSG + 40, 0, 40);
        put(MSG + 41, w.ai.on ? "AUTOPILOT ON" : "YOU ARE DRIVING", 0);
        put_num(MSG + 57, (uint32_t)(kmh < 0 ? 0 : kmh), 3, 0);
        put(MSG + 61, "KM/H", 0);
        put(MSG + 67, route_info[w.route].ai_name, 0);
        MSG[66] = scode('V');
    }
}

/* ------------------------------------------------------------ events */

static void events(void)
{
    static Event e;
    while (world_pop_event(&e)) {
        sfx(e.type);
        if (e.type == EV_SAY || e.type == EV_MULT_UP) continue;
        a8_log_s("scairy: ev=");
        a8_log_s(ev_name[e.type]);
        logi(" a=", e.a);
        logi(" seg=", seg_of(w.car.z));
        logu(" cover=", (uint16_t)w.cover);
        logl(" score=", w.score);
        LOGNL();
    }
}

/* ------------------------------------------------------------ main */

int main(void)
{
    static uint8_t f, in, acc, steps, prev_state, k;
    static uint16_t idle;
    static int32_t final_score;

    os_off();                             /* (the renderer runs under the OS ROM) */
    render_init();
    sprites_init();
    COLOR0 = 0x1A;                        /* the HUD: copper, gold, red, white */
    COLOR1 = 0x1E;
    COLOR2 = 0x34;
    COLOR3 = 0x0E;
    COLOR4 = 0x00;
    display_install();
    SDLSTL = (uint16_t)render_dl();
    SDMCTL = 0x22 | 0x0C | 0x10;          /* normal width, players + missiles, 1-line */
    sound_init();
    frame_init();
    world_init();
    a8_log_s("scairy: ready\n");
    a8_log_s("scairy: title\n");
    music_start();
    prev_state = w.state;
    acc = 0;
    for (;;) {
        f = frame_wait();
        sc_probe.frames += f;
        in = input_read();
        k = input_pressed();
        if (k & IN_OPTION) { music_on ^= 1; if (music_on) music_start(); else music_stop(); }
        if (key_get() == KEY_ESC && w.state != GS_TITLE) {      /* port: ESC back to the title */
            world_init();
            a8_log_s("scairy: title\n");
        }
        if ((k & IN_START) && (w.state == GS_PLAY)) {
            paused ^= 1;
            put_centre(MSG, 40, paused ? "PAUSED - START GOES ON" : "", 0);
            a8_log_s(paused ? "scairy: paused\n" : "scairy: resumed\n");
        }
        if (paused) continue;
        /* the simulation: 25 steps a second (PAL: one each 2 frames;
           NTSC: 5 in 12), at most 3 behind */
        acc += f * (frame_is_pal() ? 6 : 5);
        steps = 0;
        while (acc >= 12 && steps < 3) {
            acc -= 12;
            world_update(in & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN | IN_FIRE | IN_START));
            events();
            steps++;
            sc_probe.steps++;
            if (w.state != prev_state) {
                a8_log_s("scairy: state=");
                logu("", w.state);
                logu(" route=", w.route + 1);
                logl(" score=", w.score);
                logu(" cover=", (uint16_t)w.cover);
                LOGNL();
                if (prev_state == GS_GAMEOVER || prev_state == GS_WIN) {
                    /* the title again: a high score? */
                    a8_log_s("scairy: title\n");
                }
                if (w.state == GS_GAMEOVER || w.state == GS_WIN) {
                    logu("scairy: perf frames=", sc_probe.frames);
                    logu(" renders=", sc_probe.renders);
                    logu(" steps=", sc_probe.steps);
                    LOGNL();
                }
                prev_state = w.state;
            }
            if (w.state == GS_GAMEOVER || w.state == GS_WIN) final_score = w.score;
            if (w.state == GS_TITLE && final_score > 0) {   /* from the end: a high score? */
                if (hi_rank(final_score) < 5) {
                    w.score = final_score;
                    enter_initials(hi_rank(final_score));
                    w.score = 0;
                }
                final_score = 0;
            }
        }
        if (acc >= 12) { sc_probe.late++; acc = 0; }
        render_frame();
        sprites_frame();
        hud();
        sc_probe.renders++;
        sc_probe.state = w.state;
        sc_probe.route = w.route;
        sc_probe.ap = w.ai.on;
        sc_probe.cover = (uint8_t)w.cover;
        sc_probe.score = w.score;
        sc_probe.seg = seg_of(w.car.z);
        sc_probe.d = w.car.d;
        sc_probe.v = w.car.v;
        if ((sc_probe.renders & 63) == 0 && w.state == GS_PLAY) {
            logu("scairy: perf frames=", sc_probe.frames);
            logu(" renders=", sc_probe.renders);
            logu(" steps=", sc_probe.steps);
            logu(" seg=", sc_probe.seg);
            LOGNL();
        }
        (void)idle;
    }
}
