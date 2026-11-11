/*
 * game.c - Boing Twist's game shell (the original's main_os4.c, after the
 * Spectrum port's game.c): the title with the Boing ball bouncing round
 * its room, the select screen (level, picture, room), play, the solved
 * screen, the high scores.
 *
 * Moves go through a queue as in the original: a move animates (a twist
 * 240 ms, a flip 420 ms; faster while scrambling or giving up) and is
 * applied to the puzzle (bt_core.c, the original's, unchanged) when it
 * ends. Everything runs on 50 Hz ticks (fixed timestep): a slow picture
 * shows fewer steps of a move, not a slower move.
 *
 * port: controls. Joystick up/down choose the band, left/right twist it;
 * fire held + left/right choose the half (the original's A / D), a tap of
 * fire flips it. Keys: Z / X the half, K / L turn the ball, SELECT or U
 * undo, OPTION or H (held) peek, START or P pause, R give up, N a new
 * scramble, T the room, ESC back.
 * port: the original keeps your best moves and time per picture and level
 * in a file; here they last until the machine is switched off, and a solve
 * scores points for a five-place high-score table with initials.
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "bt.h"
#include "bt_core.h"
#include "ball.h"
#include "sound.h"
#include "game.h"

#pragma bss-name (push, "HIBSS")          /* under the OS ROM (bt.h) */
BtPuzzle P;
static uint8_t q_kind[128];
static int16_t q_move[128];
#pragma bss-name (pop)
uint8_t screen, paused, peeking, gave_up, busy, sel_band, sel_half;
uint8_t sel_level, sel_pic, sel_row, play_level, play_pic;
uint16_t secs, peeks, score;
int16_t anim_move = -1;
uint16_t draws, draw_frames;               /* ball pictures drawn while moves animate, and the frames they took */
static uint8_t anim_kind, anim_len, anim_t, auto_solve, timer_running, sec_ticks;
static uint16_t off16, target16;          /* the turn: texels x 256 */
static uint16_t scene_t, seed_ticks;
static uint8_t qh, qt;
static uint8_t ball_dirty, hud_dirty, fire_held, fire_used;
static uint16_t best_mv[NPICS][4], best_s[NPICS][4];
static uint8_t title_page;

enum { K_DO, K_UNDO, K_SOLVE, K_SCRAMBLE };
static const char *const screen_names[] = { "TITLE", "SELECT", "PLAY", "SOLVED", "ENTRY" };
static const char *const level_names[] = { "EASY", "MEDIUM", "HARD", "EXPERT" };

#define PLAY_ROW  12                       /* the play ball's buffer row 0 on screen */
#define BALL_X    (PMG_X0 + 80)            /* the ball's middle, colour clocks */

/* ------------------------------------------------------------ text */
static uint8_t *text_at(uint8_t row) { return TEXT_ROWS + (uint16_t)row * 40; }
static uint8_t *big_at(void) { return BIG_ROW; }

static uint8_t scode(char ch)
{
    uint8_t c = (uint8_t)ch;
    if (c < 0x20) return (uint8_t)(c + 0x40);
    if (c < 0x60) return (uint8_t)(c - 0x20);
    return c;
}
static void put(uint8_t *p, const char *s) { while (*s) *p++ = scode(*s++); }
static void text_clear(void) { memset(TEXT_ROWS, 0, 160 + 20); }
static void text(uint8_t x, uint8_t row, const char *s) { put(text_at(row) + x, s); }
static void big(const char *s)
{
    uint8_t *p = big_at(), n = (uint8_t)strlen(s), i;
    memset(p, 0, 20);
    p += (20 - n) / 2;
    for (i = 0; i < n; ++i) {
        uint8_t c = scode(s[i]);
        p[i] = (uint8_t)(c | (i & 1 ? 0x40 : 0x80));      /* mode 7: bits 6-7 pick the colour */
    }
}
static char *num(uint16_t v, uint8_t w)
{
    static char b[7];
    uint8_t i = 6;
    b[6] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i && (uint8_t)(6 - i) < w) b[--i] = ' ';
    return b + i;
}
static char *mmss(uint16_t s)
{
    static char b[6];
    uint8_t m = (uint8_t)(s / 60), ss = (uint8_t)(s % 60);
    if (m > 99) m = 99;
    b[0] = (char)('0' + m / 10);
    b[1] = (char)('0' + m % 10);
    b[2] = ':';
    b[3] = (char)('0' + ss / 10);
    b[4] = (char)('0' + ss % 10);
    b[5] = 0;
    return b;
}

