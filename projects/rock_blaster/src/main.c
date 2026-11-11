/*
 * Rock Blaster (Atari 8-bit). Port of amiga_mcp/examples/rock_blaster.
 *
 * game.c is the original's logic; this file is the Atari front end: title
 * screen with a high-score table, initials entry, POKEY sound and the
 * original's tune, the main loop, and the log lines the test reads. The
 * logic runs at a fixed 50 Hz: when drawing takes longer than a frame, the
 * game catches up (frame_wait() says how many frames passed), so it plays
 * at the original's speed whatever the frame rate.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "render.h"
#include "music.h"

#define STATE_ENTRY 4                     /* port: initials for the table */

static uint8_t start_wave = 1, music_on = 1;
static uint16_t fps_start;              /* the log's frame rate: since */

/* ------------------------------------------------------------ log */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logscore(uint32_t v)
{
    char b[11];
    uint8_t i = 10;
    b[10] = 0;
    do { b[--i] = '0' + (char)(v % 10); v /= 10; } while (v);
    a8_log_s(b + i);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

/* ------------------------------------------------------------ sound */

/* snd.s plays them in the vertical blank */
extern uint8_t fx_freq[2], fx_dist[2], fx_vol[2], fx_decay[2], fx_sweep[2];
extern void snd_start(void);
extern void __fastcall__ snd_music(uint8_t on);
extern void snd_silence(void);

static void fx(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol, uint8_t decay, int8_t sweep)
{
    fx_vol[ch] = 0;
    fx_freq[ch] = freq;
    fx_dist[ch] = dist;
    fx_decay[ch] = decay;
    fx_sweep[ch] = (uint8_t)sweep;
    fx_vol[ch] = vol;
}

void sfx_shoot(void)         { fx(0, 30, SND_PURE, 10, 1, 6); }
void sfx_explode_large(void) { fx(1, 200, SND_NOISE, 15, 1, 2); }
void sfx_explode_small(void) { fx(1, 60, SND_NOISE, 12, 1, 3); }
void sfx_die(void)           { fx(1, 240, SND_NOISE, 15, 1, 1); }

static void tune_start(void)
{
    if (music_on) snd_music(1);
}

static void tune_stop(void)
{
    snd_music(0);
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 10000, "ROK" }, { 7500, "AST" }, { 5000, "BLS" }, { 2500, "VEC" }, { 1000, "PEW" } };
static uint8_t entry_rank, entry_pos;
static char entry_name[4];

