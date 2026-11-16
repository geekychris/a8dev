/*
 * Miner Meteor (Atari 8-bit). Port of amiga_mcp/examples/miner_meteor by
 * way of the ZX Spectrum port: game.c is the original's rules (game.cpp),
 * levels.c its guardians; this file is the Atari front end.
 *
 * The cavern is 14 ANTIC 5 rows (the original's rows 0-13: row 14 is a
 * second wall row under the floor) of 40 characters, two a tile, so a tile
 * is 8 colour clocks x 16 scan lines: the original's 16 x 16 pixels at one
 * scan line a pixel. Walls in COLPF0 (each cavern its colour, the original's
 * palette_variant), belts and crumbling floors COLPF1, spikes and the closed
 * exit COLPF2, keys and the open exit COLPF3 (cycling). Belts move by
 * rewriting their two characters; a crumbling floor sinks a line at a time
 * (8 characters pairs). The miner is player 0, the guardians players 1-3,
 * one scan line a pixel. Status: an ANTIC 6 line below.
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "game.h"
#include "tmap.h"
#include "levels_data.h"
#include "art.h"
#include "demo.h"

#define CHARSET   ((uint8_t *)0xA000)
#define ROOM      ((uint8_t *)0xA400)     /* 14 x 40 */
#define HUD       ((uint8_t *)0xA640)     /* 20 */
#define DL_GAME   ((uint8_t *)0xA700)
#define DL_TITLE  ((uint8_t *)0xA780)
#define TITLE     ((uint8_t *)0xA800)     /* 24 x 20 */
#define ROWS      14
#define SCAN_TOP  16
#define PL(n)     ((uint8_t *)(0x9C00 + ((n) << 8)))
#define BAR_CH    63                      /* '_' becomes a full block: the air bar */

static uint8_t *room_row[ROWS];
static uint8_t frames_l[8 * 16];          /* the miner facing left */
static uint8_t music_on = 1;