/* ------------------------------------------------------------ high scores */
typedef struct { char name[4]; uint16_t score; } Hs;
static Hs hs[5] = { { "AMI", 3000 }, { "BOI", 2400 }, { "NGA", 1800 }, { "XEP", 1200 }, { "RPT", 600 } };
static uint8_t ed_place, ed_pos;
static char ed_name[4];

static int8_t hs_rank(uint16_t s)
{
    uint8_t i;
    for (i = 0; i < 5; ++i) if (s > hs[i].score) return (int8_t)i;
    return -1;
}

static void hs_insert(uint8_t at, const char *name, uint16_t s)
{
    uint8_t i;
    for (i = 4; i > at; --i) hs[i] = hs[i - 1];
    memcpy(hs[at].name, name, 4);
    hs[at].score = s;
}

/* ------------------------------------------------------------ queue */
static uint16_t ease(uint8_t t, uint8_t len)
{
    uint16_t x = (uint16_t)(((uint16_t)t << 8) / len);      /* 0..256 */
    return (uint16_t)((uint32_t)x * x * (768 - 2 * x) >> 16);
}

static uint8_t anim_ticks(uint8_t kind, int16_t m)
{
    uint8_t flip = BT_IS_FLIP(m) ? 1 : 0;
    switch (kind) {
    case K_SCRAMBLE: return flip ? 8 : 5;
    case K_SOLVE:    return flip ? 13 : 8;
    default:         return flip ? 21 : 12;
    }
}

static uint8_t q_len(void) { return (uint8_t)(qt - qh) & 127; }

static void enqueue(uint8_t kind, int16_t m)
{
    if (q_len() >= 127) return;
    if (kind == K_DO && !bt_valid(&P, m)) return;
    q_kind[qt] = kind;
    q_move[qt] = m;
    qt = (qt + 1) & 127;
}

static void face_half(uint8_t k) { target16 = (uint16_t)ball_half_turn(k) << 8; }

static void set_screen(uint8_t s);
static void layout(void);

static void enter_solved(void)
{
    uint16_t lm = (uint16_t)bt_levels[play_level].moves * 3, bonus;
    uint16_t *bm = &best_mv[play_pic][play_level], *bs = &best_s[play_pic][play_level];
    uint8_t best_new = 0;
    timer_running = 0;
    score = 0;
    if (!gave_up) {
        bonus = 500 + (P.moves < (int)lm ? (lm - (uint16_t)P.moves) * 25 : 0) + (secs < 300 ? (300 - secs) * 2 : 0);
        score = bonus * (play_level + 1);
        score = score > peeks * 100u ? score - peeks * 100u : 0;
        if (!*bm || (uint16_t)P.moves < *bm || secs < *bs) {
            if (!*bm || (uint16_t)P.moves < *bm) *bm = (uint16_t)P.moves;
            if (!*bs || secs < *bs) *bs = secs;
            best_new = 1;
        }
    }
    snd_play(SFX_SOLVED);
    a8_log("bt: solved moves=%d secs=%u peeks=%u score=%u gave_up=%u best=%u\n", P.moves, secs, peeks, score,
           gave_up, best_new);
    set_screen(SC_SOLVED);
}

