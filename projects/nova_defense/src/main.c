/*
 * Nova Defense (Atari 8-bit). Port of amiga_mcp/examples/nova_defense.
 *
 * game.c is the original's logic; this is the Atari front end: title with
 * a high-score table, initials entry, POKEY sound (the march, the effects,
 * the background arpeggio), the main loop at a fixed 50 Hz, and the log
 * lines the test reads.
 */
#pragma static-locals(on)     /* locals in static memory: much faster code from cc65 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "render.h"

#define STATE_ENTRY 5

static uint8_t music_on = 1;

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

/* the original's march (three rising notes) and arpeggio, as MIDI notes */
static const uint8_t march_notes[3] = { 53, 56, 59 };
static const a8_note melody[] = {
    {57, 12}, {60, 12}, {64, 12}, {60, 12}, {57, 12}, {55, 12}, {57, 12}, {60, 12},
    {62, 12}, {64, 12}, {67, 12}, {64, 12}, {62, 12}, {60, 12}, {57, 12}, {55, 12}, {0, 0} };

static void sounds(void)
{
    if (ev_shoot) snd_sfx(0, 40, SND_PURE, 10, 1, 4);
    if (ev_alien_hit) snd_sfx(1, 60, SND_NOISE, 14, 1, 4);
    if (ev_ufo_hit) snd_sfx(0, 120, SND_PURE, 15, 1, -6);
    if (ev_player_hit) snd_sfx(1, 200, SND_NOISE, 15, 0, 2);
    if (ev_march) snd_sfx(2, snd_note_freq[march_notes[march_note] - 48], SND_PURE, 10, 2, 0);
    ev_shoot = ev_alien_hit = ev_ufo_hit = ev_player_hit = ev_march = 0;
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 3000, "NOV" }, { 2500, "ADA" }, { 2000, "DEF" }, { 1500, "ENS" }, { 1000, "ORB" } };
static uint8_t entry_rank, entry_pos;
static char entry_name[4];

