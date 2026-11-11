/*
 * bb_core.c - Block Breaker 3D's rules, from the Amiga original's
 * bb_core.c: the ball, the bricks, the paddle, the capsules, the levels
 * and the autopilot.
 *
 * The ball moves a quarter of a tick at a time and one axis at a time: if a
 * move along an axis takes it into a brick, the move is undone and that
 * axis' speed turned round, which gives clean bounces off faces.
 *
 * port: 1/32 units in 16 bits (the original: 1/256 in 32), so `2 * lim - p`
 * is written `lim - (p - lim)`; cells by subtraction, not division; the
 * autopilot's hashes are cached until what they hash changes.
 */
#include <string.h>
#include <cc65.h>
#include "bb.h"

#pragma bss-name ("LOWRAM")

BBGame G;
uint8_t bb_dirty;

const char *const bb_level_names[BB_LEVELS] = {
    "FIRST LIGHT", "CHEQUERS", "THE WINDOW", "PILLARS", "STAIRWAY", "THE VAULT", "CROSSFIRE", "THE CORE"
};
/* 3 layers (nearest first) of 4 rows (top first) of 6.
 * r o y g c b: bricks, T: tough, S: steel, *: a capsule inside */
static const char levels[BB_LEVELS][12][7] = {
    { "......", "......", "......", "......",
      ".o..o.", "..**..", "..yy..", ".g..g.",
      "rrrrrr", "oooooo", "yyyyyy", "gggggg" },
    { "r.r.r.", ".o.o.o", "y.y.y.", ".g.g.g",
      ".c.c.c", "b.b.b.", ".c*c.c", "b.b.b.",
      "TTTTTT", "cccccc", "bbbbbb", "TTTTTT" },
    { "SS..SS", "S....S", "S....S", "SS..SS",
      "......", ".*yy*.", ".yyyy.", "......",
      "rrrrrr", "rTTTTr", "rTTTTr", "rrrrrr" },
    { "g....g", "g....g", "g....g", "g....g",
      "..TT..", "..TT..", "..**..", "..TT..",
      "cbcbcb", "bcbcbc", "cbcbcb", "bcbcbc" },
    { "......", "......", "......", "oooooo",
      "......", "......", "yyyyyy", "TT**TT",
      "......", "gggggg", "cccccc", "bbbbbb" },
    { "S.SS.S", "......", "......", "S.SS.S",
      "TTTTTT", "T*..*T", "T....T", "TTTTTT",
      "yyyyyy", "yrrrry", "yrrrry", "yyyyyy" },
    { "..rr..", "......", "......", "..rr..",
      "o....o", ".T**T.", ".T..T.", "o....o",
      "SbbbbS", "bcTTcb", "bcTTcb", "SbbbbS" },
    { "TTTTTT", "T....T", "T....T", "TTTTTT",
      "S.SS.S", ".*..*.", ".*..*.", "S.SS.S",
      "rrrrrr", "ooTToo", "yyTTyy", "gggggg" },
};

#define R32   (BB_R * BB_FX)
/* v / 4 rounded towards 0, as C's division (port: shifts, not cc65's divide) */
#define QUARTER(v) ((v) < 0 ? -(int16_t)((uint16_t)-(v) >> 2) : (int16_t)((uint16_t)(v) >> 2))

static void event(uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t n = G.nev;
    if (n < BB_MAX_EVENTS) { G.ev[n] = type; G.eva[n] = a; G.evb[n] = b; G.nev = n + 1; }
}

uint8_t bb_pop_event(uint8_t *a, uint8_t *b)
{
    uint8_t e;
    if (!G.nev) return BBE_NONE;
    e = G.ev[0]; *a = G.eva[0]; *b = G.evb[0];
    --G.nev;
    memmove(G.ev, G.ev + 1, G.nev);
    memmove(G.eva, G.eva + 1, G.nev);
    memmove(G.evb, G.evb + 1, G.nev);
    return e;
}

