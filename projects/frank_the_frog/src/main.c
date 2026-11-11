/*
 * Frank the Frog (Atari 8-bit). Port of amiga_mcp/examples/frank_the_frog.
 *
 * game_tick() is the original main loop's state machine (main.c), with the
 * original's rules in lanes.c, frank.c and score.c. This file adds the
 * Atari front end: title screen with a high-score table, initials entry,
 * POKEY sound and music, and the log lines the test reads.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "lanes.h"
#include "frank.h"
#include "score.h"
#include "render.h"

#define STATE_ENTRY 5            /* port: initials for the high-score table */

static uint8_t game_state = STATE_TITLE;
static uint8_t transition_frames;
static uint8_t start_level = 1, music_on = 1, home_dirty;

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 5000, "FRK" }, { 4000, "TAD" }, { 3000, "LIL" }, { 2000, "PAD" }, { 1000, "HOP" } };
static uint8_t entry_rank, entry_pos;
static char entry_name[4];

/* ------------------------------------------------------------ sound */

static const a8_note riff[] = {   /* the original's fast E-F chromatic riff */
    {52, 6}, {52, 3}, {53, 3}, {52, 6}, {55, 6}, {52, 6}, {52, 3}, {53, 3},
    {52, 6}, {57, 6}, {52, 6}, {52, 3}, {53, 3}, {55, 6}, {53, 6}, {0, 0} };

enum { S_HOP, S_SPLAT, S_SPLASH, S_HOME, S_LEVEL, S_OVER };

static void sound(uint8_t s)
{
    switch (s) {
    case S_HOP:    snd_sfx(0, 60, SND_PURE, 10, 2, -6); break;
    case S_SPLAT:  snd_sfx(1, 40, SND_NOISE, 15, 1, 3); break;
    case S_SPLASH: snd_sfx(1, 8, SND_NOISE, 14, 1, 2); break;
    case S_HOME:   snd_sfx(0, 80, SND_PURE, 14, 1, -3); break;
    case S_LEVEL:  snd_sfx(0, 120, SND_PURE, 15, 1, -4); break;
    case S_OVER:   snd_sfx(0, 60, SND_PURE, 15, 1, 4); break;
    }
}

/* ------------------------------------------------------------ log */

/* port (speed): the frequent lines without printf (a8_log's vsnprintf takes
   more than a frame); all of it compiles away in a release build */
