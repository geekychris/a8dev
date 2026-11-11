/*
 * Planet Chomp for the Atari 8-bit: a port of amiga_mcp/examples/planet_chomp
 * (the 3DO / Amiga version of the Unity game), after the Spectrum Next
 * port. A maze chase on a tiny planet: eat every crumb, dodge the four
 * spooks (GLOOM, SPARK, MIST, MOSS), grab a golden key to turn the tables.
 *
 * Controls: the joystick (or W A S D, the arrow keys) steers, relative to
 * the screen: a turn waits for the next junction. START (or FIRE) starts;
 * the attract mode (the original's autopilot) starts after 15 s on the
 * title. P or SELECT pauses.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "game.h"
#include "view.h"

extern void vbi_install(void);
extern void os_off(void);

typedef struct { char name[4]; int32_t score; } Hs;
static Hs hs[5] = { { "PAC", 20000L }, { "AMI", 10000L }, { "BOI", 5000L }, { "NGA", 2500L }, { "CHO", 1000L } };
static uint8_t entry, ed_pos, ed_place;
static char ed_name[4] = "AAA";
static uint8_t paused;
extern uint8_t take_latch(void);
extern uint8_t in_held;                 /* the joystick and console keys at the last VBI */
uint16_t steps, late;                  /* steps run; frames behind (the tests read them) */
/* where test.py looks: crumbs left, the chomper moving, the score, the state */
#ifndef A8_RELEASE
extern uint8_t pc_ways;
extern void test_ways(void);
void * const pc_probe[5] = { &g.crumbs_left, &g.player.moving, &g.score, &g.state, &pc_ways };
#endif

/* port: in asm (fastmath.s: cc65's 32-bit runtime took 14 000 cycles a number) */
extern uint32_t fmt_v;
extern char * __fastcall__ fmt_num(uint8_t w);
static char *num(int32_t v, uint8_t w)
{
    fmt_v = (uint32_t)v;
    return fmt_num(w);
}

static void hud(void)
{
    static uint8_t t;
    view_clear_text();
    if (entry) {
        view_text(1, "HIGH SCORE! YOUR INITIALS:");
        view_text(28, ed_name);
        return;
    }
    if (g.state == GS_TITLE || (g.state == GS_OVER && !g.demo)) {
        if (((t++) >> 6) & 1) {
            view_text(1, "PLANET CHOMP  START: PLAY");
        } else {
            view_text(1, "HI");
            view_text(4, hs[0].name);
            view_text(8, num(hs[0].score, 6));
            view_text(16, "2ND");
            view_text(20, hs[1].name);
            view_text(24, num(hs[1].score, 6));
        }
        if (g.state == GS_OVER) view_text(30, "GAME OVER");
        return;
    }
    view_text(0, num(g.score, 6));
    view_text(8, "LIVES");
    view_text(14, num(g.lives, 1));
    view_text(17, "LEVEL");
    view_text(23, num(g.level, 2));
    view_text(27, "CRUMBS");
    view_text(34, num(g.crumbs_left, 3));
    if (g.demo) view_text(38, "D");
    if (paused) view_text(14, "PAUSED");
    else if (g.state == GS_READY) view_text(12, "  READY!  ");
}

static void sounds(void)
{
    static uint8_t plink;
    uint8_t s = g.sfx;
    g.sfx = 0;
    if (s & SFX_CRUMB) { plink ^= 1; snd_sfx(2, plink ? 40 : 50, SND_PURE, 8, 3, 0); }
    if (s & SFX_KEY) snd_sfx(3, 60, SND_PURE, 12, 1, -1);
    if (s & SFX_EAT) snd_sfx(3, 30, SND_PURE, 14, 1, 2);
    if (s & SFX_DEATH) snd_sfx(3, 20, SND_PURE, 15, 0, 1);
    if (s & SFX_CLEAR) snd_sfx(3, 25, SND_PURE, 14, 1, -1);
    if (s & SFX_EXTRA) snd_sfx(1, 30, SND_PURE, 14, 1, -1);
    if (s & SFX_START) snd_sfx(1, 80, SND_PURE, 12, 1, -2);
    if (s & (SFX_CRUMB | SFX_KEY)) view_cell_changed(g.player.from);
    if (g.state == GS_PLAYING && g.powered) snd_tone(0, (uint8_t)(100 + ((g.clock >> 1) & 15)), SND_PURE, 3);
    else if (g.state == GS_PLAYING) snd_tone(0, (uint8_t)(160 + ((g.clock >> 2) & 7)), SND_PURE, 2);
    else snd_off(0);
}