static void anim_next(void)
{
    if (auto_solve && q_len() == 0 && anim_move < 0) {
        if (P.nhist > 0) enqueue(K_SOLVE, 0);
        else auto_solve = 0;
    }
    while (anim_move < 0 && q_len()) {
        uint8_t kind = q_kind[qh];
        int16_t m = q_move[qh];
        qh = (qh + 1) & 127;
        if (kind == K_UNDO) m = bt_last_move(&P) >= 0 ? bt_inverse(bt_last_move(&P)) : -1;
        else if (kind == K_SOLVE) m = P.nhist > 0 ? bt_inverse(P.hist[P.nhist - 1]) : -1;
        if (m < 0) continue;
        anim_move = m;
        anim_kind = kind;
        anim_t = 0;
        anim_len = anim_ticks(kind, m);
        if (kind == K_DO && !timer_running) timer_running = 1;
        if (kind != K_SCRAMBLE || BT_IS_FLIP(m)) snd_play(BT_IS_FLIP(m) ? SFX_FLIP : SFX_CLICK);
        if (peeking) { peeking = 0; ball_texture(&P, 0); }
        if (BT_IS_FLIP(m)) {
            off16 = target16 = (uint16_t)ball_half_turn((uint8_t)BT_HALF(m)) << 8;   /* port: facing the half */
            ball_turn = (uint8_t)(off16 >> 8);
            flip_build();
        } else
            ball_twist_begin((uint8_t)BT_BAND(m));
        ball_dirty = 1;
    }
}

static void anim_finish(void)
{
    int16_t m = anim_move;
    uint8_t kind = anim_kind;
    switch (kind) {
    case K_DO: bt_do(&P, m); break;
    case K_UNDO: bt_undo(&P); break;
    case K_SOLVE:
        P.nhist--;
        if (P.nscramble > P.nhist) P.nscramble = P.nhist;
        bt_apply(&P, m);
        break;
    case K_SCRAMBLE:
        bt_apply(&P, m);
        if (P.nhist < BT_HIST) P.hist[P.nhist++] = m;
        P.nscramble = P.nhist;
        break;
    }
    anim_move = -1;
    busy = q_len() && q_kind[qh] == K_SCRAMBLE;
    hud_dirty = 1;
    ball_dirty = 1;
    if (kind == K_DO || kind == K_UNDO) a8_log("bt: move m=%d moves=%d\n", m, P.moves);
    if (kind == K_SCRAMBLE && !busy) a8_log("bt: scrambled n=%d hash=%08lX\n", P.nscramble, bt_hash(&P));
    if (screen == SC_PLAY && !q_len() && kind != K_SCRAMBLE && bt_solved(&P)) {
        auto_solve = 0;
        if (kind == K_SOLVE) gave_up = 1;
        enter_solved();
        return;
    }
    if (BT_IS_FLIP(m)) ball_texture(&P, 0);
    else {
        uint8_t b = (uint8_t)BT_BAND(m);
        uint8_t v0 = (uint8_t)(((uint16_t)b * TEX_H * 2 / P.bands + 1) >> 1);
        uint8_t v1 = (uint8_t)(((uint16_t)(b + 1) * TEX_H * 2 / P.bands + 1) >> 1);
        if (TEX_W % P.segs) ball_texture_rows(&P, v0, v1, 0);
        else ball_twist_shift(BT_WEST(m) ? -(int8_t)ball_seg_texels() : (int8_t)ball_seg_texels());
    }
}

/* ------------------------------------------------------------ screens */
static void draw_hud(void);

static void start_game(void)
{
    uint8_t lv = sel_level, i;
    uint32_t seed = (uint32_t)seed_ticks * 2654435761u + 1;
    play_level = lv;
    play_pic = sel_pic;
    bt_init(&P, bt_levels[lv].bands, bt_levels[lv].segs);
    ball_picture(play_pic, &P, 1);
    seed = bt_start_level(&P, lv, seed, -1);
    a8_log("bt: play pic=%u level=%u grid=%dx%d scramble=%d seed=%lu\n", play_pic, lv, P.bands, P.segs, P.nscramble,
           seed);
    qh = qt = 0;
    for (i = 0; i < (uint8_t)P.nscramble; i++) enqueue(K_SCRAMBLE, P.hist[i]);
    bt_reset(&P);
    anim_move = -1;
    auto_solve = 0;
    busy = 1;
    timer_running = 0;
    secs = sec_ticks = 0;
    peeks = 0;
    gave_up = 0;
    paused = peeking = 0;
    sel_band = (uint8_t)(P.bands / 2 - 1);
    sel_half = 0;
    face_half(sel_half);
    off16 = target16;
    snd_music(0);
    draws = draw_frames = 0;
    set_screen(SC_PLAY);
}

