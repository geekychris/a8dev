/*
 * mt_core.c - Marble Tilt's rules (see mt.h), from the Amiga original's
 * mt_core.c: the levels, the tilt, rolling and bouncing, bumpers, gates,
 * sand, holes and checkpoints, the clock, and the autopilot.
 */
#include <string.h>
#include "a8game.h"
#include "a3d.h"
#include "mt.h"

/* this file's BSS (and -Cl's static locals) in the SDK's LOWRAM */
#pragma bss-name ("LOWRAM")

MTGame G;

const char *const mt_level_names[MT_LEVELS] = {
    "FIRST ROLL", "THE MOAT", "ZIGZAG", "GATEHOUSE", "PINBALL", "SAND TRAP", "TIGHTROPE", "THE GAUNTLET"
};
static const char levels[MT_LEVELS][MT_N][MT_N + 1] = {
    { "############", "#S....#....#", "#.##..#.##.#", "#.##....##.#", "#..........#", "####.##.####",
      "#....##....#", "#.##....##.#", "#.##.##.##.#", "#....##...E#", "#....##....#", "############" },
    { "############", "#S.........#", "#.        .#", "#. ...... .#", "#. .####. .#", "#. .#E .. .#",
      "#. .#.##. .#", "#. ...... .#", "#.   .    .#", "#..........#", "#..........#", "############" },
    { "############", "#S.....    #", "#####..    #", "#   ...    #", "#   ..######", "#   .....  #",
      "#####...   #", "#    ...   #", "#    ..#####", "#     ....E#", "#      ... #", "############" },
    { "############", "#S...#.....#", "#....G.....#", "#....#.....#", "###.####G###", "#..........#",
      "#.  ....  .#", "#.  .##.  .#", "#.  .##.  .#", "#....GG...E#", "#..........#", "############" },
    { "############", "#S.........#", "#..B...B...#", "#..........#", "#....B.....#", "#.B......B.#",
      "#....  ....#", "#...    ...#", "#.B..  ..B.#", "#..........#", "#....B....E#", "############" },
    { "############", "#S.ZZZZ....#", "#..ZZZZ.##.#", "#.......#  #", "####.####  #", "#....ZZZ...#",
      "#.##.ZZZ.#.#", "#.#  ...  ##", "#.#  .#.   #", "#....#E.   #", "#....#..   #", "############" },
    { "############", "#S.        #", "# .  .....E#", "# .  .     #", "# .  .  ...#", "# ...C  .  #",
      "#       .  #", "#  ......  #", "#  .       #", "#  ......  #", "#       .  #", "############" },
    { "############", "#S..B.  ...#", "#.....  .G.#", "###.##  .#.#", "#  .....C#.#", "#  .B.  ##.#",
      "#  ...  ...#", "#####.###G##", "#.....B....#", "#.  ....  .#", "#E  .  .  .#", "############" },
};

#define TILT_RATE  6                    /* MT_TILT_MAX / 5 */
#define VMAX       1280                 /* 5 units a tick */
#define BUMP_KICK  640                  /* 2.5 */
#define BUMPER_R   13
#define HALFC      (MT_CELL / 2 * MT_P)
#define RAD        (MT_R * MT_P)

/* the marble's position, finer: 1/64 unit x 256 (the C rules use >> 8) */
static int32_t px, pz;
static uint8_t gate_t, gate_phase;

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

static void event(uint8_t type, uint8_t a)
{
    if (G.nev < 8) { G.ev[G.nev] = type; G.eva[G.nev] = a; ++G.nev; }
}

uint8_t mt_pop_event(uint8_t *a)
{
    uint8_t e;
    if (!G.nev) return MTE_NONE;
    e = G.ev[0];
    *a = G.eva[0];
    --G.nev;
    memmove(G.ev, G.ev + 1, G.nev);
    memmove(G.eva, G.eva + 1, G.nev);
    return e;
}