/* mulberry32 */
static uint32_t rnd(void)
{
    uint32_t t = (G.rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

uint8_t bb_breakable(uint8_t type) { return type != BK_EMPTY && type != BK_STEEL; }

uint8_t bb_pad_w(void) { return G.wide_t ? BB_WIDE_W : BB_PAD_W; }
uint8_t bb_pad_h(void) { return G.wide_t ? BB_WIDE_H : BB_PAD_H; }

static int16_t cur_speed(void) { return G.slow_t ? G.speed * 3 / 4 : G.speed; }

static void set_speed(void)
{
    int16_t s = (12 + G.level) * BB_FX + (G.paddle_hits / 10) * (BB_FX / 2);
    if (s > 24 * BB_FX) s = 24 * BB_FX;
    G.speed = s;
}

static uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint16_t)r;
}

/* the ball's speed back to cur_speed, keeping its direction across the
   tunnel within limits so it always makes progress along it */
static void renorm(void)
{
    int16_t s = cur_speed(), lim = s * 3 / 5;
    int32_t rest;
    uint8_t neg = G.vz < 0;
    if (G.vx > lim) G.vx = lim;
    if (G.vx < -lim) G.vx = -lim;
    if (G.vy > lim) G.vy = lim;
    if (G.vy < -lim) G.vy = -lim;
    rest = (int32_t)s * s - (int32_t)G.vx * G.vx - (int32_t)G.vy * G.vy;
    G.vz = (int16_t)isqrt32((uint32_t)(rest > 0 ? rest : 1));
    if (neg) G.vz = -G.vz;
}

void bb_load_level(uint8_t level)
{
    uint8_t l, r, c, i, t;
    const char *row;
    G.level = level;
    G.left = 0;
    for (l = 0; l < BB_LAYERS; l++)
        for (r = 0; r < BB_ROWS; r++) {
            row = levels[level - 1][l * BB_ROWS + r];
            for (c = 0; c < BB_COLS; c++) {
                i = bb_index(c, BB_ROWS - 1 - r, l);
                switch (row[c]) {
                case 'r': t = BK_RED; break;
                case 'o': t = BK_ORANGE; break;
                case 'y': t = BK_YELLOW; break;
                case 'g': t = BK_GREEN; break;
                case 'c': t = BK_CYAN; break;
                case 'b': t = BK_BLUE; break;
                case 'T': t = BK_TOUGH; break;
                case 'S': t = BK_STEEL; break;
                case '*': t = BK_BONUS; break;
                default: t = BK_EMPTY; break;
                }
                G.cell[i] = t;
                G.hits[i] = t == BK_TOUGH ? (level >= 5 ? 3 : 2) : t == BK_EMPTY ? 0 : 1;
                if (bb_breakable(t)) G.left++;
            }
        }
    memset(G.cactive, 0, sizeof(G.cactive));
    G.wide_t = G.slow_t = G.plough_t = 0;
    G.paddle_hits = 0;
    G.stuck = 1;
    G.px = G.py = 0;
    set_speed();
    bb_dirty = 1;
}

void bb_new_game(uint32_t seed, uint8_t level)
{
    memset(&G, 0, sizeof(G));
    G.seed = G.rng = seed;
    G.lives = BB_LIVES;
    if (level < 1) level = 1;
    if (level > BB_LEVELS) level = BB_LEVELS;
    bb_load_level(level);
    G.bx = G.px;
    G.by = G.py;
    G.bz = (BB_R + 1) * BB_FX;
}

static void cell_xyz(uint8_t i, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t c = i, r = 0, l = 0;
    while (c >= BB_COLS * BB_ROWS) { c -= BB_COLS * BB_ROWS; ++l; }
    while (c >= BB_COLS) { c -= BB_COLS; ++r; }
    *x = -BB_W + c * BB_CW;
    *y = -BB_H + r * BB_CH;
    *z = BB_Z0 + l * BB_CD;
}

