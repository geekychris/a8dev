/*
 * st_core.c - Stack Tower's rules (see st.h), from the Amiga original's
 * st_core.c line for line; port: notes marked "port:".
 */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "st.h"

StGame game;
#define G game                  /* port: one game, addressed directly (cc65 code is half the size) */

/* Cells as [dx, dy] about the pivot, y up: two trominoes and six
 * pentominoes, none of the classic four-square pieces. */
const int8_t st_cells[ST_NPIECES][5][2] = {
    { { -1, 0 }, { 0, 0 }, { 1, 0 } },                        /* rod */
    { { 0, 0 }, { 1, 0 }, { 0, 1 } },                         /* elbow */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } },   /* cup */
    { { 0, -1 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },    /* flag */
    { { 0, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } },   /* star */
    { { -1, 1 }, { 0, 1 }, { 1, 1 }, { 0, 0 }, { 0, -1 } },   /* anchor */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 0, -1 }, { 1, -1 } }, /* stairs */
    { { -1, 1 }, { 0, 1 }, { 0, 0 }, { 0, -1 }, { 1, -1 } },  /* bolt */
};
const uint8_t st_ncells[ST_NPIECES] = { 3, 3, 5, 5, 5, 5, 5, 5 };
const char *const st_mode_names[MODE_COUNT] = { "marathon", "rising", "dig" };

/* tried in order after a rotation that collides: none, sideways, up, further */
static const int8_t kicks[10][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 2, 0 }, { -2, 0 },
                                     { 1, 1 }, { -1, 1 }, { 0, 2 }, { 0, -1 } };
static const int16_t line_score[7] = { 0, 100, 300, 600, 1000, 1500, 2100 };
/* gravity: 0.9 s a row at level 1, x0.82 a level, never under 1/30 s */
static const int16_t interval_tab[30] = { 900, 738, 605, 496, 407, 334, 274, 224, 184, 151, 124, 101,
    83, 68, 56, 46, 38, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33 };

#define CMOD(a) ((uint8_t)(a) & (ST_COLS - 1))     /* port: st_mod(a, 16) for two's complement */

int16_t st_interval_ms(int16_t level) { return interval_tab[level < 1 ? 0 : level > 30 ? 29 : level - 1]; }
static int16_t rise_ms(int16_t level)
{
    int16_t v = 14000 - level * 1200;
    return v < 4000 ? 4000 : v;
}

uint32_t st_hash(const char *s)
{
    uint32_t h = 2166136261UL;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619UL; }
    return h;
}

