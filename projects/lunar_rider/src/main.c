/*
 * Lunar Rider - the main loop (the original's main.c): the title, the game
 * at a fixed 50 Hz, game over, and the high-score table (added: the
 * original only shows the current score as HI).
 *
 * Log (tests): "lunar: ready", "lunar: state=..." at every state change,
 * "lunar: score=N" when it changes, "lunar: death cause=... lives=N",
 * "lunar: checkpoint zone=N", "lunar: hiscore rank=N name=ABC",
 * "lunar: perf frames=100 late=N" every 100 frames of play.
 */
#include <string.h>
#include <stdlib.h>
#include "a8game.h"
#include "a8log.h"
#include "game.h"
#include "view.h"
#include "sound.h"

/* the log without printf (a8_log's vsnprintf is 1.3K): "key=" then a number */
#ifdef A8_NO_LOG
#define LOG_S(s)      ((void)0)
#define LOG_N(k, v)   ((void)0)
#define LOG_END()     ((void)0)
#else
#define LOG_S(s)      a8_log_s(s)

static void LOG_N(const char *k, long v)
{
    char b[12];
    LOG_S(k);
    ltoa(v, b, 10);
    LOG_S(b);
}
#define LOG_END()     a8_log_c('\n')
#endif

static InputState inp;
static uint8_t in_now, in_new;

typedef struct { uint32_t score; char name[4]; } hi_t;
static hi_t hi[5] = {
    {10000, "LDF"}, {7500, "MPT"}, {5000, "BUG"}, {2500, "MOO"}, {1000, "A8 "},
};

/* for the test's bot (emu.read_mem("lunar_probe", 8)); it reads the
   terrain ahead itself (emu.read_mem("terrain", ...)) */
typedef struct {
    uint8_t state, lives, alive, on_ground;
    uint32_t scroll_px;
} probe_t;
probe_t lunar_probe;

#ifndef A8_NO_LOG
static const char *const state_name[5] = {"TITLE", "PLAYING", "DYING", "GAMEOVER", "CHECKPOINT"};
static const char *const cause_name[6] = {"", "crater", "rock", "mine", "bomb", "enemy"};
#endif

/* the SDK's input_read() (joystick 0, console keys, W/A/S/D and SPACE),
   in fewer cycles */
static uint8_t input_now(void)
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
        case KEY_A: case 6:  r |= IN_LEFT; break;
        case KEY_D: case 7:  r |= IN_RIGHT; break;
        case KEY_W: case 14: r |= IN_UP; break;
        case KEY_S: case 15: r |= IN_DOWN; break;
        case KEY_SPACE:      r |= IN_FIRE; break;
        case KEY_RETURN:     r |= IN_START; break;
        }
    }
    return r;
}

static void read_input(void)
{
    uint8_t old = in_now;
    in_now = input_now();
    in_new = (uint8_t)(in_now & ~old);
    inp.left = in_now & IN_LEFT;
    inp.right = in_now & IN_RIGHT;
    inp.jump = in_now & IN_UP;
    inp.fire = in_now & IN_FIRE;
}

static void probe(void)
{
#ifndef A8_NO_LOG
    lunar_probe.state = (uint8_t)gs.state;
    lunar_probe.lives = (uint8_t)gs.lives;
    lunar_probe.alive = gs.buggy.alive;
    lunar_probe.on_ground = gs.buggy.on_ground;
    lunar_probe.scroll_px = gs.scroll_px;
#endif
}

static void log_state(void)
{
    LOG_S("lunar: state=");
    LOG_S(state_name[gs.state]);
    LOG_N(" score=", gs.score);
    LOG_N(" lives=", gs.lives);
    LOG_N(" zone=", gs.checkpoint_cur + 1);
    LOG_END();
}

/* ---- high scores */
static uint8_t hi_rank(uint32_t score)
{
    uint8_t i;
    for (i = 0; i < 5; ++i)
        if (score > hi[i].score) return i;
    return 5;
}