static void drop_capsule(uint8_t i)
{
    uint8_t k, r = (uint8_t)(rnd() & 7);   /* % 8 */
    int16_t x0, y0, z0;
    for (k = 0; k < BB_MAX_CAPS; k++) if (!G.cactive[k]) break;
    if (k == BB_MAX_CAPS) return;
    cell_xyz(i, &x0, &y0, &z0);
    G.cactive[k] = 1;
    G.ctype[k] = r < 3 ? CAP_WIDE : r < 5 ? CAP_SLOW : r < 6 ? CAP_LIFE : CAP_PLOUGH;
    G.cx[k] = (x0 + BB_CW / 2) * BB_FX;
    G.cy[k] = (y0 + BB_CH / 2) * BB_FX;
    G.cz[k] = z0 * BB_FX;
    G.caps_dropped++;
}

static void break_brick(uint8_t i)
{
    uint8_t t = G.cell[i], l = i >= 48 ? 2 : i >= 24 ? 1 : 0;
    G.cell[i] = BK_EMPTY;
    G.hits[i] = 0;
    G.left--;
    G.broken++;
    G.score += (t == BK_TOUGH ? 30 : t == BK_BONUS ? 50 : 10) + l * 5;
    bb_dirty = 1;
    event(BBE_BREAK, i, t);
    if (t == BK_BONUS) drop_capsule(i);
}

/* a hit on brick i; 1 if the ball bounces off it */
static uint8_t hit_brick(uint8_t i)
{
    uint8_t t = G.cell[i];
    if (t == BK_STEEL) { event(BBE_STEEL, i, t); return 1; }
    if (G.plough_t) { break_brick(i); return 0; }
    if (--G.hits[i] == 0) break_brick(i);
    else { G.score += 5; bb_dirty = 1; event(BBE_HIT, i, G.hits[i]); }
    return 1;
}

/* v / step for v >= 0, clamped to max */
static uint8_t cell_of(uint16_t v, uint16_t step, uint8_t max)
{
    uint8_t n = 0;
    while (v >= step && n < max) { v -= step; ++n; }
    return n;
}

/* the occupied cells the ball's box overlaps */
static uint8_t cells[12];
static uint8_t overlap(void)
{
    int16_t x = G.bx, y = G.by, z = G.bz, zl;
    uint8_t c0, c1, r0, r1, l0, l1, c, rr, l, n = 0, i;
    if (z + R32 <= BB_Z0 * BB_FX) return 0;
    c0 = cell_of((uint16_t)(x - R32 + BB_W * BB_FX), BB_CW * BB_FX, BB_COLS - 1);
    c1 = cell_of((uint16_t)(x + R32 - 1 + BB_W * BB_FX), BB_CW * BB_FX, BB_COLS - 1);
    r0 = cell_of((uint16_t)(y - R32 + BB_H * BB_FX), BB_CH * BB_FX, BB_ROWS - 1);
    r1 = cell_of((uint16_t)(y + R32 - 1 + BB_H * BB_FX), BB_CH * BB_FX, BB_ROWS - 1);
    zl = z - R32 - BB_Z0 * BB_FX;
    l0 = zl < 0 ? 0 : cell_of((uint16_t)zl, BB_CD * BB_FX, BB_LAYERS - 1);
    l1 = cell_of((uint16_t)(z + R32 - 1 - BB_Z0 * BB_FX), BB_CD * BB_FX, BB_LAYERS - 1);
    for (l = l0; l <= l1; l++)
        for (rr = r0; rr <= r1; rr++)
            for (c = c0; c <= c1; c++) {
                i = bb_index(c, rr, l);
                if (G.cell[i] != BK_EMPTY) cells[n++] = i;
            }
    return n;
}