uint8_t pic_allowed(uint8_t i, uint8_t level)
{
    if (i == 0) return level == BT_EASY;
    return pics[i].level <= level;
}

static uint8_t next_allowed(uint8_t from, int8_t d, uint8_t level)
{
    uint8_t i = from, n;
    for (n = 0; n < NPICS; ++n) {
        i = (uint8_t)((i + NPICS + d) % NPICS);
        if (pic_allowed(i, level)) return i;
    }
    return from;
}

/* the select screen's (and title's) preview: the picture on the level's grid, solved */
static void show_picture(uint8_t pic, uint8_t level)
{
    bt_init(&P, bt_levels[level].bands, bt_levels[level].segs);
    ball_picture(pic, &P, 0);
    ball_texture(&P, 1);
    ball_dirty = 1;
    a8_log("bt: preview pic=%u level=%u\n", pic, level);
}

static void choose_picture(uint8_t i)
{
    sel_pic = i;
    if (pics[i].room) ball_room = pics[i].room;
    show_picture(sel_pic, sel_level);
    hud_dirty = 1;
}

static void change_row(int8_t d)
{
    switch (sel_row) {
    case 0:
        sel_level = (uint8_t)((sel_level + d + BT_NLEVELS) % BT_NLEVELS);
        if (!pic_allowed(sel_pic, sel_level)) sel_pic = next_allowed(sel_pic, 1, sel_level);
        choose_picture(sel_pic);
        break;
    case 1: choose_picture(next_allowed(sel_pic, d, sel_level)); break;
    case 2: ball_room = (uint8_t)((ball_room + d + NROOMS) % NROOMS); break;
    }
    layout();                                  /* the picture's colours, the room */
    hud_dirty = 1;
    ball_dirty = 1;
}

static void set_screen(uint8_t s)
{
    screen = s;
    scene_t = 0;
    hud_dirty = ball_dirty = 1;
    ball_clear_buf();
    ball_markers(0, 0, 0, 0, 0);
    layout();
    a8_log("bt: screen=%s\n", screen_names[s]);
}

static void to_title(void)
{
    snd_music(1);
    title_page = 0;
    show_picture(0, BT_HARD);                  /* the Boing ball's own 8 x 16 */
    set_screen(SC_TITLE);
}

static void to_select(void)
{
    snd_music(1);
    if (!pic_allowed(sel_pic, sel_level)) sel_pic = next_allowed(sel_pic, 1, sel_level);
    if (pics[sel_pic].room) ball_room = pics[sel_pic].room;
    show_picture(sel_pic, sel_level);
    set_screen(SC_SELECT);
}

/* ------------------------------------------------------------ input */
#define K_Z 23
#define K_X 22
#define K_K 5
#define K_L 0
#define K_U 11
#define K_H 57
#define K_R 40
#define K_N 35
#define K_T 45
#define K_P 10
#define K_ESC 28

static uint8_t key_held(uint8_t code)
{
    return (SKSTAT & 0x04) == 0 && (KBCODE & 0x3F) == code;
}