/* ------------------------------------------------------------ log, probe */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
/* (by subtraction: cc65's 32-bit division would make the frame late) */
static void logscore(uint32_t v)
{
    static const uint32_t t[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    static char b[7];
    static uint8_t i, n, c;
    for (i = n = 0; i < 6; ++i) {
        c = '0';
        while (v >= t[i]) { v -= t[i]; ++c; }
        if (c != '0' || n || i == 5) b[n++] = c;
    }
    b[n] = 0;
    a8_log_s(b);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

/* what the test's bot reads (and the speed) */
struct {
    uint16_t level_ticks;
    uint8_t mode, level, lives, keys;
    int16_t x, y;
    uint16_t frames, ticks, overruns;
} miner_probe;

static void update_probe(void)
{
    miner_probe.level_ticks = gs.level_ticks;
    miner_probe.mode = gs.mode;
    miner_probe.level = gs.level_index;
    miner_probe.lives = gs.lives;
    miner_probe.keys = gs.keys_remaining;
    miner_probe.x = gs.player.b.x;
    miner_probe.y = gs.player.b.y;
}

/* set by a harness log line (debug builds only): writing one can take most
   of a frame, so the loop gets to the next joystick read only after the
   frame boundary, runs two logic ticks on that read, and a test bot that
   sets the keys at every tick falls a tick behind. After a log line the
   loop waits for the next frame and runs one tick: the logic loses a tick
   to each log line instead of catching up. */
static uint8_t log_slow;

static void log_state(void)
{
    static uint8_t last_mode = 0xFF, last_keys = 0xFF, last_level = 0xFF;
    static const char *const modes[5] = { "title", "playing", "win", "lose", "levelwin" };
    if (gs.mode != last_mode || gs.keys_remaining != last_keys || gs.level_index != last_level) {
        a8_log_s("miner: state=");
        a8_log_s(modes[gs.mode]);
        logu(" level=", gs.level_index + 1);
        logu(" lives=", gs.lives);
        logu(" keys=", gs.keys_remaining);
        a8_log_s(" score=");
        logscore(gs.score);
        LOGNL();
#ifndef A8_NO_LOG
        log_slow = 1;
#endif
        last_mode = gs.mode;
        last_keys = gs.keys_remaining;
        last_level = gs.level_index;
    }
}

/* ------------------------------------------------------------ sound */

/* the tune (the Spectrum port's: Am - F - G - E arpeggios; the Amiga's
   sound was a stub) */
static const a8_note tune[] = {
    {69, 8}, {72, 8}, {76, 8}, {81, 8}, {79, 8}, {76, 8}, {72, 8}, {76, 8},
    {65, 8}, {69, 8}, {72, 8}, {77, 8}, {76, 8}, {72, 8}, {69, 8}, {72, 8},
    {67, 8}, {71, 8}, {74, 8}, {79, 8}, {77, 8}, {74, 8}, {71, 8}, {74, 8},
    {64, 8}, {68, 8}, {71, 8}, {76, 8}, {74, 8}, {71, 8}, {68, 8}, {71, 8}, {0, 0} };

enum { S_JUMP, S_KEY, S_DIE, S_LEVEL, S_OVER, S_WIN };

static void sound(uint8_t s)
{
    switch (s) {
    case S_JUMP:  snd_sfx(0, 120, SND_PURE, 8, 1, -8); break;
    case S_KEY:   snd_sfx(0, 40, SND_PURE, 12, 1, -2); break;
    case S_DIE:   snd_sfx(1, 60, SND_NOISE, 15, 1, 4); break;
    case S_LEVEL: snd_sfx(0, 100, SND_PURE, 15, 0, -2); break;
    case S_OVER:  snd_sfx(0, 80, SND_PURE, 15, 0, 3); break;
    case S_WIN:   snd_sfx(0, 120, SND_PURE, 15, 0, -3); break;
    }
}

static void tune_start(uint8_t vol)
{
    if (music_on) music_play(2, tune, vol);
}

/* ------------------------------------------------------------ the cavern */

static const uint8_t wall_colour[4] = { A8_COLOR(3, 6), A8_COLOR(8, 6), A8_COLOR(12, 6), A8_COLOR(5, 6) };

/* a tile's two characters */
static void tile_draw(uint8_t c, uint8_t r)
{
    static uint8_t k, a, code, *d;
    static uint16_t cell;
    k = TMAP(c, r);
    d = room_row[r] + c * 2;
    switch (k) {
    case T_SOLID:    a = A_SOLID; break;
    case T_KEY:      a = A_KEY; break;
    case T_EXIT:     a = gs.keys_remaining ? A_EXIT_C : A_EXIT_O; break;
    case T_CONV_L:   a = A_CONV_L; break;
    case T_CONV_R:   a = A_CONV_R; break;
    case T_HAZARD:   a = A_HAZARD; break;
    case T_COLLAPSE:
        cell = (uint16_t)r * GRID_W + c;
        a = game_collapse_timer(cell);
        a = a ? (uint8_t)((COLLAPSE_FRAMES - a) * 8 / COLLAPSE_FRAMES) : 0;
        a = A_COLLAPSE0 + (a > 7 ? 7 : a);
        break;
    default:
        d[0] = d[1] = 0;
        return;
    }
    code = CH_TILES + a * 2;
    if (a == A_KEY || a == A_EXIT_O) code |= 0x80;      /* COLPF3 */
    d[0] = code;
    d[1] = code + 1;
}

static uint8_t exit_c[4], exit_r[4], exits;    /* the exit's tiles (redrawn when it opens) */

static void room_build(void)
{
    static uint8_t c, r;
    exits = 0;
    for (r = 0; r < ROWS; ++r)
        for (c = 0; c < GRID_W; ++c) {
            tile_draw(c, r);
            if (TMAP(c, r) == T_EXIT && exits < 4) { exit_c[exits] = c; exit_r[exits++] = r; }
        }
    COLOR0 = wall_colour[level_palette[gs.level_index] & 3];
}

static void room_changes(void)
{
    static uint8_t i;
    static uint16_t cell;
    for (i = 0; i < gs.num_changed; ++i) {
        cell = gs.changed[i];
        if (cell < ROWS * GRID_W) tile_draw((uint8_t)(cell % GRID_W), (uint8_t)(cell / GRID_W));
    }
    if (gs.events & EV_UNLOCK)                          /* the exit opens */
        for (i = 0; i < exits; ++i) tile_draw(exit_c[i], exit_r[i]);
}

/* belts turn, keys and the open exit cycle colours */
static void animate(uint8_t f)
{
    if (!(f & 1)) {
        memcpy(CHARSET + (CH_TILES + A_CONV_L * 2) * 8, belt_frames + ((f >> 1) & 7) * 16, 16);
        memcpy(CHARSET + (CH_TILES + A_CONV_R * 2) * 8, belt_frames + (8 + ((f >> 1) & 7)) * 16, 16);
    }
    COLOR3 = (uint8_t)(((f >> 2) << 4) | 0x0C);
}

/* ------------------------------------------------------------ sprites */

static uint8_t sp_y[4], sp_on[4];
static const uint8_t *sp_gfx[4];
static const uint8_t guard_colour[3] = { A8_COLOR(10, 10), A8_COLOR(5, 10), A8_COLOR(1, 14) };

static void sprite(uint8_t n, uint8_t on, int16_t x, int16_t y, const uint8_t *g)
{
    static uint8_t sy;
    if (!on || y < 0 || y >= ROWS * 16) {
        if (sp_on[n]) memset(PL(n) + sp_y[n], 0, 16);
        sp_on[n] = 0;
        return;
    }
    sy = (uint8_t)(SCAN_TOP + y);
    pmg_x(n, (uint8_t)(PMG_X0 + (x >> 1)));
    if (sp_on[n] && sy == sp_y[n] && g == sp_gfx[n]) return;
    if (sp_on[n]) memset(PL(n) + sp_y[n], 0, 16);
    memcpy(PL(n) + sy, g, 16);
    sp_on[n] = 1;
    sp_y[n] = sy;
    sp_gfx[n] = g;
}

static void sprites_from_game(void)
{
    static uint8_t i;
    static const Guardian *G;
    const Player *p = &gs.player;
    sprite(0, 1, p->b.x, p->b.y, (p->facing ? miner_gfx : frames_l) + p->frame * 16);
    G = gs.guards;
    for (i = 0; i < 3; ++i, ++G) {
        static uint8_t on, k;
        on = i < gs.num_guards && G->alive;
        k = G->colour >= 4 && G->colour <= 6 ? G->colour - 4 : 0;     /* the original's 4, 5, 6 */
        if (on) pmg_color(1 + i, guard_colour[k]);
        sprite(1 + i, on, G->x, G->y, guard_gfx + (k * 2 + ((G->frame >> 3) & 1)) * 16);
    }
}

static void sprites_off(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) sprite(i, 0, 0, 0, 0);
}

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

/* digits by subtracting powers of ten (cc65's 32-bit division is slow) */
static const uint32_t p10[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };

static void put_num(uint8_t *d, uint32_t v, uint8_t digits, uint8_t colour)
{
    static uint8_t i, c;
    for (i = 6 - digits; i < 6; ++i) {
        c = 0x10;
        while (v >= p10[i]) { v -= p10[i]; ++c; }
        *d++ = c | colour;
    }
}

static uint16_t name_timer;

/* the status line: the cavern's name for 2 s, then score, air, lives
   (redrawn only when something on it changed) */
static void hud(void)
{
    static uint32_t s_score = 0xFFFFFFFFUL;
    static uint8_t s_air = 0xFF, s_lives = 0xFF, s_name, n, i, c;
    if (name_timer) {
        if (--name_timer == 99 || !s_name) {
            memset(HUD, 0, 20);
            n = (uint8_t)strlen(lv_names[gs.level_index]);
            put(HUD + ((20 - n) >> 1), lv_names[gs.level_index], 0xC0);
            s_name = 1;
        }
        return;
    }
    if (s_name) {
        memset(HUD, 0, 20);
        put(HUD + 7, "AIR", 0x80);
        s_score = 0xFFFFFFFFUL;
        s_air = s_lives = 0xFF;
        s_name = 0;
    }
    if (gs.score != s_score) {
        put_num(HUD, gs.score, 6, 0x80);
        s_score = gs.score;
    }
    n = (uint8_t)((gs.air + 142) / 143);              /* 0..7 blocks */
    if (n != s_air) {
        c = gs.air < AIR_MAX / 4 ? 0xC0 : 0x40;
        for (i = 0; i < 7; ++i) HUD[10 + i] = i < n ? (BAR_CH | c) : 0;
        s_air = n;
    }
    if (gs.lives != s_lives) {
        HUD[18] = scode('M') | 0xC0;
        HUD[19] = (0x10 + gs.lives) | 0xC0;
        s_lives = gs.lives;
    }
}

/* ------------------------------------------------------------ screens */

static void show(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x1C;
}

static void screens_init(void)
{
    uint8_t *p, i, k, b;
    memcpy(CHARSET, (void *)0xE000, 512);
    memset(CHARSET + BAR_CH * 8, 0x7E, 8);
    memcpy(CHARSET + CH_TILES * 8, tile_chars, sizeof(tile_chars));
    for (i = 0; i < ROWS; ++i) room_row[i] = ROOM + i * 40;
    for (i = 0; i < sizeof(frames_l); ++i) {            /* the miner, mirrored */
        b = miner_gfx[i];
        k = 0;
        if (b & 0x80) k |= 0x01;
        if (b & 0x40) k |= 0x02;
        if (b & 0x20) k |= 0x04;
        if (b & 0x10) k |= 0x08;
        if (b & 0x08) k |= 0x10;
        if (b & 0x04) k |= 0x20;
        if (b & 0x02) k |= 0x40;
        if (b & 0x01) k |= 0x80;
        frames_l[i] = k;
    }
    p = DL_GAME;
    *p++ = 0x70;                                        /* 8-15 */
    *p++ = 0x45; *p++ = (uint8_t)(uint16_t)ROOM; *p++ = (uint16_t)ROOM >> 8;
    for (i = 1; i < ROWS; ++i) *p++ = 0x05;             /* 16-239 */
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)HUD; *p++ = (uint16_t)HUD >> 8;   /* 240-247 */
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_GAME; *p = (uint16_t)DL_GAME >> 8;
    p = DL_TITLE;
    *p++ = 0x70; *p++ = 0x70;
    *p++ = 0x47; *p++ = (uint8_t)(uint16_t)TITLE; *p++ = (uint16_t)TITLE >> 8;
    *p++ = 0x70;
    for (i = 1; i < 22; ++i) *p++ = 0x06;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_TITLE; *p = (uint16_t)DL_TITLE >> 8;
    pmg_init();
    GPRIOR = 1;
    pmg_color(0, A8_COLOR(4, 8));
}