/* port: tables instead of multiplies and divides by 40 */
#define CX(c) (((c) - MT_N / 2) * MT_CELL + MT_CELL / 2) * MT_P
static const int16_t cell_xt[MT_N] = { CX(0), CX(1), CX(2), CX(3), CX(4), CX(5), CX(6), CX(7), CX(8), CX(9), CX(10), CX(11) };
#define cell_x(c) (cell_xt[(uint8_t)(c)])
#define cell_z(r) (-cell_xt[(uint8_t)(r)])
/* (v >> 3) / 5 = v / 40 exactly (40 = 5 x 8), v = 0..479 */
static const uint8_t div40[60] = {
    0,0,0,0,0, 1,1,1,1,1, 2,2,2,2,2, 3,3,3,3,3, 4,4,4,4,4, 5,5,5,5,5,
    6,6,6,6,6, 7,7,7,7,7, 8,8,8,8,8, 9,9,9,9,9, 10,10,10,10,10, 11,11,11,11,11 };
static int8_t cell_of(int16_t v) { return v < 0 ? -1 : v >= 480 ? 12 : (int8_t)div40[(uint8_t)(v >> 3)]; }
int8_t mt_col_of(int16_t x) { return cell_of((x >> 6) + MT_N / 2 * MT_CELL); }
int8_t mt_row_of(int16_t z) { return cell_of(MT_N / 2 * MT_CELL - (z >> 6)); }

char mt_at(int8_t row, int8_t col)
{
    if ((uint8_t)row >= MT_N || (uint8_t)col >= MT_N) return '#';
    return G.cell[0][((uint8_t)row << 4) | (uint8_t)col];
}

static void set_pos(void) { px = (int32_t)G.x << 8; pz = (int32_t)G.z << 8; }

static void put_at(uint8_t row, uint8_t col)
{
    G.x = cell_x(col);
    G.z = cell_z(row);
    G.vx = G.vz = 0;
    set_pos();
}

/* the autopilot's distances to the exit (breadth-first, through floor,
   sand, checkpoints and gates, round walls, bumpers and holes) */
static uint8_t dist[MT_N * 16];         /* stride 16, as the cells */

static void route(void)
{
    static uint8_t q[MT_N * MT_N];
    static const int8_t d4[4] = { -16, 16, -1, 1 };
    uint8_t head = 0, tail = 0, k, cur, n, r, c;
    char ch;
    memset(dist, 0xFF, sizeof dist);
    for (r = 0; r < MT_N; ++r)
        for (c = 0; c < MT_N; ++c)
            if (G.cell[r][c] == 'E') { k = (r << 4) | c; dist[k] = 0; q[tail++] = k; }
    while (head < tail) {
        cur = q[head++];
        r = cur >> 4; c = cur & 15;
        for (k = 0; k < 4; ++k) {
            if ((k == 0 && r == 0) || (k == 1 && r == MT_N - 1) || (k == 2 && c == 0) || (k == 3 && c == MT_N - 1)) continue;
            n = (uint8_t)(cur + d4[k]);
            ch = G.cell[0][n];
            if (ch == '#' || ch == ' ' || ch == 'B' || dist[n] != 0xFF) continue;
            dist[n] = dist[cur] + 1;
            q[tail++] = n;
        }
    }
}

static void load_level(uint8_t level)
{
    uint8_t r, c;
    G.level = level;
    for (r = 0; r < MT_N; r++) {
        memcpy(G.cell[r], levels[level - 1][r], MT_N);
        for (c = 0; c < MT_N; c++)
            if (G.cell[r][c] == 'S') { G.cp_row = r; G.cp_col = c; }
    }
    put_at(G.cp_row, G.cp_col);
    G.roll = G.pitch = 0;
    G.falling = 0;
    G.gates_open = 1;
    G.time_left = (50 + level * 10) * 25;
    G.level_ticks = 0;
    gate_t = gate_phase = 0;
    route();
}