static uint8_t hi_rank(uint32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

static uint32_t top_score(void)
{
    return gs_score > hiscores[0].score ? gs_score : hiscores[0].score;
}

/* ------------------------------------------------------------ screens */

static void draw_title(void)
{
    uint8_t i, d;
    char buf[16];
    uint32_t v;
    title_clear();
    title_print(0, 4, "ROCK BLASTER");
    title_print(1, 3, "HIGH SCORES");
    for (i = 0; i < 5; ++i) {
        v = hiscores[i].score;
        strcpy(buf, "1. AAA  000000");
        buf[0] = '1' + i;
        memcpy(buf + 3, hiscores[i].name, 3);
        for (d = 13; d >= 8; --d) { buf[d] = '0' + (char)(v % 10); v /= 10; }
        title_print(2 + i, 13, buf);
    }
    title_print(8, 1, "LEFT/RIGHT TURN  UP THRUST  FIRE SHOOT");
    title_print(10, 7, "SELECT WAVE 1   OPTION MUSIC ON ");
    title_print(13, 10, "PRESS FIRE OR START");
}

static void draw_title_options(void)
{
    char b[2];
    b[0] = '0' + start_wave;
    b[1] = 0;
    title_print(10, 19, b);
    title_print(10, 36, music_on ? "ON " : "OFF");
}

static void show_title(void)
{
    tune_stop();
    draw_title();
    draw_title_options();
    render_title_screen();
    tune_start();
    a8_log_s("rb: title\n");
}

static void hud_all(void)
{
    hud_num(0, 0, gs_score, 6);
    hud_print(0, 8, "W");
    hud_num(0, 9, gs_level, 2);
    hud_print(0, 13, "SHIPS");
    hud_num(0, 18, gs_lives > 0 ? gs_lives : 0, 1);
}

static void hud_hi(void)
{
    hud_clear(1);
    hud_print(1, 5, "HI");
    hud_num(1, 8, top_score(), 6);
}

static void show_game(void)
{
    render_game_screen();
    hud_all();
    hud_hi();
}

static void entry_draw(void)
{
    char buf[4];
    memcpy(buf, entry_name, 3);
    buf[3] = 0;
    hud_clear(1);
    hud_print(1, 1, "YOUR INITIALS:");
    hud_print(1, 16, buf);
}

static uint8_t entry_tick(uint8_t pressed)
{
    uint8_t k = key_get();
    char c = (k != KEY_NONE) ? key_ascii(k) : 0;
    if (c == '\n') c = 0, pressed |= IN_FIRE;
    if (c >= 'A' && c <= 'Z') {
        entry_name[entry_pos] = c;
        pressed |= IN_FIRE;
    }
    if (pressed & IN_UP) entry_name[entry_pos] = entry_name[entry_pos] == 'Z' ? 'A' : entry_name[entry_pos] + 1;
    if (pressed & IN_DOWN) entry_name[entry_pos] = entry_name[entry_pos] == 'A' ? 'Z' : entry_name[entry_pos] - 1;
    if (pressed & IN_FIRE) {
        if (++entry_pos == 3) return 1;
        entry_name[entry_pos] = entry_name[entry_pos - 1];
    }
    entry_draw();
    return 0;
}

/* ------------------------------------------------------------ main */

static const char *const state_names[] = { "title", "playing", "dead", "gameover", "entry" };

int main(void)
{
    uint8_t in = 0, p, n, prev_state, title_angle = 0, last_lives = 0xFF, last_level = 0;
    uint16_t draws = 0;
    uint32_t last_score = 0;

    render_init();
    snd_start();
    rnd_seed(12345);
    particles_table();
    gs_state = STATE_TITLE;
    frame_init();
    show_title();
    logu("rb: ready pal=", frame_is_pal() ? 1 : 0);
    LOGNL();

    for (;;) {
        n = frame_wait();
        if (n > 2) n = 2;             /* catch up at most a frame */
        in = input_read();
        p = input_pressed();
        prev_state = gs_state;

        if (gs_state == STATE_TITLE) {
            if (p & IN_SELECT) { start_wave = start_wave == 5 ? 1 : start_wave + 1; draw_title_options(); }
            if (p & IN_OPTION) {
                music_on ^= 1;
                draw_title_options();
                if (music_on) tune_start(); else tune_stop();
            }
            title_ship(title_angle += 2);
            if (p & (IN_FIRE | IN_START)) {
                rnd_seed(12345 + frame_count);
                game_init();
                if (start_wave > 1) {           /* port: SELECT starts at a later wave */
                    uint8_t i;
                    for (i = 0; i < MAX_ROCKS; ++i) rk_active[i] = 0;
                    gs_rock_count = 0;
                    gs_level = start_wave;
                    game_spawn_rocks(TUNE_START_ROCKS + (gs_level - 1) * TUNE_ROCKS_PER_LEVEL);
                    hits_build();
                }
                last_score = 0;
            }
        } else if (gs_state == STATE_ENTRY) {
            if (entry_tick(p)) {
                uint8_t i;
                for (i = 4; i > entry_rank; --i) hiscores[i] = hiscores[i - 1];
                hiscores[entry_rank].score = gs_score;
                memcpy(hiscores[entry_rank].name, entry_name, 3);
                hiscores[entry_rank].name[3] = 0;
                a8_log_s("rb: name=");
                a8_log_s(hiscores[entry_rank].name);
                LOGNL();
                gs_state = STATE_TITLE;
            }
        } else {
            while (n--) {
                game_update((in & IN_LEFT) != 0, (in & IN_RIGHT) != 0, (in & IN_UP) != 0, (in & IN_FIRE) != 0);
                if (gs_state == STATE_TITLE) {          /* game over's 3 s are up */
                    entry_rank = hi_rank(gs_score);
                    if (entry_rank < 5) {
                        gs_state = STATE_ENTRY;
                        strcpy(entry_name, "A  ");
                        entry_pos = 0;
                        entry_draw();
                        logu("rb: hiscore rank=", entry_rank + 1);
                        LOGNL();
                    }
                    break;
                }
            }
        }

        if (gs_state != prev_state) {
            a8_log_s("rb: state=");
            a8_log_s(state_names[gs_state]);
            a8_log_s(" score=");
            logscore(gs_score);
            logu(" lives=", gs_lives);
            logu(" level=", gs_level);
            LOGNL();
            if (prev_state == STATE_TITLE) {
                show_game();
                tune_start();
                fps_start = frame_count;
                draws = 0;
                last_lives = gs_lives;
                last_level = gs_level;
            } else if (gs_state == STATE_TITLE) {
                show_title();
            } else if (gs_state == STATE_GAMEOVER) {
                hud_clear(1);
                hud_print(1, 5, "GAME OVER");
                a8_log_s("rb: gameover score=");
                logscore(gs_score);
                logu(" overruns=", frame_overruns);
                LOGNL();
            }
        }
        if (gs_state == STATE_TITLE || gs_state == STATE_ENTRY) continue;

        render_world();
        ++draws;
        if (gs_score != last_score) {
            a8_log_s("rb: score=");
            logscore(gs_score);
            LOGNL();
            last_score = gs_score;
            hud_num(0, 0, gs_score, 6);
            if (gs_score > hiscores[0].score && gs_state != STATE_GAMEOVER) hud_num(1, 8, gs_score, 6);
        }
        if (gs_lives != last_lives) {
            logu("rb: lives=", gs_lives > 0 ? gs_lives : 0);
            LOGNL();
            last_lives = gs_lives;
            hud_all();
        }
        if (gs_level != last_level) {
            logu("rb: level=", gs_level);
            LOGNL();
            last_level = gs_level;
            hud_all();
        }
        {
            if ((uint16_t)(frame_count - fps_start) >= 250) {
                logu("rb: fps=", (uint16_t)((uint32_t)draws * 50 / (uint16_t)(frame_count - fps_start)));
                {
                    uint8_t i, r = 0;
                    for (i = 0; i < MAX_ROCKS; ++i) r += rk_active[i];
                    logu(" rocks=", r);
                }
                logu(" particles=", particles_live);
                LOGNL();
                fps_start = frame_count;
                draws = 0;
            }
        }
    }
}