static void game_screen(void)
{
    sprites_off();
    COLOR4 = 0;
    COLOR1 = A8_COLOR(2, 10);
    COLOR2 = A8_COLOR(0, 10);
    COLOR3 = A8_COLOR(1, 14);
    CHBAS = 0xA0;
    room_build();
    name_timer = 101;
    hud();
    show(DL_GAME);
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 5000, "ORE" }, { 4000, "AIR" }, { 3000, "KEY" }, { 2000, "PIT" }, { 1000, "MNR" } };

static uint8_t hi_rank(uint32_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hiscores[i].score) return i;
    return 5;
}

static void title_print(uint8_t row, uint8_t col, const char *s, uint8_t colour)
{
    put(TITLE + row * 20 + col, s, colour << 6);
}

static void title_screen(void)
{
    static uint8_t i;
    static char buf[16];
    memset(TITLE, 0, 22 * 20);
    sprites_off();
    title_print(0, 4, "MINER METEOR", 1);
    title_print(2, 0, "COLLECT EVERY KEY,", 2);
    title_print(3, 0, "REACH THE EXIT", 2);
    title_print(4, 0, "BEFORE THE AIR GOES", 2);
    title_print(6, 4, "HIGH SCORES", 1);
    for (i = 0; i < 5; ++i) {
        strcpy(buf, "1 AAA 000000");
        buf[0] = '1' + i;
        memcpy(buf + 2, hiscores[i].name, 3);
        title_print(7 + i, 4, buf, 0);
        put_num(TITLE + (7 + i) * 20 + 10, hiscores[i].score, 6, 0x00);
    }
    title_print(13, 0, "JOYSTICK WALKS", 3);
    title_print(14, 0, "FIRE OR UP JUMPS", 3);
    title_print(15, 0, "OPTION MUSIC", 3);
    title_print(16, 0, "ESC GIVES UP", 3);
    title_print(18, 1, "PRESS FIRE", 1);
    COLOR4 = 0;
    COLOR0 = A8_COLOR(1, 14);
    COLOR1 = A8_COLOR(3, 8);
    COLOR2 = A8_COLOR(0, 10);
    COLOR3 = A8_COLOR(12, 10);
    CHBAS = 0xA0;
    show(DL_TITLE);
}