void mt_new_game(uint8_t level)
{
    memset(&G, 0, sizeof G);
    if (level < 1) level = 1;
    if (level > MT_LEVELS) level = MT_LEVELS;
    load_level(level);
}

static int8_t ease(int8_t v, int8_t target)
{
    if (v < target) { v += TILT_RATE; if (v > target) v = target; }
    else if (v > target) { v -= TILT_RATE; if (v < target) v = target; }
    return v;
}

/* |(a, b)| (1/8 units: |a|, |b| < 2048), via isqrt16 */
static int16_t dist8(int16_t a, int16_t b)
{
    a >>= 3; b >>= 3;
    return (int16_t)isqrt16((uint16_t)(a * a + b * b)) << 3;
}

/* the marble against the walls and bumpers round it */
static void collide(void)
{
    int8_t r, c, r1, c1, rlo, clo;
    int16_t cx, cz, nx, nz, dx, dz, d, vn, ux, uz;
    char ch;
    /* port: only the cells in reach. A wall next door reaches the marble
       only within RAD of the shared edge, a bumper next door even less, so
       a neighbour counts only when the marble is that near its side of
       its own cell */
    r = mt_row_of(G.z); c = mt_col_of(G.x);
    d = G.x - cell_x(c);
    clo = d < RAD - HALFC ? c - 1 : c;
    c1 = d > HALFC - RAD ? c + 1 : c;
    d = G.z - cell_z(r);
    rlo = d > HALFC - RAD ? r - 1 : r;                  /* z grows away, rows nearer */
    r1 = d < RAD - HALFC ? r + 1 : r;
    for (r = rlo; r <= r1; r++)
        for (c = clo; c <= c1; c++) {
            ch = mt_at(r, c);
            if (ch != 'B' && ch != '#' && (ch != 'G' || G.gates_open)) continue;
            cx = cell_x(c); cz = cell_z(r);
            if (ch == 'B') {
                dx = G.x - cx; dz = G.z - cz;
                if (iabs(dx) >= (BUMPER_R + MT_R) * MT_P || iabs(dz) >= (BUMPER_R + MT_R) * MT_P) continue;
                d = dist8(dx, dz);
                if (d >= (BUMPER_R + MT_R) * MT_P) continue;
                if (d < 64) { dx = 64; dz = 0; d = 64; }
                ux = a3d_muldiv(dx, 256, d);                   /* the normal, Q8 */
                uz = a3d_muldiv(dz, 256, d);
                G.x = cx + a3d_mulf(ux, (BUMPER_R + MT_R) * MT_P);
                G.z = cz + a3d_mulf(uz, (BUMPER_R + MT_R) * MT_P);
                vn = a3d_mulf(G.vx, ux) + a3d_mulf(G.vz, uz);
                if (vn < 0) { G.vx -= a3d_mulf(2 * vn, ux); G.vz -= a3d_mulf(2 * vn, uz); }
                G.vx += a3d_mulf(BUMP_KICK, ux);
                G.vz += a3d_mulf(BUMP_KICK, uz);
                ++G.bumps;
                event(MTE_BUMP, 0);
            } else {
                /* the nearest point of the cell's square */
                nx = G.x < cx - HALFC ? cx - HALFC : G.x > cx + HALFC ? cx + HALFC : G.x;
                nz = G.z < cz - HALFC ? cz - HALFC : G.z > cz + HALFC ? cz + HALFC : G.z;
                dx = G.x - nx; dz = G.z - nz;
                if (iabs(dx) >= RAD || iabs(dz) >= RAD) continue;
                d = dist8(dx, dz);
                if (d >= RAD) continue;
                if (d < 16) {                                   /* the middle inside the wall: out the way it came */
                    if (iabs(G.vx) > iabs(G.vz)) { dx = G.vx > 0 ? -64 : 64; dz = 0; }
                    else { dz = G.vz > 0 ? -64 : 64; dx = 0; }
                    d = 64;
                }
                ux = a3d_muldiv(dx, 256, d);
                uz = a3d_muldiv(dz, 256, d);
                G.x += a3d_mulf(ux, RAD - d);
                G.z += a3d_mulf(uz, RAD - d);
                vn = a3d_mulf(G.vx, ux) + a3d_mulf(G.vz, uz);
                if (vn < 0) {                                   /* bounce back at 40% */
                    G.vx -= a3d_mulf(a3d_mulf(vn, 358), ux);
                    G.vz -= a3d_mulf(a3d_mulf(vn, 358), uz);
                    if (vn < -384) event(MTE_WALL, (uint8_t)(-vn >> 6));
                }
            }
            set_pos();
        }
}