static void paddle_bounce(void)
{
    int16_t s = cur_speed(), pw = bb_pad_w() * BB_FX, ph = bb_pad_h() * BB_FX;
    G.bz = 2 * R32 - G.bz;
    G.vz = -G.vz;
    G.vx += (int16_t)((int32_t)(G.bx - G.px) * (s / 2) / pw);
    G.vy += (int16_t)((int32_t)(G.by - G.py) * (s / 2) / ph);
    G.paddle_hits++;
    set_speed();
    renorm();
    event(BBE_PADDLE, 0, 0);
}

static uint8_t solid_hits(void)
{
    uint8_t n = overlap(), k, solid = 0;
    for (k = 0; k < n; k++) solid |= hit_brick(cells[k]);
    return solid;
}

#define LIMX ((BB_W - BB_R) * BB_FX)
#define LIMY ((BB_H - BB_R) * BB_FX)
#define LIMZ ((BB_D - BB_R) * BB_FX)

static void move_x(int16_t d)
{
    int16_t before = G.bx;
    G.bx += d;
    if (G.bx > LIMX) { G.bx = LIMX - (G.bx - LIMX); G.vx = -G.vx; event(BBE_WALL, 0, 1); }
    else if (G.bx < -LIMX) { G.bx = -LIMX - (G.bx + LIMX); G.vx = -G.vx; event(BBE_WALL, 0, 0); }
    if (G.bz + R32 > BB_Z0 * BB_FX && solid_hits()) { G.bx = before; G.vx = -G.vx; }
}

static void move_y(int16_t d)
{
    int16_t before = G.by;
    G.by += d;
    if (G.by > LIMY) { G.by = LIMY - (G.by - LIMY); G.vy = -G.vy; event(BBE_WALL, 1, 1); }
    else if (G.by < -LIMY) { G.by = -LIMY - (G.by + LIMY); G.vy = -G.vy; event(BBE_WALL, 1, 0); }
    if (G.bz + R32 > BB_Z0 * BB_FX && solid_hits()) { G.by = before; G.vy = -G.vy; }
}

static void move_z(int16_t d)
{
    int16_t before = G.bz, dx, dy;
    G.bz += d;
    if (G.bz > LIMZ) { G.bz = LIMZ - (G.bz - LIMZ); G.vz = -G.vz; event(BBE_WALL, 2, 1); }
    if (G.bz + R32 > BB_Z0 * BB_FX && solid_hits()) { G.bz = before; G.vz = -G.vz; }
    /* the paddle, crossing its plane on the way back */
    if (G.vz < 0 && before > R32 && G.bz <= R32) {
        dx = G.bx - G.px; if (dx < 0) dx = -dx;
        dy = G.by - G.py; if (dy < 0) dy = -dy;
        if (dx <= (bb_pad_w() + BB_R) * BB_FX && dy <= (bb_pad_h() + BB_R) * BB_FX) paddle_bounce();
    }
}

static void serve(int8_t dx, int8_t dy)
{
    int16_t s;
    G.stuck = 0;
    s = cur_speed();
    G.vx = (int16_t)(dx * s / 4 + (int32_t)(rnd() % 61) * s / 300 - s / 10);
    G.vy = (int16_t)(dy * s / 4 + (int32_t)(rnd() % 61) * s / 300 - s / 10);
    G.vz = 1;
    renorm();
    event(BBE_SERVE, 0, 0);
}

static void lose_ball(void)
{
    G.lives--;
    event(BBE_LOST, G.lives, 0);
    G.wide_t = G.slow_t = G.plough_t = 0;
    if (G.lives == 0) { G.over = 1; event(BBE_OVER, 0, 0); return; }
    G.stuck = 1;
    set_speed();
}