/* initials: typed, or UP / DOWN and FIRE */
static void enter_initials(uint8_t rank)
{
    static char name[4];
    static uint8_t pos, p, k, i;
    static char c;
    strcpy(name, "A  ");
    pos = 0;
    logu("miner: hiscore rank=", rank + 1);
    LOGNL();
    memset(HUD, 0, 20);
    for (;;) {
        put(HUD, "INITIALS", 0xC0);
        put(HUD + 10, name, 0x80);
        frame_wait();
        input_read();
        p = input_pressed();
        k = key_get();
        c = k != KEY_NONE ? key_ascii(k) : 0;
        if (c >= 'A' && c <= 'Z') { name[pos] = c; p |= IN_FIRE; }
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & IN_FIRE) {
            if (++pos == 3) break;
            name[pos] = name[pos - 1];
        }
    }
    for (i = 4; i > rank; --i) hiscores[i] = hiscores[i - 1];
    hiscores[rank].score = gs.score;
    memcpy(hiscores[rank].name, name, 3);
    hiscores[rank].name[3] = 0;
    a8_log_s("miner: name=");
    a8_log_s(hiscores[rank].name);
    LOGNL();
}

/* ------------------------------------------------------------ demo */

static uint8_t demo_on;
static uint16_t demo_at;
static uint8_t demo_left;