/* mulberry32, as the browser's rng() */
uint32_t st_rand32(uint32_t *a)
{
    uint32_t t;
    *a += 0x6D2B79F5UL;
    t = *a;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

/* floor(rand * n / 2^32); port: (hi*n + (lo*n >> 16)) >> 16, exact for n < 65536 */
uint8_t st_rand_below(uint32_t *a, uint8_t n)
{
    uint32_t r = st_rand32(a);
    uint32_t hi = (r >> 16) * n, lo = (r & 0xFFFFUL) * n;
    return (uint8_t)((hi + (lo >> 16)) >> 16);
}

static void push_ev(uint8_t type, int16_t a, int16_t b)
{
    StEvent *e;
    if (G.nev < ST_MAX_EVENTS) {
        e = &G.ev[G.nev++];
        e->type = type; e->a = a; e->b = b;
    }
}

uint8_t st_pop_event(StEvent *e)
{
    if (!G.nev) return 0;
    *e = G.ev[0];
    memmove(&G.ev[0], &G.ev[1], (G.nev - 1) * sizeof(StEvent));
    G.nev--;
    return 1;
}

/* a piece's cells after rot quarter turns clockwise: (x, y) -> (y, -x).
   port: the rotations are worked out once (st_init_cells), here only added */
int8_t st_rot_cells[ST_NPIECES][4][10];
/* st_fast.s addresses the board and the piece at fixed offsets */
typedef char st_check_board[offsetof(StGame, board) == 9 ? 1 : -1];
typedef char st_check_piece[offsetof(StGame, piece) == 420 ? 1 : -1];

void st_init_cells(void)
{
    uint8_t t, r, i, k;
    int8_t x, y, v;
    for (t = 0; t < ST_NPIECES; t++)
        for (r = 0; r < 4; r++)
            for (i = 0; i < st_ncells[t]; i++) {
                x = st_cells[t][i][0]; y = st_cells[t][i][1];
                for (k = 0; k < r; k++) { v = x; x = y; y = -v; }
                st_rot_cells[t][r][2 * i] = x;
                st_rot_cells[t][r][2 * i + 1] = y;
            }
}

uint8_t st_cells_of(const Piece *p, int8_t out[5][2])
{
    uint8_t n = st_cellsp(p), i;
    for (i = 0; i < n; ++i) {
        out[i][0] = st_cx[i];
        out[i][1] = st_cy[i];
    }
    return n;
}

static void bag(void)
{
    uint8_t b[ST_NPIECES], i, j, t;
    for (i = 0; i < ST_NPIECES; i++) b[i] = i;
    for (i = ST_NPIECES - 1; i > 0; i--) {
        j = st_rand_below(&G.rng, i + 1);
        t = b[i]; b[i] = b[j]; b[j] = t;
    }
    for (i = 0; i < ST_NPIECES && G.nqueue < ST_QUEUE; i++) G.queue[G.nqueue++] = b[i];
}

static void refill(void) { while (G.nqueue < 8) bag(); }

uint8_t st_preview(uint8_t n, uint8_t *out)
{
    uint8_t i;
    refill();
    for (i = 0; i < n && i < G.nqueue; i++) out[i] = G.queue[i];
    return i;
}

static void garbage_row(uint8_t gaps, uint8_t *row)
{
    uint8_t freec[ST_COLS], nf = 0, i, c, seen;
    memset(row, ST_GARBAGE, ST_COLS);
    while (nf < gaps) {
        c = st_rand_below(&G.rng, ST_COLS);
        for (seen = 0, i = 0; i < nf; i++) if (freec[i] == c) seen = 1;
        if (!seen) freec[nf++] = c;
    }
    for (i = 0; i < nf; i++) row[freec[i]] = 0;
}

static void spawn(int8_t type, int8_t col)
{
    uint8_t fresh = type < 0;
    refill();
    if (fresh) {
        type = G.queue[0];
        memmove(&G.queue[0], &G.queue[1], G.nqueue - 1);
        G.nqueue--;
    }
    G.piece.type = type;
    G.piece.rot = 0;
    G.piece.col = CMOD(col);
    G.piece.row = ST_ROWS + 1;
    G.fall_ms = G.lock_ms = G.resets = 0;
    if (fresh) G.can_hold = 1;
    if (st_collides(&G.piece)) {
        G.piece.row++;                       /* try one higher before giving up */
        if (st_collides(&G.piece)) { G.over = 1; push_ev(STE_OVER, 0, 0); }
    }
}

void st_new_game(uint8_t mode, uint32_t seed, int16_t level, int8_t col)
{
    char key[40];
    uint8_t r;
    memset(&G, 0, sizeof G);
    G.mode = mode;
    G.seed = seed;
    ultoa(seed, key, 10);                     /* port: sprintf("%lu:%s") */
    strcat(key, ":");
    strcat(key, st_mode_names[mode]);
    G.rng = st_hash(key);
    if (!G.rng) G.rng = 0x9e3779b9UL;
    G.hold = -1;
    G.can_hold = 1;
    G.level = G.start_level = level < 1 ? 1 : level;
    G.combo = -1;
    G.last_col = col;
    if (mode == MODE_DIG) {
        for (r = 0; r < ST_DIG_ROWS; r++) garbage_row(r % 3 == 0 ? 2 : 1, &G.board[r * ST_COLS]);
        G.garbage_left = ST_DIG_ROWS;
    }
    refill();
    spawn(-1, col);
}

static uint8_t touching(void)
{
    Piece p;
    p = G.piece;
    p.row--;
    return st_collides(&p);
}

static void moved(void)
{
    /* a successful move or turn on the stack restarts the lock timer, a limited number of times */
    if (touching() && G.resets < ST_LOCK_RESETS) { G.lock_ms = 0; G.resets++; }
}

uint8_t st_move(int8_t dir)
{
    Piece p;
    if (G.over || G.won) return 0;
    p = G.piece;
    p.col = CMOD(p.col + dir);
    if (st_collides(&p)) return 0;
    G.piece = p;
    G.last_col = p.col;
    moved();
    push_ev(STE_MOVE, dir, 0);
    return 1;
}

uint8_t st_rotate(int8_t dir)
{
    uint8_t k;
    Piece p;
    if (G.over || G.won) return 0;
    for (k = 0; k < 10; k++) {
        p = G.piece;
        p.rot = (p.rot + dir) & 3;
        p.col = CMOD(p.col + kicks[k][0] * dir);
        p.row += kicks[k][1];
        if (!st_collides(&p)) {
            G.piece = p;
            G.last_col = p.col;
            moved();
            push_ev(k ? STE_KICK : STE_ROTATE, dir, 0);
            return 1;
        }
    }
    return 0;
}

/* st_ghost_row: st_fast.s */

static uint8_t full_rows(uint8_t *rows)
{
    uint8_t r, c, n = 0;
    const uint8_t *b = G.board;
    for (r = 0; r < ST_TOTAL; r++, b += ST_COLS) {
        for (c = 0; c < ST_COLS && b[c]; c++)
            ;
        if (c == ST_COLS) rows[n++] = r;
    }
    return n;
}

static int16_t count_garbage_rows(void)
{
    uint8_t r, c, n = 0;
    for (r = 0; r < ST_TOTAL; r++)
        for (c = 0; c < ST_COLS; c++)
            if (G.board[r * ST_COLS + c] == ST_GARBAGE) { n++; break; }
    return n;
}

static void clear_rows(const uint8_t *rows, uint8_t n)
{
    /* port: compacted in place (rows are in increasing order) */
    uint8_t r, k = 0, i = 0;
    for (r = 0; r < ST_TOTAL; r++) {
        if (i < n && rows[i] == r) { ++i; continue; }
        if (k != r) memcpy(&G.board[k * ST_COLS], &G.board[r * ST_COLS], ST_COLS);
        ++k;
    }
    memset(&G.board[k * ST_COLS], 0, (ST_TOTAL - k) * ST_COLS);
}

static void lock_piece(void)
{
    int8_t cells[5][2];
    uint8_t n = st_cells_of(&G.piece, cells), i, all_hidden = 1, rows[ST_TOTAL], nr;
    int16_t lv;
    int32_t pts;
    for (i = 0; i < n; i++) {
        G.board[(uint8_t)cells[i][1] * ST_COLS + cells[i][0]] = (uint8_t)(G.piece.type + 1);
        if (cells[i][1] < ST_ROWS) all_hidden = 0;
    }
    G.pieces++;
    push_ev(STE_LOCK, G.piece.type, 0);
    if (all_hidden) { G.over = 1; push_ev(STE_OVER, 1, 0); return; }
    nr = full_rows(rows);
    if (nr) {
        clear_rows(rows, nr);
        G.combo++;
        G.lines += nr;
        pts = (int32_t)line_score[nr < 7 ? nr : 6] * G.level + (G.combo > 0 ? 50L * G.combo * G.level : 0);
        G.score += pts;
        push_ev(STE_CLEAR, nr, (int16_t)pts);
        if (G.mode == MODE_DIG) {
            G.garbage_left = count_garbage_rows();
            if (G.garbage_left == 0) { G.won = 1; push_ev(STE_WIN, 0, 0); return; }
        }
        lv = G.start_level + G.lines / 10;
        if (G.mode != MODE_DIG && lv > G.level) { G.level = lv; push_ev(STE_LEVEL, lv, 0); }
    } else {
        G.combo = -1;
    }
    spawn(-1, G.last_col);
}

uint8_t st_hard_drop(void)
{
    int8_t to, dist;
    if (G.over || G.won) return 0;
    to = st_ghost_row();
    dist = G.piece.row - to;
    G.piece.row = to;
    G.score += 2 * dist;
    push_ev(STE_DROP, dist, 0);
    lock_piece();
    return (uint8_t)dist;
}

void st_soft_drop(uint8_t on) { G.soft = on != 0; }

uint8_t st_hold(void)
{
    int8_t cur, t;
    if (G.over || G.won || !G.can_hold) return 0;
    cur = G.piece.type;
    if (G.hold < 0) { G.hold = cur; spawn(-1, G.last_col); }
    else { t = G.hold; G.hold = cur; spawn(t, G.last_col); }
    G.can_hold = 0;
    push_ev(STE_HOLD, cur, 0);
    return 1;
}

void st_add_garbage(uint8_t gaps)
{
    uint8_t row[ST_COLS], c, spilled = 0;
    garbage_row(gaps, row);
    for (c = 0; c < ST_COLS; c++) if (G.board[(ST_TOTAL - 1) * ST_COLS + c]) spilled = 1;
    memmove(&G.board[ST_COLS], &G.board[0], ST_COLS * (ST_TOTAL - 1));
    memcpy(&G.board[0], row, ST_COLS);
    if (spilled) { G.over = 1; push_ev(STE_OVER, 2, 0); return; }
    if (st_collides(&G.piece)) {
        G.piece.row++;
        if (st_collides(&G.piece)) { G.over = 1; push_ev(STE_OVER, 2, 0); return; }
    }
    push_ev(STE_GARBAGE, 0, 0);
}

void st_tick(int16_t dt)
{
    int16_t step, iv, every, s;
    if (G.over || G.won) return;
    G.time_ms += dt;
    if (G.mode == MODE_RISING) {
        every = rise_ms(G.level);
        G.rise_ms += dt;
        if (G.rise_ms >= every) {
            G.rise_ms -= every;
            st_add_garbage(G.level >= 6 ? 1 : 2);
            if (G.over) return;
        }
    }
    iv = st_interval_ms(G.level);
    step = iv;
    if (G.soft) { s = iv / 20 > 17 ? iv / 20 : 17; step = s < iv ? s : iv; }
    if (touching()) {
        G.fall_ms = 0;
        G.lock_ms += dt;
        if (G.lock_ms >= ST_LOCK_MS || (G.resets >= ST_LOCK_RESETS && G.lock_ms >= ST_LOCK_MS / 3))
            lock_piece();
        return;
    }
    G.fall_ms += dt;
    while (G.fall_ms >= step && !touching()) {
        G.fall_ms -= step;
        G.piece.row--;
        if (G.soft) G.score += 1;
    }
    if (touching()) G.fall_ms = 0;
}