static int8_t hs_rank(int32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hs[i].score) return (int8_t)i;
    return -1;
}

static void entry_input(uint8_t edge, uint8_t key)
{
    char c = key != KEY_NONE ? key_ascii(key) : 0;
    if (c >= 'A' && c <= 'Z') { ed_name[ed_pos] = c; if (ed_pos < 2) ed_pos++; }
    if (edge & IN_UP) ed_name[ed_pos] = ed_name[ed_pos] == 'Z' ? 'A' : (char)(ed_name[ed_pos] + 1);
    if (edge & IN_DOWN) ed_name[ed_pos] = ed_name[ed_pos] == 'A' ? 'Z' : (char)(ed_name[ed_pos] - 1);
    if ((edge & IN_RIGHT) && ed_pos < 2) ed_pos++;
    if ((edge & IN_LEFT) && ed_pos > 0) ed_pos--;
    if ((edge & (IN_FIRE | IN_START)) || c == '\n') {
        uint8_t i;
        for (i = 4; i > ed_place; --i) hs[i] = hs[i - 1];
        memcpy(hs[ed_place].name, ed_name, 4);
        hs[ed_place].score = g.score;
        a8_log("pc: hiscore name=%s score=%ld place=%u\n", ed_name, g.score, ed_place);
        entry = 0;
        g.state = GS_TITLE;
        g.timer = 0;
    }
}

int main(void)
{
    uint8_t n, held, edge, key, last_state = 0xFF, due = 0, stepped;
    os_off();
    pmg_init();
    sound_init();
    view_init();
    vbi_install();
    GPRIOR = 0x11;                          /* players over the maze; the missiles are one player (PF3) */
    game_init();
    frame_init();
    rnd_seed(1);
    a8_log("pc: ready\n");
    for (;;) {
        n = frame_wait();

        /* port: from the VBI (os.s): a press during a slow frame isn't lost */
        held = in_held;
        edge = take_latch();
        key = key_get();
        if (entry) { entry_input(edge, key); hud(); continue; }
        if (key == 10 || (edge & IN_SELECT)) { if (g.state == GS_PLAYING && !g.demo) paused ^= 1; }
        if ((g.state == GS_TITLE || g.state == GS_OVER) && (edge & (IN_START | IN_FIRE)) && g.timer > 12) {
            game_start(0);
            a8_log("pc: game demo=0\n");
        } else if (g.demo && (edge & (IN_START | IN_FIRE))) {
            g.demo = 0;
            g.state = GS_TITLE;
            g.timer = 0;
        } else if (g.state == GS_TITLE && g.timer > 375) {
            game_start(1);                  /* the attract mode */
            a8_log("pc: game demo=1\n");
        }
        /* the rules: 25 steps a second, one every second frame */
        due += n;
        if (due > 4) { late += due - 4; due = 4; }
        stepped = 0;
        if (!paused)
            while (due >= 2) {
                stepped = 1;
                due -= 2;
                game_step(held, edge);
#ifndef A8_RELEASE
                test_ways();
#endif
                ++steps;
                edge = 0;
                sounds();
            }
        if (g.state != last_state) {
            last_state = g.state;
            if (g.state == GS_OVER && !g.demo) {
                int8_t r = hs_rank(g.score);
                a8_log("pc: gameover score=%ld\n", g.score);
                if (r >= 0) { entry = 1; ed_place = (uint8_t)r; ed_pos = 0; memcpy(ed_name, "AAA", 4); }
            }
            if (g.state == GS_OVER && g.demo && g.timer == 0) { /* the demo ends: back to the title */ }
        }
        if (g.state == GS_OVER && g.demo && g.timer > 75) { g.demo = 0; g.state = GS_TITLE; g.timer = 0; }
        /* port: the movers move only in a step: draw after one (25 a second) */
        if (stepped || paused || g.state == GS_TITLE) view_frame();
        if ((g.clock & 7) == 0 || entry) {
            /* port: in play only when something on it changes (num is slow) */
            static int32_t h_score = -1;
            static uint16_t h_misc;
            uint16_t misc = (uint16_t)(g.lives ^ (g.level << 3) ^ (g.crumbs_left << 6) ^ (g.state << 12) ^ (paused << 15));
            if (entry || g.state == GS_TITLE || g.state == GS_OVER || g.score != h_score || misc != h_misc) {
                hud();
                h_score = g.score;
                h_misc = misc;
            }
        }
    }
}