static uint8_t demo_input(void)
{
    if (gs.mode != GM_PLAYING) return 0;
    if (gs.level_ticks == 0) {                /* a cavern starts: find its route */
        uint8_t l = 0;
        demo_at = 0;
        while (l < gs.level_index) if (demo_route[demo_at++][0] == 0xFF) l++;
        demo_left = demo_route[demo_at][1];
    }
    while (demo_left == 0 && demo_route[demo_at][0] != 0xFF) demo_left = demo_route[++demo_at][1];
    if (demo_route[demo_at][0] == 0xFF) return 0;
    demo_left--;
    return demo_route[demo_at][0];
}

/* ------------------------------------------------------------ input */

static uint8_t start_pressed, quit_pressed;

static uint8_t read_input(void)
{
    static uint8_t in, p, k, f;
    in = input_read();
    p = input_pressed();
    k = key_get();
    quit_pressed = k == KEY_ESC;
    start_pressed = (p & (IN_FIRE | IN_START)) != 0;
    f = 0;
    if (in & IN_LEFT) f |= INPUT_LEFT;
    if (in & IN_RIGHT) f |= INPUT_RIGHT;
    if (in & (IN_FIRE | IN_UP)) f |= INPUT_JUMP;
    if (start_pressed) f |= INPUT_START;
    if (p & IN_OPTION) music_on ^= 1;
    return f;
}

/* title: the tune, the demo after 12 s */
static void title_loop(void)
{
    static uint16_t idle;
    idle = 0;
    title_screen();
    a8_log_s("miner: title\n");
    tune_start(8);
    for (;;) {
        frame_wait();
        read_input();
        title_print(15, 13, music_on ? "ON " : "OFF", 3);
        if (start_pressed) break;
        if (++idle == 600) {
            demo_on = 1;
            a8_log_s("miner: demo start\n");
            break;
        }
    }
    game_tick(INPUT_START);
    log_state();
    music_stop();
    game_screen();
    if (!demo_on) tune_start(4);
}

