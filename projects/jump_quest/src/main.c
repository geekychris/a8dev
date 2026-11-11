/*
 * Jump Quest (Atari 8-bit). Port of amiga_mcp/examples/jump_quest by way of
 * the ZX Spectrum port: jq.c is the original's logic (player, enemies,
 * items, levels, the state machine); this is the Atari front end.
 *
 * The level's rows 1-12 (row 0 is clouds or a ceiling, row 13 the earth
 * under the grass) are 12 ANTIC 5 rows, two characters a tile: a tile is 8
 * colour clocks x 16 scan lines, the original's 16 x 16, and the view is the
 * original's 20 tiles across. Each row is a strip of the whole level in
 * memory: ANTIC scrolls them (LMS + HSCROL, set in the vertical blank), so
 * the view follows the hero smoothly at no cost. Items are characters in
 * the strips. The hero is player 0; the enemies share players 1-3 by bands
 * of 16 lines (display.s).
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "jq.h"
#include "tmap.h"
#include "art.h"
#include "tune.h"
#include "demo.h"

#define STRIPS    ((uint8_t *)0xA000)     /* row r (1..12): STRIPS + (r - 1) * 512 */
#define STRIP_PAD 8                       /* characters before the level's first */
#define CHARSET   ((uint8_t *)0xB800)
#define HUD       ((uint8_t *)0xBC00)     /* 2 x 20 */
#define DL_GAME   ((uint8_t *)0xBC40)
#define DL_TITLE  ((uint8_t *)0xBCA0)
#define TITLE     ((uint8_t *)0xBD00)     /* 22 x 20 */
#define ROWS      12
#define SCAN_TOP  32                      /* scan line of world y 16 (row 1) */
#define PL(n)     ((uint8_t *)(0x9C00 + ((n) << 8)))

extern void display_install(void);
extern uint8_t vb_lms, vb_hs, vb_hero, vb_ready, row_lo[12], row_hi[12];
extern uint8_t *dl_rows;
extern uint8_t pend_x1[12], pend_x2[12], pend_x3[12], pend_c1[12], pend_c2[12], pend_c3[12];

static uint8_t sel_char = CHAR_RJ, sel_players = 1, demo_on, resync, music_on = 1;
int16_t view_x;                           /* (foes.s reads it) */                    /* world pixel at the screen's left */
static uint8_t item_on[MAX_ITEMS];

/* ------------------------------------------------------------ log, probe */