static void title_page(uint8_t page)
{
    char line[41];
    uint8_t i;
    if (!page) {
        title_row(0, "LEFT/RIGHT  SPEED     UP  JUMP");
        title_row(1, "FIRE  SHOOT FORWARD AND UP");
        title_row(2, "JUMP CRATERS AND ROCKS, DODGE BOMBS");
        title_row(3, "KEYS: A/D SPEED  W JUMP  SPACE FIRE");
        return;
    }
    title_row(0, "HIGH SCORES");
    for (i = 0; i < 5; i += 2) {
        uint8_t j, k = 0;
        for (j = i; j < i + 2 && j < 5; ++j) {
            uint32_t v = hi[j].score;
            uint8_t d;
            if (j > i) { line[k++] = ' '; line[k++] = ' '; line[k++] = ' '; }
            line[k++] = (char)('1' + j);
            line[k++] = ' ';
            memcpy(line + k, hi[j].name, 3);
            k += 3;
            line[k++] = ' ';
            for (d = 6; d; --d) {
                line[k + d - 1] = (char)('0' + (uint8_t)(v % 10));
                v /= 10;
            }
            k += 6;
        }
        line[k] = 0;
        title_row((uint8_t)(1 + i / 2), line);
    }
}

static void enter_initials(uint8_t rank)
{
    char name[4] = "A__";
    char shown[24];
    uint8_t pos = 0, rep = 0, i;
    uint8_t k;

    sky_clear();
    sky_msg(0, "NEW HIGH SCORE", 0);
    sky_msg(2, "ENTER YOUR INITIALS", 2);
    LOG_N("lunar: initials rank=", rank + 1);
    LOG_END();
    key_get();
    while (pos < 3) {
        frame_wait();
        snd_tick();
        read_input();
        k = key_get();
        if (k != KEY_NONE) {
            char c = key_ascii(k);
            if (c >= 'A' && c <= 'Z') {
                name[pos++] = c;
                if (pos < 3) name[pos] = 'A';
                snd_shoot();
            } else if (c == '\n' || c == ' ') {
                if (name[pos] == '_') name[pos] = 'A';
                ++pos;
                if (pos < 3) name[pos] = 'A';
            }
        } else if (in_now & (IN_UP | IN_DOWN)) {
            if (!rep) {
                char c = name[pos];
                if (in_now & IN_UP) c = c >= 'Z' ? 'A' : c + 1;
                else c = c <= 'A' ? 'Z' : c - 1;
                name[pos] = c;
                rep = 8;
            } else {
                --rep;
            }
        } else {
            rep = 0;
        }
        if (in_new & (IN_FIRE | IN_RIGHT | IN_START)) {
            ++pos;
            if (pos < 3) name[pos] = 'A';
            snd_shoot();
        }
        for (i = 0; i < 3; ++i) {
            shown[i * 2] = name[i];
            shown[i * 2 + 1] = ' ';
        }
        shown[5] = 0;
        sky_msg(3, shown, 1);
    }
    for (i = 4; i > rank; --i) hi[i] = hi[i - 1];
    hi[rank].score = (uint32_t)gs.score;
    memcpy(hi[rank].name, name, 3);
    hi[rank].name[3] = 0;
    LOG_N("lunar: hiscore rank=", rank + 1);
    LOG_S(" name=");
    LOG_S(hi[rank].name);
    LOG_N(" score=", gs.score);
    LOG_END();
}

/* ---- the title screen */
static void title(void)
{
    static uint8_t first = 1;
    uint16_t t = 0;
    uint32_t mpx = 0;

    game_init(&gs);
    gs.state = STATE_TITLE;
    snd_silence();
    view_title();
    title_page(0);
    bottom_row(0, "JOYSTICK IN PORT 1");
    title_buggy(BUGGY_X);
    log_state();
    if (first) {
        LOG_S("lunar: ready\n");
        first = 0;
    }
    for (;;) {
        frame_wait();
        ++t;
        mpx += 2;                         /* the mountains drift by */
        view_scroll(mpx, 0);
        title_buggy(BUGGY_X);
        if ((t % 250) == 0) title_page((uint8_t)((t / 250) & 1));
        bottom_row(1, (t & 32) ? "PRESS FIRE TO START" : "");
        read_input();
        if (t > 10 && (in_new & (IN_FIRE | IN_START | IN_UP))) {
            bottom_row(1, "GET READY");     /* (the course takes ~0.7 s to make) */
            break;
        }
    }
}