static void play_input(uint8_t held, uint8_t edge, uint8_t key, uint8_t cons, uint8_t cedge)
{
    uint8_t S = (uint8_t)P.segs, B = (uint8_t)P.bands, old_band = sel_band, old_half = sel_half, pk;
    if ((edge & IN_START) || key == K_P) {
        paused = !paused;
        hud_dirty = 1;
        a8_log("bt: paused=%u\n", paused);
    }
    if (paused) return;
    pk = (cons & IN_OPTION) || key_held(K_H);
    if (pk && !peeking && anim_move < 0 && !busy) {
        peeking = 1;
        peeks++;
        hud_dirty = 1;
        ball_texture(&P, 1);
        ball_dirty = 1;
        a8_log("bt: peek n=%u\n", peeks);
    }
    if (!pk && peeking) {
        peeking = 0;
        hud_dirty = 1;
        ball_texture(&P, 0);
        ball_dirty = 1;
    }
    if (edge & IN_FIRE) { fire_held = 1; fire_used = 0; }
    if (fire_held && (held & IN_FIRE)) {
        if (edge & IN_LEFT) { sel_half = (uint8_t)((sel_half + S - 1) % S); fire_used = 1; }
        if (edge & IN_RIGHT) { sel_half = (uint8_t)((sel_half + 1) % S); fire_used = 1; }
    } else {
        if (fire_held && !fire_used && !busy) enqueue(K_DO, BT_FLIP(sel_half));
        fire_held = 0;
        if ((edge & IN_UP) && sel_band > 0) sel_band--;
        if ((edge & IN_DOWN) && sel_band < B - 1) sel_band++;
        if ((edge & IN_LEFT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 1));    /* west */
        if ((edge & IN_RIGHT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 0));   /* east */
    }
    if (key == K_Z) sel_half = (uint8_t)((sel_half + S - 1) % S);
    if (key == K_X) sel_half = (uint8_t)((sel_half + 1) % S);
    if (sel_half != old_half) face_half(sel_half);
    if (key == K_K) target16 -= 8u << 8;
    if (key == K_L) target16 += 8u << 8;
    if (((cedge & IN_SELECT) || key == K_U) && !busy) enqueue(K_UNDO, 0);
    if (key == K_R && !busy) { qh = qt; auto_solve = 1; a8_log("bt: give up\n"); }
    if (key == K_N) { start_game(); return; }
    if (key == K_T) { ball_room = (uint8_t)((ball_room + 1) % NROOMS); layout(); }
    if (sel_band != old_band || sel_half != old_half) {
        hud_dirty = 1;
        ball_dirty = 1;
        a8_log("bt: sel band=%u half=%u\n", sel_band, sel_half);
    }
}

static void entry_input(uint8_t edge, uint8_t key)
{
    char c = key != KEY_NONE ? key_ascii(key) : 0;
    if (c >= 'A' && c <= 'Z') { ed_name[ed_pos] = c; if (ed_pos < 2) ed_pos++; hud_dirty = 1; }
    if (edge & IN_UP) { ed_name[ed_pos] = ed_name[ed_pos] == 'Z' ? 'A' : (char)(ed_name[ed_pos] + 1); hud_dirty = 1; }
    if (edge & IN_DOWN) { ed_name[ed_pos] = ed_name[ed_pos] == 'A' ? 'Z' : (char)(ed_name[ed_pos] - 1); hud_dirty = 1; }
    if ((edge & IN_RIGHT) && ed_pos < 2) { ed_pos++; hud_dirty = 1; }
    if ((edge & IN_LEFT) && ed_pos > 0) { ed_pos--; hud_dirty = 1; }
    if (edge & (IN_FIRE | IN_START) || c == '\n') {
        hs_insert(ed_place, ed_name, score);
        a8_log("bt: hiscore name=%s score=%u place=%u\n", ed_name, score, ed_place);
        to_title();
    }
}