#ifndef A8_NO_LOG
static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }
static void logi(const char *k, int16_t v)
{
    a8_log_s(k);
    if (v < 0) { a8_log_c('-'); v = -v; }
    a8_log_u((uint16_t)v);
}
static void logscore(int32_t v)
{
    static const uint32_t t[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    static char b[7];
    static uint8_t i, n, c;
    uint32_t u = v < 0 ? 0 : (uint32_t)v;
    for (i = n = 0; i < 6; ++i) {
        c = '0';
        while (u >= t[i]) { u -= t[i]; ++c; }
        if (c != '0' || n || i == 5) b[n++] = c;
    }
    b[n] = 0;
    a8_log_s(b);
}
#define LOGNL() a8_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logi(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

#define ACTIVE (game.active_player ? &game.player[1] : &game.player[0])

struct {
    uint16_t level_ticks;
    uint8_t state, level, lives, health;
    int16_t x, y, cam_x;
    uint16_t frames, ticks, overruns;     /* fps = 50 * frames / ticks */
    uint16_t scrolls;
} jq_probe;

static void update_probe(void)
{
    const Player *p = ACTIVE;
    jq_probe.level_ticks = game.level_ticks;
    jq_probe.state = (uint8_t)game.state;
    jq_probe.level = (uint8_t)game.current_level;
    jq_probe.lives = (uint8_t)p->lives;
    jq_probe.health = (uint8_t)p->health;
    jq_probe.x = p->x;
    jq_probe.y = p->y;
    jq_probe.cam_x = view_x;
}

static const char *const state_names[5] = { "title", "playing", "dying", "levelwin", "gameover" };

static void log_state(void)
{
#ifndef A8_NO_LOG
    static int8_t last_state = -1, last_level = -1, last_lives = -1, last_health = -1, last_player = -1;
    const Player *p = ACTIVE;
    if (game.state != last_state || game.current_level != last_level || p->lives != last_lives
        || p->health != last_health || game.active_player != last_player) {
        a8_log_s("jq: state=");
        a8_log_s(state_names[game.state]);
        logu(" level=", game.current_level + 1);
        logu(" player=", game.active_player + 1);
        logi(" lives=", p->lives);
        logi(" health=", p->health);
        a8_log_s(" score=");
        logscore(p->score);
        LOGNL();
        last_state = game.state;
        last_level = game.current_level;
        last_lives = p->lives;
        last_health = p->health;
        last_player = game.active_player;
    }
#endif
}

/* ------------------------------------------------------------ sound */

enum { S_JUMP, S_STOMP, S_COLLECT, S_POWERUP, S_HURT, S_DIE, S_LEVEL };

static void sound(uint8_t s)
{
    switch (s) {
    case S_JUMP:    snd_sfx(0, 100, SND_PURE, 9, 1, -6); break;
    case S_STOMP:   snd_sfx(1, 40, SND_NOISE, 13, 1, 3); break;
    case S_COLLECT: snd_sfx(0, 50, SND_PURE, 12, 1, -3); break;
    case S_POWERUP: snd_sfx(0, 120, SND_PURE, 14, 1, -8); break;
    case S_HURT:    snd_sfx(1, 80, SND_NOISE, 14, 1, 2); break;
    case S_DIE:     snd_sfx(0, 60, SND_PURE, 15, 0, 3); break;
    case S_LEVEL:   snd_sfx(0, 120, SND_PURE, 15, 0, -2); break;
    }
}

static void music_start(void)
{
    if (!music_on) return;
    music_play(2, tune_melody, 6);
    music_play(3, tune_bass, 5);
}

/* ------------------------------------------------------------ the level */

static uint8_t *strip(uint8_t row) { return STRIPS + ((uint16_t)(row - 1) << 9) + STRIP_PAD; }

static uint8_t code_of(uint8_t art)
{
    return (uint8_t)((CH_TILES + art * 2) | (tile_inverse[art] ? 0x80 : 0));
}

static void tile_draw(uint8_t tx, uint8_t ty)
{
    static uint8_t *d, c;
    if (ty < 1 || ty > ROWS) return;
    d = strip(ty) + tx * 2;
    c = TMAP(tx, ty);
    if (c == TILE_EMPTY) { d[0] = d[1] = 0; return; }
    c = code_of(c);
    d[0] = c;
    d[1] = c + 1;
}

static uint8_t item_art(int8_t type)
{
    if (type >= ENT_BOARD && type <= ENT_PLANT) return ART_ITEMS + (type - ENT_BOARD);
    if (type >= ENT_BRIEFCASE && type <= ENT_WINE) return ART_ITEMS + 5 + (type - ENT_BRIEFCASE);
    return 0;
}

static void level_build(void)
{
    static uint8_t r, x, i, w, ty, c;
    static uint8_t *d;
    static const Item *it;
    w = (uint8_t)tmap_w;
    for (r = 1; r <= ROWS; ++r) {
        d = strip(r) - STRIP_PAD;
        memset(d, 0, 512);
        for (x = 0; x < w; ++x) tile_draw(x, r);
    }
    it = game.items;
    for (i = 0; i < MAX_ITEMS; ++i, ++it) {          /* items sit on empty tiles */
        item_on[i] = it->active;
        if (!it->active) continue;
        ty = (uint8_t)(it->y >> 4);
        if (ty < 1 || ty > ROWS) continue;
        d = strip(ty) + ((it->x >> 4) << 1);
        c = code_of(item_art(it->type));
        d[0] = c;
        d[1] = c + 1;
    }
}

static void items_taken(void)
{
    static uint8_t i;
    static const Item *it;
    it = game.items;
    for (i = 0; i < MAX_ITEMS; ++i, ++it)
        if (item_on[i] && !it->active) {
            item_on[i] = 0;
            tile_draw((uint8_t)(it->x >> 4), (uint8_t)(it->y >> 4));
        }
}

/* ------------------------------------------------------------ the view */

/* the view follows the hero (port: smoothly, a pixel at a time; the
   original's cam_x snaps to whole tiles) */
static void view_follow(void)
{
    static int16_t target, maxv;
    const Player *p = ACTIVE;
    maxv = (int16_t)tmap_w * TILE_SIZE - 320;
    target = p->x + 7 - 160;
    if (target < 0) target = 0;
    if (target > maxv) target = maxv;
    if (view_x < target - 4) view_x += (target - view_x + 3) >> 2;
    else if (view_x > target + 4) view_x -= (view_x - target + 3) >> 2;
    if (view_x < 0) view_x = 0;
    if (view_x > maxv) view_x = maxv;
}

/* the scroll for the vertical blank: buffer pixel STRIP_PAD * 4 + world
   colour clock at screen x = world colour clock - view: 4 lms - h = 16 + view */
static void view_scroll(void)
{
    static uint16_t t;
    t = 16 + (uint16_t)(view_x >> 1);
    vb_lms = (uint8_t)((t + 3) >> 2) - STRIP_PAD + STRIP_PAD;
    vb_hs = (uint8_t)(((t + 3) & ~3) - t);
}

/* ------------------------------------------------------------ sprites */

static uint8_t hero_y, hero_on;
static const uint8_t *hero_g;
extern void foes_draw(void), foes_clear(void);
/* hero_gfx + hero_off[(character * 2 + frame) * 3 + pose] */
static const uint16_t hero_off[12] = { 0, 28, 56, 84, 112, 140, 168, 196, 224, 252, 280, 308 };
static const uint8_t hero_base[4] = { 0, 3, 6, 9 };

static void sprites(uint8_t show_hero, uint8_t show_foes)
{
    static const Player *p;
    static uint8_t y;
    static int16_t sx, sy;
    static const uint8_t *g;
    /* the hero */
    p = ACTIVE;
    sx = (p->x - view_x) >> 1;
    sy = p->y - 16;
    if (show_hero && sy > -28 && sy < ROWS * 16 && !(p->invuln && (game.level_ticks & 4))) {
        y = (uint8_t)(SCAN_TOP + sy);
        g = hero_gfx + hero_off[hero_base[(p->character << 1) | (p->walk_frame & 1)] +
                                (p->on_ground ? (p->vx ? 1 : 0) : 2)];
        vb_hero = (uint8_t)(PMG_X0 + sx);
        if (!hero_on || y != hero_y || g != hero_g) {
            if (hero_on) memset(PL(0) + hero_y, 0, 28);
            memcpy(PL(0) + y, g, 28);
            hero_on = 1;
            hero_y = y;
            hero_g = g;
        }
    } else if (hero_on) {
        memset(PL(0) + hero_y, 0, 28);
        hero_on = 0;
    }
    /* the enemies: players 1-3 by bands (rows) of 16 lines (foes.s) */
    if (show_foes) foes_draw();
    else foes_clear();
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

static void hud(uint8_t force)
{
    static int32_t s_score = -1;
    static int8_t s_lives = -1, s_health = -1, s_level = -1;
    const Player *p = ACTIVE;
    if (force) {
        memset(HUD, 0, 40);
        s_score = -1;
        s_lives = s_health = s_level = -1;
        put(HUD + 20 + ((20 - strlen(levels[game.current_level < NUM_LEVELS ? game.current_level : 0].name)) >> 1),
            levels[game.current_level < NUM_LEVELS ? game.current_level : 0].name, 0x40);
    }
    if (p->score != s_score) {
        put_num(HUD, p->score < 0 ? 0 : (uint32_t)p->score, 6, 0x80);
        s_score = p->score;
    }
    if (p->health != s_health) {
        uint8_t i;
        for (i = 0; i < 5; ++i) HUD[8 + i] = i < p->health ? (scode('*') | 0xC0) : 0;
        s_health = p->health;
    }
    if (p->lives != s_lives) {
        put(HUD + 15, game.num_players == 2 ? (game.active_player ? "P2" : "P1") : "  ", 0x40);
        HUD[18] = scode('X') | 0x40;
        HUD[19] = (0x10 + (p->lives > 0 ? p->lives : 0)) | 0x40;
        s_lives = p->lives;
    }
}

/* ------------------------------------------------------------ screens */

static void show(uint8_t *dl)
{
    SDMCTL = 0;
    frame_wait();
    SDLSTL = (uint16_t)dl;
    frame_wait();
    SDMCTL = 0x22 | 0x18;                     /* normal width, players (no missiles), single line */
}

static void screens_init(void)
{
    static uint8_t *p, i, r;
    memcpy(CHARSET, (void *)0xE000, 512);
    memcpy(CHARSET + CH_TILES * 8, tile_chars, sizeof(tile_chars));
    p = DL_GAME;
    *p++ = 0x70; *p++ = 0x70;                                   /* 8-23 */
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)HUD; *p++ = (uint16_t)HUD >> 8;   /* 24-31 */
    dl_rows = p + 1;
    for (r = 1; r <= ROWS; ++r) {                               /* 32-223 */
        *p++ = r < ROWS ? 0xD5 : 0x55;                          /* DLI LMS HSCROL ANTIC 5 */
        *p++ = (uint8_t)(uint16_t)strip(r);
        *p++ = (uint16_t)strip(r) >> 8;
        row_lo[r - 1] = (uint8_t)(uint16_t)(strip(r) - STRIP_PAD);
        row_hi[r - 1] = (uint16_t)(strip(r) - STRIP_PAD) >> 8;
    }
    *p++ = 0x46; *p++ = (uint8_t)(uint16_t)(HUD + 20); *p++ = (uint16_t)(HUD + 20) >> 8;   /* 224-231 */
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_GAME; *p = (uint16_t)DL_GAME >> 8;
    p = DL_TITLE;
    *p++ = 0x70; *p++ = 0x70;
    *p++ = 0x47; *p++ = (uint8_t)(uint16_t)TITLE; *p++ = (uint16_t)TITLE >> 8;
    *p++ = 0x70;
    for (i = 1; i < 22; ++i) *p++ = 0x06;
    *p++ = 0x41; *p++ = (uint8_t)(uint16_t)DL_TITLE; *p = (uint16_t)DL_TITLE >> 8;
    pmg_init();
    GPRIOR = 1;
    display_install();
}

static void game_screen(void)
{
    static const Player *p;
    p = ACTIVE;
    vb_ready = 0;
    memset((void *)0x9B00, 0, 0x500);
    hero_on = 0;
    tmap_init(game.tiles, levels[game.current_level].width, TILES_Y);
    level_build();
    view_x = 0;
    view_follow();
    view_x = p->x + 7 - 160 < 0 ? 0 : view_x;
    view_scroll();
    COLOR4 = A8_COLOR(9, 10);                 /* the sky */
    COLOR0 = A8_COLOR(2, 4);                  /* earth, brick */
    COLOR1 = A8_COLOR(12, 6);                 /* grass, bushes, pipes */
    COLOR2 = 0x0C;                            /* stone, clouds */
    COLOR3 = A8_COLOR(1, 14);                 /* ? blocks, items */
    CHBAS = 0xB8;
    pmg_color(0, p->character == CHAR_RJ ? A8_COLOR(4, 8) : A8_COLOR(8, 8));
    hud(1);
    sprites(1, 1);
    vb_ready = 1;
    show(DL_GAME);
    resync = 1;
}

/* ------------------------------------------------------------ high scores */

typedef struct { uint32_t score; char name[4]; } hi_entry;
static hi_entry hiscores[5] = {
    { 500, "RJJ" }, { 400, "DAL" }, { 300, "BUG" }, { 200, "FLY" }, { 100, "ANT" } };

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

static void title_select(void)
{
    title_print(14, 0, sel_char == CHAR_RJ ? "   >RJ<    DALE    " : "    RJ    >DALE<   ", 3);
    title_print(15, 0, sel_players == 1 ? "    >1 PLAYER<     " : "    >2 PLAYERS<    ", 2);
}

static void title_screen(void)
{
    static uint8_t i;
    static char buf[16];
    vb_ready = 0;
    memset(TITLE, 0, 22 * 20);
    memset((void *)0x9B00, 0, 0x500);
    hero_on = 0;
    title_print(0, 5, "JUMP QUEST", 1);
    title_print(2, 0, "STOMP THE BUGS, GET", 2);
    title_print(3, 0, "THE GOODIES, REACH", 2);
    title_print(4, 0, "THE FLAG: 3 LEVELS", 2);
    title_print(6, 4, "HIGH SCORES", 1);
    for (i = 0; i < 5; ++i) {
        strcpy(buf, "1 AAA 000000");
        buf[0] = '1' + i;
        memcpy(buf + 2, hiscores[i].name, 3);
        title_print(7 + i, 4, buf, 0);
        put_num(TITLE + (7 + i) * 20 + 10, hiscores[i].score, 6, 0x00);
    }
    title_print(13, 0, "LEFT/RIGHT WHO", 0);
    title_select();
    title_print(17, 0, "JOYSTICK RUN, FIRE", 0);
    title_print(18, 0, "JUMPS, ESC GIVES UP", 0);
    title_print(20, 1, "PRESS FIRE OR START", 1);
    COLOR4 = 0;
    COLOR0 = A8_COLOR(1, 12);
    COLOR1 = A8_COLOR(12, 10);
    COLOR2 = A8_COLOR(9, 10);
    COLOR3 = A8_COLOR(4, 10);
    CHBAS = 0xE0;
    show(DL_TITLE);
}

static void enter_initials(uint8_t rank, uint32_t score)
{
    static char name[4];
    static uint8_t pos, p, k, i;
    static char c;
    strcpy(name, "A  ");
    pos = 0;
    logu("jq: hiscore rank=", rank + 1);
    LOGNL();
    for (;;) {
        memset(HUD + 20, 0, 20);
        put(HUD + 20, "INITIALS", 0xC0);
        put(HUD + 30, name, 0x80);
        frame_wait();
        input_read();
        p = input_pressed();
        k = key_get();
        c = k != KEY_NONE ? key_ascii(k) : 0;
        if (c >= 'A' && c <= 'Z') { name[pos] = c; p = IN_FIRE; }   /* (W A S D also move) */
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & IN_FIRE) {
            if (++pos == 3) break;
            name[pos] = name[pos - 1];
        }
    }
    for (i = 4; i > rank; --i) hiscores[i] = hiscores[i - 1];
    hiscores[rank].score = score;
    memcpy(hiscores[rank].name, name, 3);
    hiscores[rank].name[3] = 0;
    a8_log_s("jq: name=");
    a8_log_s(hiscores[rank].name);
    LOGNL();
}

