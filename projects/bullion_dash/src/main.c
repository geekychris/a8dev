/*
 * Bullion Dash (Atari 8-bit). Port of amiga_mcp/examples/bullion_dash by way
 * of the ZX Spectrum port: player.c, enemy.c, level.c and play.c are the
 * original's rules, game_tick() below its main loop's states. This file is
 * the Atari front end: title (level select, the editor), high scores with
 * initials, POKEY effects, the 50 Hz loop and the log the test reads.
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "level.h"
#include "play.h"
#include "editor.h"
#include "render.h"

GameState gs;
uint8_t sfx_pending;

static int16_t transition_frames;
static int16_t start_level = 1;     /* port: the title chooses the first level */
static uint8_t test_play;           /* playing the editor's level */
static uint8_t need_room;           /* the play screen must be redrawn */

/* ------------------------------------------------------------ log */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logs(const char *k, int16_t v)
{
    a8_log_s(k);
    if (v < 0) { a8_log_c('-'); v = -v; }
    a8_log_u((uint16_t)v);
}
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
#define logs(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

/* ------------------------------------------------------------ sound */

/* port: the original makes dig, gold, death and level samples but never
   plays them; the Spectrum port plays them, and so does this one */
static void sound(uint8_t s)
{
    switch (s) {
    case SFX_DIG:   snd_sfx(1, 30, SND_NOISE, 10, 1, 2); break;
    case SFX_GOLD:  snd_sfx(0, 60, SND_PURE, 12, 1, -5); break;
    case SFX_TRAP:  snd_sfx(1, 120, SND_PURE, 12, 1, 6); break;
    case SFX_DEATH: snd_sfx(0, 80, SND_PURE, 15, 1, 5); break;
    case SFX_LEVEL: snd_sfx(0, 120, SND_PURE, 15, 1, -3); break;
    case SFX_OVER:  snd_sfx(0, 100, SND_PURE, 15, 0, 4); break;
    }
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {           /* (the original has no table) */
    { 500, "GLD" }, { 400, "BAR" }, { 300, "DIG" }, { 200, "RUN" }, { 100, "LDR" } };
static uint8_t entry_rank = 5, entry_pos;
static char entry_name[4];

static uint8_t hi_rank(uint32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

/* ------------------------------------------------------------ title */

static void title_level(void)
{
    static char b[4];
    b[0] = '0' + start_level / 100;
    b[1] = '0' + (start_level / 10) % 10;
    b[2] = '0' + start_level % 10;
    b[3] = 0;
    title_print(19, 9, b, 2);
}

static void title_screen(void)
{
    uint8_t i, d;
    static char buf[16];
    uint32_t v;
    title_clear();
    title_print(0, 4, "BULLION DASH", 3);
    title_print(2, 1, "GET ALL THE GOLD,", 1);
    title_print(3, 1, "DIG TRAPS FOR THE", 1);
    title_print(4, 1, "GUARDS, CLIMB OUT", 1);
    title_print(6, 4, "HIGH SCORES", 3);
    for (i = 0; i < 5; ++i) {
        v = hiscores[i].score;
        strcpy(buf, "1 AAA 000000");
        buf[0] = '1' + i;
        memcpy(buf + 2, hiscores[i].name, 3);
        for (d = 11; d >= 6; --d) { buf[d] = '0' + (char)(v % 10); v /= 10; }
        title_print(7 + i, 4, buf, 0);
    }
    title_print(13, 0, "JOYSTICK RUN, CLIMB", 2);
    title_print(14, 0, "FIRE+LEFT/RIGHT DIG", 2);
    title_print(15, 0, "ESC GIVES UP A LIFE", 2);
    title_print(16, 0, "E EDITOR", 2);
    title_print(18, 1, "PRESS FIRE", 3);
    title_print(19, 1, "LEVEL <     >", 2);
    title_level();
    render_title();
}

/* ------------------------------------------------------------ logging */

static const char *const state_names[7] = {"title", "playing", "dying", "level_done", "gameover", "editor", "entry"};
#define STATE_ENTRY 6               /* port: initials for the high-score table */

static void log_state(void)
{
    static int16_t last_state = -1, last_lives = -1, last_level = -1, last_gold = -1;
    static uint32_t last_score = 0xFFFFFFFFUL;
    if (gs.state != last_state) {
        a8_log_s("bullion: state=");
        a8_log_s(state_names[gs.state]);
        logs(" level=", gs.level_num);
        logs(" lives=", gs.lives);
        a8_log_s(" score=");
        logscore(gs.score);
        LOGNL();
        last_state = gs.state;
    }
    if (gs.score != last_score) { a8_log_s("bullion: score="); logscore(gs.score); LOGNL(); last_score = gs.score; }
    if (gs.lives != last_lives) { logs("bullion: lives=", gs.lives); LOGNL(); last_lives = gs.lives; }
    if (gs.level_num != last_level) { logs("bullion: level=", gs.level_num); LOGNL(); last_level = gs.level_num; }
    if (gs.gold_collected != last_gold) {
        logs("bullion: gold=", gs.gold_collected);
        logs("/", gs.gold_total);
        LOGNL();
        last_gold = gs.gold_collected;
    }
}

/* ------------------------------------------------------------ the game (the original's main loop) */

static void start_level_play(void)
{
    level_load(gs.level_num);
    gs.state = STATE_PLAYING;
    need_room = 1;
}

static void die(const char *why)
{
    a8_log_s("bullion: death ");
    a8_log_s(why);
    logs(" at ", gs.player.gx);
    logs(",", gs.player.gy);
    LOGNL();
    gs.state = STATE_DYING;
    gs.player.state = PS_DEAD;
    transition_frames = 30;
    sound(SFX_DEATH);
}

static void back_to_editor(void)
{
    test_play = 0;
    gs.state = STATE_EDITOR;
    editor_resume();
}

/* one 50 Hz frame of the original's loop; held/pressed = IN_* bits */
static void game_tick(uint8_t held, uint8_t pressed, char letter)
{
    switch (gs.state) {
    case STATE_TITLE:
        if (pressed & (IN_FIRE | IN_START)) {
            gs.score = 0;
            gs.lives = 5;
            gs.level_num = start_level;
            start_level_play();
            logs("bullion: game started at level ", start_level);
            LOGNL();
        } else if (letter == 'E') {
            gs.state = STATE_EDITOR;
            editor_init();
            a8_log_s("bullion: editor\n");
        } else if (pressed & (IN_LEFT | IN_RIGHT | IN_SELECT)) {     /* port: level select */
            start_level += (pressed & IN_LEFT) ? -1 : 1;
            if (start_level < 1) start_level = MAX_LEVELS;
            if (start_level > MAX_LEVELS) start_level = 1;
            title_level();
        }
        break;

    case STATE_PLAYING:
        switch (play_tick(held, letter == 27)) {
        case PLAY_CAUGHT:  die("caught"); break;
        case PLAY_BURIED:  die("buried"); break;
        case PLAY_GAVE_UP: die("gave up"); break;
        case PLAY_WON:
            gs.state = STATE_LEVEL_DONE;
            transition_frames = 60;
            sound(SFX_LEVEL);
            logs("bullion: level ", gs.level_num);
            a8_log_s(" complete\n");
            break;
        }
        break;

    case STATE_DYING:
        if (--transition_frames > 0) break;
        if (test_play) { back_to_editor(); break; }
        gs.lives--;
        if (gs.lives <= 0) {
            gs.state = STATE_GAMEOVER;
            transition_frames = 30;
            sound(SFX_OVER);
            a8_log_s("bullion: gameover score=");
            logscore(gs.score);
            LOGNL();
        } else {
            start_level_play();     /* reload the current level */
        }
        break;

    case STATE_LEVEL_DONE:
        if (--transition_frames > 0) break;
        if (test_play) { back_to_editor(); break; }
        gs.score += 1000;
        gs.level_num++;
        if (gs.level_num > MAX_LEVELS) gs.level_num = 1;
        start_level_play();
        break;

    case STATE_GAMEOVER:
        if (transition_frames > 0)
            transition_frames--;
        else if (pressed & (IN_FIRE | IN_START)) {
            entry_rank = hi_rank(gs.score);
            if (entry_rank < 5) {
                gs.state = STATE_ENTRY;
                strcpy(entry_name, "A  ");
                entry_pos = 0;
                logu("bullion: hiscore rank=", entry_rank + 1);
                LOGNL();
            } else {
                gs.state = STATE_TITLE;
            }
        }
        break;

    case STATE_EDITOR:
        switch (editor_update(held, pressed, letter)) {
        case EDITOR_TEST:
            test_play = 1;
            gs.score = 0;
            gs.lives = 1;
            gs.state = STATE_PLAYING;
            need_room = 1;
            a8_log_s("bullion: test play\n");
            break;
        case EDITOR_EXIT:
            gs.state = STATE_TITLE;
            break;
        }
        break;

    case STATE_ENTRY:
        if (letter >= 'A' && letter <= 'Z') {
            entry_name[entry_pos] = letter;
            pressed |= IN_FIRE;
        }
        if (pressed & IN_UP) entry_name[entry_pos] = entry_name[entry_pos] == 'Z' ? 'A' : entry_name[entry_pos] + 1;
        if (pressed & IN_DOWN) entry_name[entry_pos] = entry_name[entry_pos] == 'A' ? 'Z' : entry_name[entry_pos] - 1;
        if (pressed & IN_FIRE) {
            if (++entry_pos == 3) {
                uint8_t i;
                for (i = 4; i > entry_rank; --i) hiscores[i] = hiscores[i - 1];
                hiscores[entry_rank].score = gs.score;
                memcpy(hiscores[entry_rank].name, entry_name, 3);
                hiscores[entry_rank].name[3] = 0;
                a8_log_s("bullion: name=");
                a8_log_s(hiscores[entry_rank].name);
                LOGNL();
                gs.state = STATE_TITLE;
                break;
            }
            entry_name[entry_pos] = entry_name[entry_pos - 1];
        }
        break;
    }
}

/* for the test bot (test.py reads it by symbol) */
struct { uint16_t ticks, draws; } bullion_perf;

int main(void)
{
    static int16_t prev_state;
    static uint8_t ticks, t, held, pressed, k;
    static char letter;

    render_init();
    level_init();
    sound_init();
    frame_init();
    gs.lives = 5;
    gs.level_num = 1;
    gs.state = STATE_TITLE;
    title_screen();
    a8_log_s("bullion: ready\n");

    for (;;) {
        prev_state = gs.state;
        ticks = frame_wait();       /* fixed timestep: the logic runs at 50 Hz */
        if (ticks > 3) ticks = 3;

        held = input_read();
        pressed = input_pressed();
        letter = 0;
        k = key_get();
        if (k != KEY_NONE) letter = k == KEY_ESC ? 27 : key_ascii(k);

        for (t = 0; t < ticks; t++) {
            game_tick(held, pressed, letter);
            pressed = 0;            /* an edge counts once */
            letter = 0;
            gs.frame++;
            if (gs.state == STATE_PLAYING) bullion_perf.ticks++;
            log_state();
            if (gs.state != prev_state || need_room) break;
        }
        if (sfx_pending) {
            sound(sfx_pending);
            sfx_pending = 0;
        }

        if (need_room) {
            need_room = 0;
            render_room(0);
            render_game_screen();
        }
        switch (gs.state) {
        case STATE_TITLE:
            if (prev_state != STATE_TITLE) {
                title_screen();
                a8_log_s("bullion: title\n");
            }
            break;
        case STATE_PLAYING:
            if (prev_state != STATE_PLAYING) render_msg(0, 0);
            render_frame(1);
            render_status(0);
            bullion_perf.draws++;
            if ((bullion_perf.draws & 255) == 0) {
                logu("bullion: perf ticks=", bullion_perf.ticks);
                logu(" draws=", bullion_perf.draws);
                LOGNL();
            }
            break;
        case STATE_DYING:
            render_frame((transition_frames & 3) >= 2);     /* the original's blink */
            render_status(0);
            break;
        case STATE_LEVEL_DONE:
            if (prev_state != STATE_LEVEL_DONE) {
                render_frame(1);
                render_status(0);
                render_msg("LEVEL COMPLETE!", "+1000 BONUS!");
            }
            break;
        case STATE_GAMEOVER:
            if (prev_state != STATE_GAMEOVER) {
                render_frame(0);
                render_status(0);
                render_msg("GAME OVER", "PRESS FIRE");
            }
            break;
        case STATE_ENTRY:
            {
                static char buf[] = "INITIALS: ...";
                memcpy(buf + 10, entry_name, 3);
                render_msg("NEW HIGH SCORE!", buf);
            }
            break;
        case STATE_EDITOR:
            if (prev_state != STATE_EDITOR) render_game_screen();
            editor_draw();
            break;
        }
    }
}