/* v / 8 on sand, else v / 100 (port: 41 / 4096, no divides), towards 0 */
static int16_t drag(int16_t v, uint8_t sand)
{
    int16_t f, a = iabs(v);
    f = sand ? a >> 3 : (int16_t)((uint16_t)a3d_mulf(a, 41) >> 4);
    return v < 0 ? -f : f;
}

void mt_tick(uint8_t in)
{
    int8_t r, c;
    char ch;
    uint8_t open, k;
    int16_t sp, bonus;
    if (G.over || G.won) return;
    if (G.between) {
        if (--G.between == 0) {
            if (G.level >= MT_LEVELS) { G.won = 1; event(MTE_WIN, 0); return; }
            load_level(G.level + 1);
            event(MTE_LEVEL, G.level);
        }
        return;
    }
    ++G.level_ticks;
    /* the gates (port: a countdown, not a divide) */
    if (++gate_t == MT_GATE_T) { gate_t = 0; gate_phase ^= 1; }
    open = !gate_phase;
    if (!open && mt_at(mt_row_of(G.z), mt_col_of(G.x)) == 'G') open = 1;     /* not on the marble */
    if (open != G.gates_open) { G.gates_open = open; event(MTE_GATE, open); }
    /* the tilt */
    G.roll = ease(G.roll, (in & MT_IN_RIGHT) ? MT_TILT_MAX : (in & MT_IN_LEFT) ? -MT_TILT_MAX : 0);
    G.pitch = ease(G.pitch, (in & MT_IN_UP) ? MT_TILT_MAX : (in & MT_IN_DOWN) ? -MT_TILT_MAX : 0);

    if (G.falling) {
        if (--G.falling == 0) {
            put_at(G.cp_row, G.cp_col);
            event(MTE_RESPAWN, 0);
        }
    } else {
        r = mt_row_of(G.z);
        c = mt_col_of(G.x);
        ch = mt_at(r, c);
        G.vx += G.roll;                                     /* sin(tilt) ~ tilt: 1/256 unit a tick^2 */
        G.vz += G.pitch;
        /* rolling friction; sand drags */
        G.vx -= drag(G.vx, ch == 'Z');
        G.vz -= drag(G.vz, ch == 'Z');
        sp = iabs(G.vx) > iabs(G.vz) ? iabs(G.vx) + (iabs(G.vz) >> 2) + (iabs(G.vz) >> 3)
                                     : iabs(G.vz) + (iabs(G.vx) >> 2) + (iabs(G.vx) >> 3);
        if (sp > VMAX) { G.vx = a3d_muldiv(G.vx, VMAX, sp); G.vz = a3d_muldiv(G.vz, VMAX, sp); }
        for (k = 0; k < 2; k++) {                           /* port: 2 steps a tick (the original: 4) */
            px += (int32_t)G.vx << 5;                       /* v / 2 in 1/64 unit x 256 */
            pz += (int32_t)G.vz << 5;
            G.x = (int16_t)(px >> 8);
            G.z = (int16_t)(pz >> 8);
            collide();
        }
        r = mt_row_of(G.z);
        c = mt_col_of(G.x);
        ch = mt_at(r, c);
        if (ch == ' ') {
            G.falling = MT_FALL_T;
            ++G.falls;
            G.time_left -= 3 * 25;
            event(MTE_FALL, 0);
        } else if (ch == 'C' && (G.cp_row != (uint8_t)r || G.cp_col != (uint8_t)c)) {
            G.cp_row = r;
            G.cp_col = c;
            event(MTE_CHECKPOINT, 0);
        } else if (ch == 'E') {
            bonus = (G.time_left / 25) * 10 + 100 * G.level;
            G.score += bonus;
            G.between = MT_BETWEEN;
            event(MTE_EXIT, G.level);
            return;
        }
    }
    if (--G.time_left <= 0) {
        G.time_left = 0;
        G.over = 1;
        event(MTE_TIMEOUT, 0);
        event(MTE_OVER, 0);
    }
}