/* ------------------------------------------------------------ demo, input */

static uint16_t demo_at;
static uint8_t demo_left;

static uint8_t demo_input(void)
{
    if (game.state != STATE_PLAYING) return 0;
    if (game.level_ticks == 0) {
        uint8_t l = 0;
        demo_at = 0;
        while (l < game.current_level) if (demo_route[demo_at++][0] == 0xFF) l++;
        demo_left = demo_route[demo_at][1];
    }
    while (demo_left == 0 && demo_route[demo_at][0] != 0xFF) demo_left = demo_route[++demo_at][1];
    if (demo_route[demo_at][0] == 0xFF) return 0;
    demo_left--;
    return demo_route[demo_at][0];
}

static uint8_t start_pressed, quit_pressed;

static uint8_t read_input(void)
{
    static uint8_t in, p, k, f;
    in = input_read();
    p = input_pressed();
    k = key_get();
    quit_pressed = k == KEY_ESC;
    start_pressed = (p & (IN_FIRE | IN_START)) != 0;
    if (p & IN_OPTION) { music_on ^= 1; if (!music_on) music_stop(); }
    f = 0;
    if (in & IN_LEFT) f |= INP_LEFT;
    if (in & IN_RIGHT) f |= INP_RIGHT;
    if (in & IN_UP) f |= INP_UP;
    if (in & IN_DOWN) f |= INP_DOWN;
    if (in & IN_FIRE) f |= INP_JUMP;
    return f;
}