static uint8_t hi_rank(uint32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

/* ------------------------------------------------------------ screens */

static void draw_title(void)
{
    uint8_t i, d;
    char buf[16];
    uint32_t v;
    title_clear();
    title_print(0, 4, "NOVA DEFENSE");
    title_print(1, 4, "30 EYE PODS");
    title_print(2, 4, "20 RAYS");
    title_print(3, 4, "10 SPORES");
    title_print(4, 4, "?? THE COMET");
    title_print(5, 4, "HIGH SCORES");
    for (i = 0; i < 5; ++i) {
        v = hiscores[i].score;
        strcpy(buf, "1 AAA 000000");
        buf[0] = '1' + i;
        memcpy(buf + 2, hiscores[i].name, 3);
        for (d = 11; d >= 6; --d) { buf[d] = '0' + (char)(v % 10); v /= 10; }
        title_print(6 + i, 4, buf);
    }
    title_print(12, 0, "JOYSTICK MOVES, FIRE");
    title_print(13, 0, "OPTION: MUSIC ON");
    title_print(15, 1, "PRESS FIRE TO START");
}

static void show_title(void)
{
    music_stop();
    draw_title();
    title_print(13, 14, music_on ? "ON " : "OFF");
    render_title_screen();
    a8_log_s("nova: title\n");
}

static void hud_all(void)
{
    hud_clear(0);
    hud_num(0, 0, score, 6);
    hud_print(0, 8, "WAVE");
    hud_num(0, 13, wave, 2);
    hud_clear(1);
    hud_print(1, 0, "LIVES");
    hud_num(1, 6, lives > 0 ? lives : 0, 1);
    hud_print(1, 10, "HI");
    hud_num(1, 13, hiscore > hiscores[0].score ? hiscore : hiscores[0].score, 6);
}

static void entry_draw(void)
{
    static char buf[] = "INITIALS ...";
    memcpy(buf + 9, entry_name, 3);
    render_message(buf);
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

static const char *const state_names[] = { "title", "playing", "dying", "gameover", "waveclear", "entry" };

int main(void)
{
    uint8_t in, p, n, prev, last_lives = 0xFF, last_wave = 0;
    uint32_t last_score = 0;
    uint16_t loops = 0, fps_start = 0;

    render_init();
    sound_init();
    rnd_seed(4242);
    game_init();
    hiscore = 0;
    frame_init();
    show_title();
    logu("nova: ready pal=", frame_is_pal() ? 1 : 0);
    LOGNL();

    for (;;) {
        n = frame_wait();
        if (n > 2) n = 2;
        in = input_read();
        p = input_pressed();
        prev = state;

        if (state == STATE_TITLE) {
            if (p & IN_OPTION) {
                music_on ^= 1;
                title_print(13, 14, music_on ? "ON " : "OFF");
            }
            title_anim((uint8_t)frame_count);
            if (p & (IN_FIRE | IN_START)) {
                rnd_seed(4242 + frame_count);
                game_update(0, 0, 1);
            }
        } else if (state == STATE_ENTRY) {
            if (entry_tick(p)) {
                uint8_t i;
                for (i = 4; i > entry_rank; --i) hiscores[i] = hiscores[i - 1];
                hiscores[entry_rank].score = score;
                memcpy(hiscores[entry_rank].name, entry_name, 3);
                hiscores[entry_rank].name[3] = 0;
                a8_log_s("nova: name=");
                a8_log_s(hiscores[entry_rank].name);
                LOGNL();
                state = STATE_TITLE;
            }
        } else {
            while (n--) {
                game_update((in & IN_LEFT) != 0, (in & IN_RIGHT) != 0, (p & IN_FIRE) != 0);
                p = 0;                        /* a press counts once */
                sounds();
                if (state != STATE_PLAYING) break;
            }
            if (state == STATE_TITLE) {       /* game over's time is up */
                entry_rank = hi_rank(score);
                if (entry_rank < 5) {
                    state = STATE_ENTRY;
                    strcpy(entry_name, "A  ");
                    entry_pos = 0;
                    entry_draw();
                    logu("nova: hiscore rank=", entry_rank + 1);
                    LOGNL();
                }
            }
        }

        if (state != prev) {
            a8_log_s("nova: state=");
            a8_log_s(state_names[state]);
            a8_log_s(" score=");
            logscore(score);
            logu(" lives=", lives > 0 ? lives : 0);
            logu(" wave=", wave);
            LOGNL();
            if (prev == STATE_TITLE) {
                render_game_screen();
                hud_all();
                if (music_on) music_play(3, melody, 4);
                last_score = score;
                last_lives = lives;
                last_wave = wave;
                fps_start = frame_count;
                loops = 0;
            } else if (state == STATE_TITLE) {
                show_title();
            } else if (state == STATE_WAVE_CLEAR) {
                render_message("WAVE CLEAR");
            } else if (prev == STATE_WAVE_CLEAR && state == STATE_PLAYING) {
                render_message(0);
                render_swarm_all();
                hud_all();
            } else if (state == STATE_GAMEOVER) {
                music_stop();
                render_message("GAME OVER");
                a8_log_s("nova: gameover score=");
                logscore(score);
                LOGNL();
            }
        }
        if (state == STATE_TITLE || state == STATE_ENTRY) continue;

        render_frame();
        ++loops;
        if (score != last_score) {
            a8_log_s("nova: score=");
            logscore(score);
            LOGNL();
            last_score = score;
            hud_num(0, 0, score, 6);
            if (score > hiscores[0].score) hud_num(1, 13, score, 6);
        }
        if (lives != last_lives) {
            logu("nova: lives=", lives > 0 ? lives : 0);
            LOGNL();
            last_lives = lives;
            hud_all();
        }
        if (wave != last_wave) {
            logu("nova: wave=", wave);
            LOGNL();
            last_wave = wave;
        }
        if ((uint16_t)(frame_count - fps_start) >= 250) {
            logu("nova: fps=", (uint16_t)((uint32_t)loops * 50 / (uint16_t)(frame_count - fps_start)));
            logu(" aliens=", alive_count);
            LOGNL();
            fps_start = frame_count;
            loops = 0;
        }
    }
}