/* ---- a game */
static void play(void)
{
    int16_t prev;
    int32_t last_score = -1;
    uint16_t perf_frames = 0, perf_late = 0;
    uint8_t late;

    game_init(&gs);
    gs.state = STATE_PLAYING;
    view_hiscore = hi[0].score;
    view_game();
    frame_wait();
    snd_music(1);
    log_state();
    in_now = input_now();                /* the button that started is held */

    for (;;) {
        late = frame_wait();
        view_sprites();                 /* the state game_update left last frame */
        snd_tick();
        read_input();
        prev = gs.state;
        if (gs.state == STATE_GAMEOVER && gs.state_timer < 130 && (in_new & (IN_FIRE | IN_START)))
            gs.state = STATE_TITLE;      /* port: a new press, after a second */
        else
            game_update(&gs, &inp);

        if (gs.ev_shoot) snd_shoot();
        if (gs.ev_jump) snd_jump();
        if (gs.ev_explode) snd_explode();
        if (gs.ev_death) {
            snd_death();
            LOG_S("lunar: death cause=");
            LOG_S(cause_name[gs.death_cause]);
            LOG_N(" lives=", gs.lives);
            LOG_N(" x=", (long)gs.scroll_px);
            LOG_END();
        }
        if (gs.ev_checkpoint) {
            snd_checkpoint();
            LOG_N("lunar: checkpoint zone=", gs.checkpoint_cur + 1);
            LOG_N(" score=", gs.score);
            LOG_END();
        }
        /* the bands for the new state: the VBI applies them with the frame
           whose sprites the next view_sprites() draws */
        view_scroll(gs.scroll_px, gs.scroll_px);
        view_ground();
        if (gs.score > (int32_t)view_hiscore) view_hiscore = (uint32_t)gs.score;
        view_hud();

        if (gs.state != prev) {
            log_state();
            switch (gs.state) {
            case STATE_CHECKPOINT: {
                char z[8] = "ZONE 00";
                z[5] = (char)('0' + (gs.checkpoint_cur + 1) / 10);
                z[6] = (char)('0' + (gs.checkpoint_cur + 1) % 10);
                sky_msg(1, z, 2);
                sky_msg(2, "1000 BONUS", 0);
                break;
            }
            case STATE_PLAYING:
                sky_clear();
                break;
            case STATE_GAMEOVER: {
                char s[12];
                uint32_t v = (uint32_t)gs.score;
                uint8_t d;
                snd_music(0);
                view_hide_sprites();
                sky_clear();
                sky_msg(1, "GAME OVER", 3);
                for (d = 6; d; --d) { s[d - 1] = (char)('0' + (uint8_t)(v % 10)); v /= 10; }
                s[6] = 0;
                sky_msg(3, s, 0);
                break;
            }
            case STATE_TITLE: {
                uint8_t r = hi_rank((uint32_t)gs.score);
                snd_music(0);
                if (r < 5) enter_initials(r);
                return;
            }
            }
        }
        if (gs.score != last_score) {
            last_score = gs.score;
#ifndef A8_NO_LOG
            {   /* (from the HUD's digits: ltoa of a long is ~2,000 cycles) */
                extern uint8_t score_dig[6];
                char b[8];
                uint8_t i = 0, k = 0;
                while (i < 5 && !score_dig[i]) ++i;
                while (i < 6) b[k++] = (char)('0' + score_dig[i++]);
                b[k] = 0;
                LOG_S("lunar: score=");
                LOG_S(b);
                LOG_END();
            }
#endif
        }
        probe();
        ++perf_frames;
        if (late > 1) perf_late += late - 1;
        if (perf_frames == 100) {
            LOG_N("lunar: perf frames=100 late=", perf_late);
            LOG_END();
            perf_frames = perf_late = 0;
        }
    }
}

int main(void)
{
    rnd_seed(54321u);
    snd_init();
    view_init();
    frame_init();
    for (;;) {
        title();
        play();
    }
}