static void title_loop(void)
{
    static uint16_t idle;
    static uint8_t pr;
    idle = 0;
    title_screen();
    a8_log_s("jq: title\n");
    music_start();
    for (;;) {
        frame_wait();
        read_input();
        pr = input_pressed();
        if (pr & (IN_LEFT | IN_RIGHT)) { sel_char = (pr & IN_LEFT) ? CHAR_RJ : CHAR_DALE; title_select(); idle = 0; }
        if (pr & (IN_UP | IN_DOWN)) { sel_players = sel_players == 1 ? 2 : 1; title_select(); idle = 0; }
        if (start_pressed) break;
        if (++idle == 600) {
            demo_on = 1;
            a8_log_s("jq: demo start\n");
            break;
        }
    }
    music_stop();
    if (demo_on) jq_new_game(CHAR_RJ, 1);
    else jq_new_game(sel_char, sel_players);
    logu("jq: start character=", game.player[0].character);
    logu(" players=", game.num_players);
    logu(" demo=", demo_on);
    LOGNL();
    log_state();
    music_start();
    game_screen();
    update_probe();
}

static void gameover_screen(void)
{
    static int32_t sv;
    sv = game.player[0].score;
    if (game.num_players == 2) sv += game.player[1].score;
    music_stop();
    vb_ready = 0;
    memset((void *)0x9B00, 0, 0x500);
    hero_on = 0;
    memset(HUD + 20, 0, 20);
    put(HUD + 20 + (game.current_level >= NUM_LEVELS ? 1 : 5),
        game.current_level >= NUM_LEVELS ? "CONGRATULATIONS!" : "GAME OVER", 0xC0);
    a8_log_s("jq: gameover score=");
    logscore(sv);
    logu(" won=", game.current_level >= NUM_LEVELS);
    logu(" overruns=", jq_probe.overruns);
    LOGNL();
}