static void input(void)
{
    /* the joystick and console keys from the VBI (presses latched, so a busy
       moment loses none, and each counts once); the keyboard's from its IRQ */
    uint8_t edge = take_latch(), held = in_held, key = key_get(), cons, cedge;
    switch (key) {
    case KEY_A: case 6:  edge |= IN_LEFT; key = KEY_NONE; break;     /* A, + (left arrow) */
    case KEY_D: case 7:  edge |= IN_RIGHT; key = KEY_NONE; break;    /* D, * */
    case KEY_W: case 14: edge |= IN_UP; key = KEY_NONE; break;       /* W, - */
    case KEY_S: case 15: edge |= IN_DOWN; key = KEY_NONE; break;     /* S, = */
    case KEY_SPACE:      edge |= IN_FIRE; key = KEY_NONE; break;
    case KEY_RETURN:     edge |= IN_START; key = KEY_NONE; break;
    }
    if (key_held(KEY_SPACE)) held |= IN_FIRE;
    cons = (uint8_t)(held & (IN_START | IN_SELECT | IN_OPTION));
    cedge = (uint8_t)(edge & (IN_START | IN_SELECT | IN_OPTION));
    if (screen == SC_ENTRY) { entry_input(edge, key); return; }
    switch (screen) {
    case SC_TITLE:
        if (edge & (IN_FIRE | IN_START)) to_select();
        else if (key == K_T) { ball_room = (uint8_t)((ball_room + 1) % NROOMS); layout(); }
        break;
    case SC_SELECT:
        if (key == K_ESC) to_title();
        else if (edge & IN_UP) { sel_row = (uint8_t)((sel_row + 2) % 3); hud_dirty = 1; }
        else if (edge & IN_DOWN) { sel_row = (uint8_t)((sel_row + 1) % 3); hud_dirty = 1; }
        else if (edge & IN_LEFT) change_row(-1);
        else if (edge & IN_RIGHT) change_row(1);
        else if (edge & (IN_FIRE | IN_START)) start_game();
        break;
    case SC_PLAY:
        if (key == K_ESC) { qh = qt; anim_move = -1; to_title(); }
        else play_input(held, edge, key, cons, cedge);
        break;
    case SC_SOLVED:
        if (scene_t < 50) break;
        if (key == K_ESC) to_title();
        else if (edge & (IN_FIRE | IN_START)) {
            int8_t r = gave_up ? -1 : hs_rank(score);
            if (r >= 0 && score > 0) {
                ed_place = (uint8_t)r;
                ed_pos = 0;
                memcpy(ed_name, "AAA", 4);
                set_screen(SC_ENTRY);
            } else to_select();
        }
        break;
    }
}

/* ------------------------------------------------------------ ticks */
#define BOUNCE_T 56                        /* ticks a bounce */

static void tick(void)
{
    int16_t d;
    seed_ticks++;
    scene_t++;
    if (screen == SC_ENTRY) return;
    if (screen == SC_TITLE || screen == SC_SOLVED) {
        off16 -= screen == SC_TITLE ? 256 : 328;   /* 70 / 90 degrees a second */
        if (scene_t % BOUNCE_T == 0) snd_play(SFX_THUD);
        if (screen == SC_TITLE && scene_t % 300 == 0) { title_page = (uint8_t)((title_page + 1) % 3); hud_dirty = 1; }
        ball_dirty = 1;
        return;
    }
    if (screen == SC_SELECT) {
        off16 += 91;
        ball_dirty = 1;
        return;
    }
    if (paused) return;
    anim_next();
    if (anim_move >= 0) {
        ball_dirty = 1;
        if (++anim_t >= anim_len) { anim_finish(); anim_next(); }
    }
    if (screen != SC_PLAY) return;
    if (timer_running && ++sec_ticks >= 50) {
        sec_ticks = 0;
        secs++;
        hud_dirty = 1;
    }
    d = (int16_t)(target16 - off16);
    if (d) {
        int16_t st = d / 6;
        if (st == 0) st = d;
        off16 += st;
        ball_dirty = 1;
    }
}

/* ------------------------------------------------------------ drawing */
static int8_t bounce_row, bounce_dx;       /* the title's ball: its buffer row 0 on screen, byte shift */

static void layout(void)
{
    switch (screen) {
    case SC_TITLE:
    case SC_SOLVED:
        ball_screen(1, V_TITLE, bounce_row, bounce_dx, 2, 0x90);
        break;
    case SC_SELECT:
        ball_screen(1, V_TITLE, 10, 8, 4, 0x90);
        break;
    case SC_ENTRY:
        ball_screen(1, V_TITLE, 10, 0, 4, 0x90);
        break;
    default:
        ball_screen(0, V_PLAY, PLAY_ROW, 0, 2, 0x90);
        break;
    }
}