static void capsules(void)
{
    uint8_t k;
    int16_t before, dx, dy;
    for (k = 0; k < BB_MAX_CAPS; k++) {
        if (!G.cactive[k]) continue;
        before = G.cz[k];
        G.cz[k] -= 7 * BB_FX;
        if (before > 0 && G.cz[k] <= 0) {
            dx = G.cx[k] - G.px; if (dx < 0) dx = -dx;
            dy = G.cy[k] - G.py; if (dy < 0) dy = -dy;
            if (dx <= (bb_pad_w() + 14) * BB_FX && dy <= (bb_pad_h() + 10) * BB_FX) {
                G.cactive[k] = 0;
                G.score += 100;
                switch (G.ctype[k]) {
                case CAP_WIDE: G.wide_t = BB_POWER_TICKS; break;
                case CAP_SLOW: G.slow_t = BB_POWER_TICKS; if (!G.stuck) renorm(); break;
                case CAP_LIFE: if (G.lives < 9) G.lives++; break;
                case CAP_PLOUGH: G.plough_t = BB_POWER_TICKS / 2; break;
                }
                event(BBE_CAPSULE, G.ctype[k], 0);
                continue;
            }
        }
        if (G.cz[k] < BB_LOST_Z * BB_FX) { G.cactive[k] = 0; event(BBE_CAP_MISS, G.ctype[k], 0); }
    }
}

void bb_tick(uint8_t in)
{
    int8_t dx = (in & BB_IN_RIGHT ? 1 : 0) - (in & BB_IN_LEFT ? 1 : 0);
    int8_t dy = (in & BB_IN_UP ? 1 : 0) - (in & BB_IN_DOWN ? 1 : 0);
    int16_t lim;
    uint8_t k;

    if (G.over || G.won) return;
    G.tick++;
    if (G.between) {
        if (--G.between == 0) {
            if (G.level >= BB_LEVELS) { G.won = 1; event(BBE_WIN, 0, 0); return; }
            bb_load_level(G.level + 1);
            event(BBE_LEVEL, G.level, 0);
        }
        return;
    }
    /* the paddle */
    if (dx > 0) G.px += BB_PAD_SPEED * BB_FX; else if (dx < 0) G.px -= BB_PAD_SPEED * BB_FX;
    if (dy > 0) G.py += BB_PAD_SPEED * BB_FX; else if (dy < 0) G.py -= BB_PAD_SPEED * BB_FX;
    lim = (BB_W - bb_pad_w()) * BB_FX;
    if (G.px > lim) G.px = lim;
    if (G.px < -lim) G.px = -lim;
    lim = (BB_H - bb_pad_h()) * BB_FX;
    if (G.py > lim) G.py = lim;
    if (G.py < -lim) G.py = -lim;
    if (G.wide_t) G.wide_t--;
    if (G.plough_t) G.plough_t--;
    if (G.slow_t && --G.slow_t == 0 && !G.stuck) renorm();

    if (G.stuck) {
        G.bx = G.px;
        G.by = G.py;
        G.bz = (BB_R + 1) * BB_FX;
        if (in & BB_IN_FIRE) serve(dx, dy);
    } else {
        for (k = 0; k < 4; k++) {
            move_x(QUARTER(G.vx));
            move_y(QUARTER(G.vy));
            move_z(QUARTER(G.vz));
        }
        if (G.bz < BB_LOST_Z * BB_FX) lose_ball();
    }
    capsules();
    if (G.left == 0 && !G.over) {
        G.score += 250 * G.level;
        G.between = BB_BETWEEN;
        G.stuck = 1;
        memset(G.cactive, 0, sizeof(G.cactive));
        event(BBE_CLEAR, G.level, 0);
    }
}

void bb_clear_all(void)
{
    uint8_t i;
    for (i = 0; i < BB_CELLS; i++) if (bb_breakable(G.cell[i])) break_brick(i);
}

/* ---------- the autopilot ---------- */