static void game_over_to_title(void)
{
    static uint8_t i, rank;
    music_stop();
    if (!demo_on)
        for (i = 0; i < (uint8_t)game.num_players; i++) {
            rank = hi_rank((uint32_t)game.player[i].score);
            if (rank < 5) enter_initials(rank, (uint32_t)game.player[i].score);
        }
    demo_on = 0;
    title_loop();
}

int main(void)
{
    static uint8_t ticks, t, f, prev_state, full_redraw, tiles_q[8], tq;
    static Player *p;

    memset(&game, 0, sizeof(game));
    screens_init();
    sound_init();
    rnd_seed(1);
    frame_init();
    logi("jq: ready jump=", JUMP_FORCE);
    LOGNL();
    title_loop();

    for (;;) {
        ticks = frame_wait();                  /* fixed timestep: the logic runs `ticks` times */
        if (ticks > 4) ticks = 4;
        if (resync) { ticks = 1; resync = 0; }
        jq_probe.ticks += ticks;
        jq_probe.frames++;
        if (ticks > 1) jq_probe.overruns += ticks - 1;

        f = read_input();
        if (demo_on && (start_pressed || quit_pressed)) {
            a8_log_s("jq: demo stop\n");
            demo_on = 0;
            music_stop();
            title_loop();
            continue;
        }
        if (!demo_on && quit_pressed && game.state == STATE_PLAYING) {   /* port: ESC gives up */
            music_stop();
            title_loop();
            continue;
        }

        prev_state = game.state;
        full_redraw = 0;
        for (t = 0; t < ticks; t++) {
            if (demo_on) f = demo_input();
            update_probe();                    /* what a test bot sees before each tick */
            jq_tick(f);
            if (game.events & (EV_COLLECT | EV_POWERUP)) items_taken();
            if (game.events & EV_TILE) tile_draw(game.tile_tx, game.tile_ty);
            if (game.events & EV_JUMP) sound(S_JUMP);
            if (game.events & EV_STOMP) sound(S_STOMP);
            if (game.events & EV_COLLECT) sound(S_COLLECT);
            if (game.events & EV_POWERUP) sound(S_POWERUP);
            if (game.events & EV_HURT) sound(S_HURT);
            if (game.events & EV_DIE) {
                p = ACTIVE;
                logi("jq: died lives=", p->lives - 1);
                logu(" level=", game.current_level + 1);
                logi(" x=", p->x);
                LOGNL();
                sound(S_DIE);
            }
            if (game.events & EV_LEVELWIN) {
                logu("jq: cleared level=", game.current_level + 1);
                a8_log_s(" score=");
                logscore(ACTIVE->score);
                logu(" ticks=", game.level_ticks);
                LOGNL();
                sound(S_LEVEL);
            }
            if (game.events & EV_LEVEL) full_redraw = 1;
            log_state();
            if (game.state != prev_state || full_redraw) break;
        }
        (void)tiles_q; (void)tq;

        if (full_redraw && game.state != STATE_GAMEOVER) {
            game_screen();
            continue;
        }
        if (game.state != prev_state) {
            if (game.state == STATE_LEVELWIN) {
                memset(HUD + 20, 0, 20);
                put(HUD + 22, "LEVEL COMPLETE!", 0xC0);
            }
            if (game.state == STATE_GAMEOVER) gameover_screen();
            if (game.state == STATE_TITLE) { game_over_to_title(); continue; }
        }
        if (demo_on && game.state == STATE_GAMEOVER && game.transition <= 0) {
            a8_log_s("jq: demo done\n");
            demo_on = 0;
            title_loop();
            continue;
        }
        if (game.state == STATE_PLAYING || game.state == STATE_DYING || game.state == STATE_LEVELWIN) {
            static int16_t last_view;
            vb_ready = 0;
            last_view = view_x;
            view_follow();
            if (view_x != last_view) jq_probe.scrolls++;
            view_scroll();
            sprites(game.state != STATE_DYING, game.state != STATE_LEVELWIN);
            hud(0);
            vb_ready = 1;
        }
        update_probe();
        if ((jq_probe.frames & 63) == 0 && game.state == STATE_PLAYING) {
            logu("jq: perf frames=", jq_probe.frames);
            logu(" ticks=", jq_probe.ticks);
            logu(" overruns=", jq_probe.overruns);
            logu(" level=", game.current_level + 1);
            LOGNL();
        }
    }
}