#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logscore(uint32_t v)
{
    if (v < 65536UL) a8_log_u((uint16_t)v);
    else a8_log("%lu", v);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

static const char *const state_names[] = { "title", "playing", "dying", "level", "gameover", "entry" };

static void die(const char *why, uint8_t snd)
{
    a8_log_s("frank: death ");
    a8_log_s(why);
    logu(" row=", frog_row);
    logu(" col=", frank_col());
    LOGNL();
    frank_start_death();
    game_state = STATE_DYING;
    sound(snd);
}

/* ------------------------------------------------------------ the game */

static uint8_t hi_rank(uint32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

static void new_game(void)
{
    score_init();
    score_level = start_level;
    frank_init();
    lanes_init(start_level);
    lanes_reset_home();
}

static void game_tick(int8_t joy_dx, int8_t joy_dy, uint8_t fire_pressed)
{
    int8_t ride_dx, slot;
    uint8_t death_done;

    switch (game_state) {
    case STATE_TITLE:
        if (fire_pressed) {
            game_state = STATE_PLAYING;
            new_game();
        }
        break;

    case STATE_PLAYING:
        if (joy_dx || joy_dy) {
            uint8_t old_highest = frank_highest_row;
            frank_move(joy_dx, joy_dy);
            sound(S_HOP);
            if (frank_highest_row < old_highest) score_add(10);
        }

        lanes_tick();

        /* River riding */
        if (frog_row >= ROW_RIVER_5 && frog_row <= ROW_RIVER_1) {
            if (!lanes_check_river(frog_px, (uint8_t)frog_row, &ride_dx)) {
                die("splash", S_SPLASH);
            } else if (ride_dx != 0) {
                int16_t new_x = frog_px + ride_dx;
                if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) die("carried", S_SPLASH);
                else frank_set_x(new_x);
            }
        }

        /* Car collision */
        if (game_state == STATE_PLAYING && frog_row >= ROW_ROAD_5 && frog_row <= ROW_ROAD_1) {
            if (lanes_check_car(frog_px, (uint8_t)frog_row)) die("splat", S_SPLAT);
        }

        /* Home base check */
        if (game_state == STATE_PLAYING && frog_row == ROW_HOME) {
            slot = lanes_check_home(frog_px);
            if (slot >= 0) {
                lanes_fill_home(slot);
                home_dirty = 1;
                score_add(50);
                sound(S_HOME);
                logu("frank: home slot=", slot);
                LOGNL();
                if (lanes_all_home()) {
                    game_state = STATE_LEVEL;
                    transition_frames = 60;
                    sound(S_LEVEL);
                    logu("frank: level ", score_level);
                    a8_log_s(" complete\n");
                } else {
                    frank_init();
                }
            } else {
                die("bank", S_SPLASH);
            }
        }
        break;

    case STATE_DYING:
        death_done = !frank_die_tick();
        lanes_tick();
        if (death_done) {
            score_lose_life();
            if (score_lives == 0) {
                game_state = STATE_GAMEOVER;
                transition_frames = 30;
                sound(S_OVER);
                a8_log_s("frank: gameover score=");
                logscore(score_current);
                LOGNL();
            } else {
                frank_init();
                game_state = STATE_PLAYING;
            }
        }
        break;

    case STATE_LEVEL:
        if (--transition_frames == 0) {
            score_next_level();
            lanes_reset_home();
            lanes_init(score_level);
            frank_init();
            game_state = STATE_PLAYING;
        }
        break;

    case STATE_GAMEOVER:
        lanes_tick();
        if (transition_frames > 0) {
            transition_frames--;
        } else if (fire_pressed) {
            entry_rank = hi_rank(score_current);
            game_state = entry_rank < 5 ? STATE_ENTRY : STATE_TITLE;
        }
        break;
    }
}

/* ------------------------------------------------------------ screens */

static void draw_title(void)
{
    uint8_t i, r;
    char buf[24];
    for (r = 0; r < 16; ++r) title_clear_row(r);
    title_print(0, 2, "HOP FRANK OVER THE ROAD AND RIVER");
    title_print(1, 4, "INTO THE FOUR PONDS ON THE BANK");
    title_print(3, 14, "HIGH SCORES");
    for (i = 0; i < 5; ++i) {
        uint32_t v = hiscores[i].score;
        uint8_t d;
        strcpy(buf, "1. AAA  000000");
        buf[0] = '1' + i;
        memcpy(buf + 3, hiscores[i].name, 3);
        for (d = 13; d >= 8; --d) { buf[d] = '0' + (char)(v % 10); v /= 10; }
        title_print(5 + i, 13, buf);
    }
    title_print(11, 8, "JOYSTICK OR WASD/ARROWS HOP");
    title_print(12, 6, "SELECT LEVEL   OPTION MUSIC ON ");
    title_print(14, 10, "PRESS FIRE OR START");
}

static void draw_title_options(void)
{
    char buf[2];
    buf[0] = '0' + start_level;
    buf[1] = 0;
    title_print(12, 19, buf);
    title_print(12, 34, music_on ? "ON " : "OFF");
}

static void show_title(void)
{
    music_stop();
    draw_title();
    draw_title_options();
    render_title_screen();
    title_face(0);
    a8_log("frank: title\n");
}

static void show_game(void)
{
    render_message("");
    render_start_message("");
    render_lanes_build();
    render_home();
    render_game_screen();
}

static void entry_draw(void)
{
    static char buf[] = "NEW HIGH SCORE! YOUR INITIALS: ...";
    memcpy(buf + 31, entry_name, 3);
    render_message(buf);
}

static void entry_begin(void)
{
    strcpy(entry_name, "A  ");
    entry_pos = 0;
    entry_draw();
    a8_log("frank: hiscore rank=%u\n", entry_rank + 1);
}

static void entry_finish(void)
{
    uint8_t i;
    for (i = 4; i > entry_rank; --i) hiscores[i] = hiscores[i - 1];
    hiscores[entry_rank].score = score_current;
    memcpy(hiscores[entry_rank].name, entry_name, 3);
    hiscores[entry_rank].name[3] = 0;
    a8_log("frank: name=%s\n", hiscores[entry_rank].name);
}

/* letters: typed, or UP/DOWN to choose and FIRE to accept */
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

int main(void)
{
    uint8_t in, p, prev_state, last_lives = 0xFF, last_level = 0, title_t = 0;
    int8_t dx, dy;
    uint32_t last_score = 0xFFFFFFFFUL;

    render_init();
    sound_init();
    frame_init();
    show_title();
    a8_log("frank: ready pal=%u\n", frame_is_pal() ? 1 : 0);

    for (;;) {
        frame_wait();
        in = input_read();
        p = input_pressed();
        dx = dy = 0;
        if (p & IN_UP) dy = -1;
        else if (p & IN_DOWN) dy = 1;
        else if (p & IN_LEFT) dx = -1;
        else if (p & IN_RIGHT) dx = 1;

        prev_state = game_state;
        if (game_state == STATE_TITLE) {
            if (p & IN_SELECT) { start_level = start_level == 4 ? 1 : start_level + 1; draw_title_options(); }
            if (p & IN_OPTION) { music_on ^= 1; draw_title_options(); }
            if (++title_t == 100) title_face(1);
            else if (title_t == 108) { title_face(0); title_t = 0; }
        }
        if (game_state == STATE_ENTRY) {
            if (entry_tick(p)) {
                entry_finish();
                game_state = STATE_TITLE;
            }
        } else {
            game_tick(dx, dy, (p & (IN_FIRE | IN_START)) != 0);
        }
        frank_probe[2] = game_state;

        /* the display follows the state */
        if (game_state != prev_state) {
            a8_log_s("frank: state=");
            a8_log_s(state_names[game_state]);
            a8_log_s(" score=");
            logscore(score_current);
            logu(" lives=", score_lives);
            logu(" level=", score_level);
            LOGNL();
            if (prev_state == STATE_TITLE) {
                show_game();
                if (music_on) music_play(3, riff, 4);
            } else if (game_state == STATE_TITLE) {
                show_title();
            } else if (game_state == STATE_LEVEL) {
                render_message("WELL DONE, FRANK!");
            } else if (prev_state == STATE_LEVEL) {
                render_message("");
                render_lanes_build();
                render_home();
            } else if (game_state == STATE_GAMEOVER) {
                music_stop();
                render_message("GAME OVER - PRESS FIRE");
            } else if (game_state == STATE_ENTRY) {
                entry_begin();
            }
        }
        if (game_state == STATE_TITLE) continue;

        pend_ready = 0;
        render_scroll();
        if (game_state == STATE_PLAYING || game_state == STATE_DYING) render_frank(game_state);
        else render_hide_frank();
        pend_ready = 1;

        if (score_current != last_score) {
            if (last_score != 0xFFFFFFFFUL) {
                a8_log_s("frank: score=");
                logscore(score_current);
                LOGNL();
            }
            last_score = score_current;
            render_hud(score_current, score_hi > hiscores[0].score ? score_hi : hiscores[0].score,
                       score_lives, score_level);
        }
        if (home_dirty) {
            render_home();
            home_dirty = 0;
        }
        if (score_lives != last_lives || score_level != last_level) {
            if (last_lives != 0xFF && score_lives != last_lives) { logu("frank: lives=", score_lives); LOGNL(); }
            if (last_level && score_level != last_level) { logu("frank: level=", score_level); LOGNL(); }
            last_lives = score_lives;
            last_level = score_level;
            render_hud(score_current, score_hi > hiscores[0].score ? score_hi : hiscores[0].score,
                       score_lives, score_level);
        }
        {
            static int16_t last_row = -1;
            if (frog_row != last_row && game_state == STATE_PLAYING) {
                logu("frank: row=", frog_row);
                logu(" col=", frank_col());
                LOGNL();
                last_row = frog_row;
            }
        }
    }
}