/* ---------- the autopilot ---------- */
/* the next cell on the route from (r, c): (row << 4) | col, or -1 */
static int16_t next_cell(int8_t r, int8_t c)
{
    static const int8_t d4[4] = { -16, 16, -1, 1 };
    uint8_t k, here, at, n;
    if ((uint8_t)r >= MT_N || (uint8_t)c >= MT_N) return -1;
    at = ((uint8_t)r << 4) | (uint8_t)c;
    here = dist[at];
    if (here == 0xFF) return -1;
    if (here == 0) return at;
    for (k = 0; k < 4; ++k) {
        n = (uint8_t)(at + d4[k]);
        if ((n & 15) < MT_N && (n >> 4) < MT_N && dist[n] == here - 1) return n;
    }
    return -1;
}

uint8_t mt_bot(void)
{
    int8_t r, c, nr, nc, k;
    int16_t next, n2;
    int16_t tx, tz, dx, dz, d, vdx, vdz, cap, wx, wz, sp, k16;
    uint8_t in = 0;
    static const int8_t d8r[8] = { -1, 1, 0, 0, -1, -1, 1, 1 }, d8c[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    if (G.over || G.won || G.between || G.falling) return 0;
    r = mt_row_of(G.z);
    c = mt_col_of(G.x);
    next = next_cell(r, c);
    if (next < 0) return 0;
    nr = next >> 4; nc = next & 15;
    tx = cell_x(nc); tz = cell_z(nr);
    /* the cell after next, if the route goes straight on: aim through */
    n2 = next_cell(nr, nc);
    if (n2 >= 0 && n2 != next && (n2 >> 4) - nr == nr - r && (n2 & 15) - nc == nc - c) {
        tx = cell_x(n2 & 15); tz = cell_z(n2 >> 4);
    }
    /* a gate ahead that is shut: wait short of it */
    if (mt_at(nr, nc) == 'G' && !G.gates_open) { tx = cell_x(c); tz = cell_z(r); }
    dx = tx - G.x; dz = tz - G.z;                           /* 1/64 unit */
    d = dist8(dx, dz);
    /* slow beside holes */
    cap = 410;                                              /* 1.6 units a tick */
    for (k = 0; k < 8; ++k) if (mt_at(r + d8r[k], c + d8c[k]) == ' ') { cap = 230; break; }
    if (d > 32) {
        sp = d / 3;                                         /* d / 12 units -> 1/256 a tick: (d/64) / 12 * 256 */
        if (sp > cap) sp = cap;
        k16 = (sp << 6) / (d >> 2);                         /* sp / d, Q8 (port: one divide) */
        vdx = a3d_mulf(dx, k16);
        vdz = a3d_mulf(dz, k16);
    } else vdx = vdz = 0;
    /* tilt to bring the speed to that */
    wx = vdx - G.vx;
    wz = vdz - G.vz;
    if (wx > 20) in |= MT_IN_RIGHT;
    else if (wx < -20) in |= MT_IN_LEFT;
    if (wz > 20) in |= MT_IN_UP;
    else if (wz < -20) in |= MT_IN_DOWN;
    return in;
}