static void place_shadow(int8_t row, int8_t dx, uint8_t radius)
{
    uint8_t x = (uint8_t)(BALL_X - dx * 4);          /* the list's LMS + dx moves the picture left */
    uint8_t y = (uint8_t)(PMG_Y0 + 2 * (row + 32));      /* the ball's middle, scan lines */
    if (rooms[ball_room].wall)            /* on the back wall, down and to the right */
        ball_shadow((uint8_t)(x + radius / 2), (uint8_t)(y - radius * 2 + 12), (uint8_t)(radius * 2 - 4), (uint8_t)(radius * 4 - 8));
    else {                                 /* on the floor, under the ball */
        uint8_t fy = (uint8_t)(PMG_Y0 + 2 * 82);
        ball_shadow(x, (uint8_t)(fy - 6), (uint8_t)(radius * 2), 10);    /* port: the same at any height (as the Spectrum's) */
    }
}

static void draw_ball(void)
{
    uint8_t view = V_PLAY;
    ball_turn = (uint8_t)((off16 + 128) >> 8) & 63;
    if (screen == SC_PLAY) {
        if (anim_move >= 0) {
            uint16_t e = ease(anim_t, anim_len);
            if (BT_IS_FLIP(anim_move)) {
                view = V_FLIP;
                ball_turn = (uint8_t)((e * 32u + 128) >> 8);
            } else {
                uint8_t w = (uint8_t)(TEX_W * 16 / P.segs), dd = (uint8_t)((e * w + 2048) >> 12);
                ball_twist_shift(BT_WEST(anim_move) ? -(int8_t)dd : (int8_t)dd);
            }
        }
        ball_draw(views[view].bank);
        return;
    }
    if (screen == SC_TITLE || screen == SC_SOLVED) {
        /* the bounce: up and down on |sin|, side to side on sin */
        uint8_t ph = (uint8_t)((uint32_t)(scene_t % BOUNCE_T) * 128 / BOUNCE_T);
        int16_t up = fix_sin(ph);                         /* 0..256 */
        uint8_t sx = (uint8_t)((uint32_t)(scene_t % 400) * 256 / 400);
        bounce_row = (int8_t)(24 - (int16_t)((int32_t)up * 22 >> 8));
        bounce_dx = (int8_t)((int32_t)fix_sin(sx) * 7 >> 8);
        ball_screen(1, V_TITLE, bounce_row, bounce_dx, 2, 0x90);
        place_shadow(bounce_row, bounce_dx, views[V_TITLE].radius);
    }
    ball_draw(views[V_TITLE].bank);
}