/* p + v * t reflected back and forth within -lim..lim (port: in 1/2 units) */
static int32_t smul(int16_t a, int16_t b);
static int16_t fold(int16_t p, int16_t v, uint16_t t, int16_t lim)
{
    int16_t l = lim >> 4, period = 4 * l, q;
    q = (int16_t)(smul(v, t) >> 4) + (p >> 4) + l;
    q %= period;
    if (q < 0) q += period;
    if (q > 2 * l) q = period - q;
    return (q - l) << 4;
}

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352DUL; x ^= x >> 15; x *= 0x846CA68BUL; x ^= x >> 16;
    return x;
}

/* port: the hashes and what comes from them are worked out only when what
   they hash changes (32-bit maths is slow here) */
static int16_t stuck_x, stuck_y, off_x, off_y;
static uint16_t k_stuck = 0xFFFF, k_play = 0xFFFF;
static uint32_t k_seed;

static int32_t smul(int16_t a, int16_t b)
{
    uint32_t r = umul16x16r32((uint16_t)(a < 0 ? -a : a), (uint16_t)(b < 0 ? -b : b));
    return (a < 0) != (b < 0) ? -(int32_t)r : (int32_t)r;
}

uint8_t bb_bot(void)
{
    int16_t tx, ty, pw, ph;
    uint16_t t, key;
    const int16_t dead = BB_PAD_SPEED * BB_FX / 2;
    uint8_t in = 0, k;
    uint32_t h;

    if (G.over || G.won || G.between) return 0;
    if (k_seed != G.seed) { k_seed = G.seed; k_stuck = k_play = 0xFFFF; }
    if (G.stuck) {
        key = G.broken * 64 + G.lives * 8 + G.level;
        if (key != k_stuck) {
            k_stuck = key;
            h = mix(G.seed * 31 + (uint32_t)G.lives * 7 + (uint32_t)G.level * 131 + (uint32_t)G.broken);
            stuck_x = (int16_t)(((int32_t)(h % 201) - 100) * ((BB_W - BB_PAD_W) * BB_FX) / 100);
            stuck_y = (int16_t)(((int32_t)((h >> 8) % 201) - 100) * ((BB_H - BB_PAD_H) * BB_FX) / 100);
        }
        tx = stuck_x; ty = stuck_y;
        if (G.px - tx < dead && tx - G.px < dead && G.py - ty < dead && ty - G.py < dead) return BB_IN_FIRE;
    } else {
        key = G.paddle_hits * 2 + (G.wide_t != 0);
        if (key != k_play) {
            k_play = key;
            pw = bb_pad_w() * BB_FX; ph = bb_pad_h() * BB_FX;
            h = mix(G.seed + (uint32_t)G.paddle_hits * 2654435761UL);
            /* where on the paddle to take it: varied, so the ball goes everywhere */
            off_x = (int16_t)(((int32_t)(h % 121) - 60) * pw / 100);
            off_y = (int16_t)(((int32_t)((h >> 10) % 121) - 60) * ph / 100);
        }
        if (G.vz < 0) t = G.bz > R32 ? (uint16_t)(G.bz - R32) / (uint16_t)-G.vz : 0;
        else t = ((uint16_t)LIMZ - G.bz + (uint16_t)((BB_D - 2 * BB_R) * BB_FX)) / (uint16_t)G.vz;
        tx = fold(G.bx, G.vx, t, LIMX) - off_x;
        ty = fold(G.by, G.vy, t, LIMY) - off_y;
        /* a capsule about to arrive, while the ball is far: fetch it */
        if (G.bz > 350 * BB_FX && G.vz > 0)
            for (k = 0; k < BB_MAX_CAPS; k++)
                if (G.cactive[k] && G.cz[k] < 150 * BB_FX) { tx = G.cx[k]; ty = G.cy[k]; break; }
    }
    if (tx > G.px + dead) in |= BB_IN_RIGHT;
    if (tx < G.px - dead) in |= BB_IN_LEFT;
    if (ty > G.py + dead) in |= BB_IN_UP;
    if (ty < G.py - dead) in |= BB_IN_DOWN;
    return in;
}