int main(void)
{
    static uint8_t ticks, t, f, prev_mode, death_frames, resync, rank;
    screens_init();
    sound_init();
    game_init();
    frame_init();
    logu("miner: ready pal=", frame_is_pal() ? 1 : 0);
    LOGNL();
    title_loop();
    resync = 1;

    for (;;) {
        prev_mode = gs.mode;
        ticks = frame_wait();                 /* fixed timestep: the logic runs `ticks` times */
        if (log_slow) { frame_wait(); ticks = 1; log_slow = 0; }   /* debug builds: see log_slow */
        if (ticks > 3) ticks = 3;
        if (resync) { ticks = 1; resync = 0; }
        miner_probe.ticks += ticks;
        miner_probe.frames++;
        if (ticks > 1) miner_probe.overruns += ticks - 1;

        f = read_input();
        if (demo_on) {
            if (start_pressed || quit_pressed) {
                demo_on = 0;
                a8_log_s("miner: demo stop\n");
                game_init();
                title_loop();
                resync = 1;
                continue;
            }
            f = 0;
        } else if (quit_pressed && gs.mode == GM_PLAYING) {   /* port: ESC = give up */
            game_init();
            music_stop();
            title_loop();
            resync = 1;
            continue;
        }

        if (death_frames) {                   /* port: the death flash pauses the cavern */
            COLOR4 = (death_frames & 4) ? 0x0E : 0;
            if (--death_frames == 0) {
                COLOR4 = 0;
                room_build();
                hud();
                resync = 1;
                update_probe();
            }
            continue;
        }

        for (t = 0; t < ticks; t++) {
            if (demo_on) f = demo_input();
            update_probe();                   /* what a test bot sees before each tick */
            game_tick(f);
            f &= ~INPUT_START;
            if (gs.events & EV_LOADED) {
                if (!(gs.events & EV_DIED)) { room_build(); name_timer = 101; resync = 1; update_probe(); }
            } else {
                room_changes();
            }
            if (gs.events & EV_JUMP) sound(S_JUMP);
            if (gs.events & EV_KEY) sound(S_KEY);
            if (gs.events & EV_DIED) {
                logu("miner: died lives=", gs.lives);
                logu(" level=", gs.level_index + 1);
                LOGNL();
                sound(S_DIE);
                death_frames = 24;
                sprites_off();
            }
            if (gs.events & EV_LEVEL) {
                logu("miner: cleared level=", gs.level_index);
                a8_log_s(" score=");
                logscore(gs.score);
                LOGNL();
                sound(gs.events & EV_WON ? S_WIN : S_LEVEL);
            }
            log_state();
            if (death_frames || gs.mode != prev_mode) break;
        }
        if (gs.mode == GM_PLAYING) {
            sprites_from_game();
            animate((uint8_t)frame_count);
            hud();
        } else if (gs.mode != prev_mode) {
            sprites_off();
            memset(HUD, 0, 20);
            if (gs.mode == GM_WIN) put(HUD + 1, "ALL CAVERNS CLEARED", 0xC0);
            if (gs.mode == GM_LOSE) put(HUD + 5, "GAME OVER", 0xC0);
            if (gs.mode == GM_LOSE || gs.mode == GM_WIN) {
                music_stop();
                sound(gs.mode == GM_WIN ? S_WIN : S_OVER);
                a8_log_s(gs.mode == GM_WIN ? "miner: won score=" : "miner: gameover score=");
                logscore(gs.score);
                logu(" overruns=", miner_probe.overruns);
                LOGNL();
            }
        }
        if (gs.mode == GM_TITLE && prev_mode != GM_TITLE) {   /* from the end screen */
            rank = demo_on ? 5 : hi_rank(gs.score);
            demo_on = 0;
            music_stop();
            if (rank < 5) enter_initials(rank);
            game_init();
            title_loop();
            resync = 1;
        }
        if (demo_on && (gs.mode == GM_WIN || gs.mode == GM_LOSE) && gs.state_timer == 0) {
            demo_on = 0;
            a8_log_s("miner: demo done\n");
            game_init();
            title_loop();
            resync = 1;
        }
        update_probe();
        if ((miner_probe.frames & 127) == 0 && gs.mode == GM_PLAYING) {
            logu("miner: perf frames=", miner_probe.frames);
            logu(" ticks=", miner_probe.ticks);
            logu(" overruns=", miner_probe.overruns);
            logu(" level=", gs.level_index + 1);
            LOGNL();
#ifndef A8_NO_LOG
            log_slow = 1;
#endif
        }
    }
}