static void draw_hud(void)
{
    text_clear();
    switch (screen) {
    case SC_TITLE:
        big("BOING TWIST");
        if (title_page == 0) {
            text(1, 0, "TWIST THE BANDS, FLIP THE HALVES");
            text(1, 1, "FIRE OR START: CHOOSE A PUZZLE");
        } else if (title_page == 1) {
            uint8_t i;
            text(1, 0, "HIGH SCORES");
            for (i = 0; i < 5; ++i) {
                text((uint8_t)(1 + i * 8), 1, hs[i].name);
            }
            for (i = 0; i < 3; ++i) text((uint8_t)(14 + i * 8), 0, num(hs[i].score, 5));
        } else {
            text(1, 0, "STICK: BAND + TWIST  FIRE: FLIP");
            text(1, 1, "SELECT UNDO  OPTION PEEK  START PAUSE");
        }
        break;
    case SC_SELECT:
        big("CHOOSE");
        text(0, 0, sel_row == 0 ? ">" : " ");
        text(1, 0, "LEVEL   ");
        text(9, 0, level_names[sel_level]);
        text(0, 1, sel_row == 1 ? ">" : " ");
        text(1, 1, "PICTURE ");
        text(9, 1, pics[sel_pic].title);
        text(0, 2, sel_row == 2 ? ">" : " ");
        text(1, 2, "ROOM    ");
        text(9, 2, rooms[ball_room].name);
        text(1, 3, "STICK CHOOSES, FIRE PLAYS, ESC BACK");
        break;
    case SC_PLAY:
        text(1, 0, pics[play_pic].title);
        text(22, 0, "MOVES");
        text(27, 0, num((uint16_t)P.moves, 4));
        text(33, 0, mmss(secs));
        if (busy) text(1, 1, "SCRAMBLING...");
        else if (paused) text(1, 1, "PAUSED: START GOES ON");
        else if (peeking) text(1, 1, "PEEKING AT THE PICTURE");
        else {
            text(1, 1, "BAND");
            text(5, 1, num((uint16_t)sel_band + 1, 2));
            text(9, 1, "HALF");
            text(13, 1, num((uint16_t)sel_half + 1, 3));
            text(19, 1, level_names[play_level]);
            if (peeks) { text(28, 1, "PEEKS"); text(33, 1, num(peeks, 2)); }
        }
        break;
    case SC_SOLVED:
        big(gave_up ? "GIVEN UP" : "SOLVED!");
        text(1, 0, "MOVES");
        text(6, 0, num((uint16_t)P.moves, 4));
        text(12, 0, "TIME");
        text(17, 0, mmss(secs));
        text(24, 0, "SCORE");
        text(29, 0, num(score, 6));
        text(1, 1, "FIRE: GO ON   ESC: TITLE");
        break;
    case SC_ENTRY:
        big("HIGH SCORE");
        text(1, 0, "YOUR INITIALS:");
        text(16, 0, ed_name);
        text((uint8_t)(16 + ed_pos), 1, "^");
        text(1, 2, "STICK OR KEYS, FIRE WHEN DONE");
        text(1, 3, "SCORE");
        text(7, 3, num(score, 6));
        break;
    }
}

static void draw_markers(void)
{
    uint8_t g = P.bands == 4 ? 0 : P.bands == 6 ? 1 : 2, top, bot;
    if (screen != SC_PLAY || busy || peeking || anim_move >= 0) { ball_markers(0, 0, 0, 0, 0); return; }
    top = (uint8_t)(PMG_Y0 + 2 * (PLAY_ROW + band_top[g][sel_band]));
    bot = (uint8_t)(PMG_Y0 + 2 * (PLAY_ROW + band_bot[g][sel_band]) + 2);
    ball_markers(1, top, bot, (uint8_t)(BALL_X - 33 - 8), (uint8_t)(BALL_X + 33));
}

void game_init(void)
{
    pmg_color(0, 0x1C);                    /* the band markers: yellow */
    pmg_color(1, 0x1C);
    pmg_size(2, 3);                        /* the shadow: four times wide */
    pmg_size(3, 3);
    GPRIOR = 4;                            /* playfield over players: the ball hides the shadow */
    ball_room = 0;
    to_title();
    place_shadow(24, 0, views[V_TITLE].radius);
}

static uint8_t last_shadow_screen = 0xFF;

void game_frame(uint8_t frames)
{
    uint8_t i, t0;
    input();
    for (i = 0; i < frames && i < 8; ++i) tick();
    if (screen != last_shadow_screen) {
        last_shadow_screen = screen;
        if (screen == SC_PLAY) place_shadow(PLAY_ROW, 0, views[V_PLAY].radius);
        else if (screen == SC_SELECT || screen == SC_ENTRY) ball_shadow_off();
    }
    if (ball_dirty) {
        ball_dirty = 0;
        t0 = RTCLOK2;
        draw_ball();
        if (screen == SC_PLAY && anim_move >= 0) {
            draws++;
            draw_frames += (uint8_t)(RTCLOK2 - t0) + 1;
        }
    }
    if (hud_dirty) {
        hud_dirty = 0;
        draw_hud();
        draw_markers();
    }
}
