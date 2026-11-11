/*
 * game.c - Planet Chomp's rules: the original's game.c (GameDirector.cs,
 * Ghost.cs, Chomper.cs, Mover.cs) on the maze's cell graph. 25 steps a
 * second (port: the original 50: the times halved, the distances doubled).
 *
 * port: the original moves on rails between cells as 3D directions (Q14
 * vectors, normalised every step: on a 6502 that alone is more than a
 * frame). Here a mover is the same (from, to, t along the edge), but every
 * choice is made in exits: the player's screen direction is an exit through
 * the view's orientation (view.c), cruising straight on is the exit
 * opposite the one it came in by (the cube grid's), a spook's target is a
 * Q7 direction and it takes the exit whose cell is nearest it by the dot
 * product, as the original does with Q14 ones. Edges all have the average
 * length (the view draws every cell the same size). The rest is the
 * original's: the speeds, the spooks' four brains, scatter and chase,
 * fright, the keys, the combo, lives, levels, the autopilot.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "maze.h"
#include "game.h"

#pragma bss-name (push, "LOWRAM")          /* $0C00-$1FFF (not zeroed: game_init does) */
Game g;
#pragma bss-name (pop)
const char *const ghost_name[4] = { "GLOOM", "SPARK", "MIST", "MOSS" };

#define START_LIVES   3
#define EXTRA_LIFE_AT 10000
#define CATCH_D2      100             /* 0.08 rad between them (the original's CATCH_COS): Q7 chord^2 */
#define CELL_LEN      536             /* an edge, world Q8 (the original's average: 2.09 units) */

/* port: the rules step 25 times a second (the original 50): every time in steps is halved, every distance a step doubled */
#define TICKS(s100) ((s100) * 25 / 100)   /* hundredths of a second -> steps */
static const int16_t mode_ticks[8] = { 175, 500, 175, 500, 125, 500, 125, 0x7FFF };   /* 7, 20, 7, 20, 5, 20, 5 s */

uint8_t open_of(uint16_t c)
{
    return (uint8_t)(g.open[c >> 1] >> ((c & 1) << 2)) & 15;
}


#define OPEN(c, k)  ((open_of(c) >> (k)) & 1)

/* ---- movement on rails (Mover.cs) ---- */

typedef int8_t (*Chooser)(void *ctx, uint16_t cell, int8_t cruising);
typedef void (*Arrived)(void *ctx, uint16_t cell);

static void mover_place(Mover *m, uint16_t cell, uint8_t facing)
{
    m->from = cell; m->to = NONE; m->prev = NONE; m->t = 0; m->moving = 0; m->exitk = 0xFF;
    m->head = facing;
}

uint16_t mover_nearest(const Mover *m) { return m->moving && m->t > 32768u ? m->to : m->from; }

static void mover_reverse(Mover *m)
{
    uint16_t f;
    uint8_t back;
    if (!m->moving) return;
    back = REV(m->from, m->exitk);
    f = m->from; m->from = m->to; m->to = f;
    m->exitk = back;
    m->t = (uint16_t)(0 - m->t);
    m->prev = m->from;
    m->head = back;
}

/* t is Q16 of an edge: dist (world Q8) is dist * 65536 / CELL_LEN
   (port: 16 bits: a step's distance is under 537, so dt < 65536) */
static void mover_advance(Mover *m, uint16_t dist, Chooser choose, Arrived arrived, void *ctx)
{
    int8_t cruising = (int8_t)m->moving, guard;
    uint16_t dt = dist * (uint16_t)(65536UL / CELL_LEN);      /* port: every edge the same length */
    for (guard = 0; dt > 0 && guard < 8; guard++) {
        uint16_t remain;
        if (!m->moving) {
            int8_t k = choose(ctx, m->from, cruising);
            if (k < 0) break;
            m->to = NB(m->from, k);
            m->exitk = (uint8_t)k;
            m->t = 0;
            m->moving = 1;
        }
        remain = (uint16_t)(0 - m->t);                 /* (0: all 65536 of it) */
        if (remain == 0 || dt < remain) {
            m->t += dt;
            dt = 0;
        } else {
            uint8_t back = REV(m->from, m->exitk);
            dt -= remain;
            m->prev = m->from;
            m->from = m->to;
            m->to = NONE;
            m->t = 0;
            m->moving = 0;
            m->head = (uint8_t)((back + 2) & 3);      /* straight on: the exit opposite the way in */
            cruising = 1;
            if (arrived) arrived(ctx, m->from);
        }
    }
    if (m->moving) m->head = m->exitk;
}

/* a mover within a cell of the player (the quick test before mover_pos):
   the player's cell and its neighbours, made once a step (player_cells) */
static uint16_t pcell[5];
static void player_cells(void)
{
    uint16_t c = mover_nearest(&g.player);
    pcell[0] = c;
    pcell[1] = NB(c, 0);
    pcell[2] = NB(c, 1);
    pcell[3] = NB(c, 2);
    pcell[4] = NB(c, 3);
}
static uint8_t near_cells(const Mover *a, const Mover *b)
{
    uint16_t ca = mover_nearest(a);
    (void)b;
    return ca == pcell[0] || ca == pcell[1] || ca == pcell[2] || ca == pcell[3] || ca == pcell[4];
}

/* a mover's place: its cells' directions mixed by t (Q7, not normalised:
   the original's mover_dir, for the distances that need it) */
/* port: in asm (fastmath.s) */
extern int32_t __fastcall__ dot3(const int16_t *a, const int8_t *b);
extern int16_t __fastcall__ lerp8(int8_t a, int8_t b, uint8_t t);
#ifdef MATHCHECK                           /* make MATHCHECK=1: the asm against the C */
static int32_t dot3_asm(const int16_t *a, const int8_t *b) { return dot3(a, b); }
#define dot3(a, b) dot3_chk(a, b)
static int32_t dot3_chk(const int16_t *a, const int8_t *b)
{
    int32_t r = dot3_asm(a, b), c = (int32_t)a[0] * b[0] + (int32_t)a[1] * b[1] + (int32_t)a[2] * b[2];
    if (r != c) a8_log("MATHCHECK dot3 %d %d %d . %d %d %d", a[0], a[1], a[2], b[0], b[1], b[2]);
    return c;
}
static int16_t lerp8_chk(int8_t a, int8_t b, uint8_t t)
{
    int16_t r = lerp8(a, b, t), c = (int16_t)(a + (int16_t)(((int16_t)(b - a) * t) >> 8));
    if (r != c) a8_log("MATHCHECK lerp8 %d %d %u", a, b, t);
    return c;
}
#define lerp8(a, b, t) lerp8_chk(a, b, t)
#endif

void mover_pos(const Mover *m, int16_t *p)
{
    const int8_t *a = mz_dir[m->from];
    uint8_t i;
    if (!m->moving) { p[0] = a[0]; p[1] = a[1]; p[2] = a[2]; return; }
    {
        const int8_t *b = mz_dir[m->to];
        uint8_t t = (uint8_t)(m->t >> 8);
        for (i = 0; i < 3; ++i) p[i] = lerp8(a[i], b[i], t);
    }
}


/* ---- the player (Chomper.cs) ---- */

/* the exit at c the player's screen direction picks (view.c's orientation) */
extern int8_t view_exit_for(uint16_t cell, uint8_t screen_dir);
extern void view_level(void);
extern uint8_t view_screen_dir(uint16_t cell, uint8_t exit);

static int8_t pend = -1, last = -1;   /* screen direction 0 up 1 right 2 down 3 left, -1 none */
static uint8_t held_now;
static const uint8_t dir_bit[4] = { IN_UP, IN_RIGHT, IN_DOWN, IN_LEFT };

static uint8_t held_dir(int8_t d) { return d >= 0 && (held_now & dir_bit[d]); }
static void consume_pending(void) { if (!held_dir(pend)) pend = -1; }

/* ---- the autopilot (GameDirector.BotChoose / BotDodge) ---- */
#pragma bss-name (push, "HIBSS")           /* under the OS ROM (planet_chomp.cfg) */
static uint8_t danger[CELLS];
static int16_t bdist[CELLS], bparent[CELLS], bq[CELLS];
#pragma bss-name (pop)

static uint8_t want_cell(uint16_t n, int16_t d)
{
    uint8_t want = g.crumb[n] || (g.key[n] && d < 6), k;
    if (g.power_left > TICKS(150))
        for (k = 0; k < 4; ++k)
            if (g.ghost[k].fright && g.ghost[k].mode == GM_ACTIVE && mover_nearest(&g.ghost[k].mv) == n)
                want = d < 12;
    return want;
}

static int8_t bot_choose(uint16_t c)
{
    uint16_t i, head = 0, tail = 0, target = NONE;
    uint8_t k;
    memset(danger, 0, sizeof danger);
    for (i = 0; i < CELLS; ++i) bdist[i] = 0x7FFF;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        uint16_t gc;
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        gc = mover_nearest(&gh->mv);
        danger[gc] = 1;
        for (k = 0; k < 4; k++) if (OPEN(gc, k)) danger[NB(gc, k)] = 1;
        if (gh->mv.moving) danger[gh->mv.from == gc ? gh->mv.to : gh->mv.from] = 1;
    }
    bdist[c] = 0;
    bq[tail++] = (int16_t)c;
    /* port: stops at the first cell it wants: the original takes the
       nearest after the whole search, and the queue's order is the same */
    while (head < tail) {
        uint16_t x = (uint16_t)bq[head++];
        if (head > 1 && want_cell(x, bdist[x])) { target = x; break; }
        for (k = 0; k < 4; k++) {
            uint16_t nb = NB(x, k);
            if (!OPEN(x, k) || bdist[nb] != 0x7FFF || danger[nb]) continue;
            bdist[nb] = (int16_t)(bdist[x] + 1);
            bparent[nb] = (int16_t)x;
            bq[tail++] = (int16_t)nb;
        }
    }
    if (target != NONE) {
        uint16_t step = target;
        while ((uint16_t)bparent[step] != c) step = (uint16_t)bparent[step];
        for (k = 0; k < 4; k++) if (NB(c, k) == step && OPEN(c, k)) return (int8_t)k;
    }
    {
        /* nothing safe to eat: the exit farthest from the nearest spook */
        int8_t bk = -1;
        int32_t bscore = -0x7FFFFFFFL;
        int16_t gp[3];
        for (k = 0; k < 4; k++) {
            int32_t worst = -0x7FFFFFFFL;
            if (!OPEN(c, k)) continue;
            for (i = 0; i < 4; i++)
                if (ghost_dangerous(&g.ghost[i])) {
                    int32_t d;
                    mover_pos(&g.ghost[i].mv, gp);
                    d = dot3(gp, mz_dir[NB(c, k)]);
                    if (d > worst) worst = d;
                }
            if (-worst > bscore) { bscore = -worst; bk = (int8_t)k; }
        }
        return bk;
    }
}

static void bot_dodge(void)
{
    uint8_t i;
    int16_t p[3], gp[3];
    if (!g.player.moving) return;
    mover_pos(&g.player, p);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        int16_t d[3];
        const int8_t *hf, *ht;
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        if (!near_cells(&gh->mv, &g.player)) continue;
        mover_pos(&gh->mv, gp);
        d[0] = gp[0] - p[0]; d[1] = gp[1] - p[1]; d[2] = gp[2] - p[2];
        /* within ~1.3 cells (the original: cos > 15960/16384) */
        if ((int32_t)d[0] * d[0] + (int32_t)d[1] * d[1] + (int32_t)d[2] * d[2] > 840) continue;
        /* ahead of us: the spook's offset along the heading (to - from) */
        hf = mz_dir[g.player.from];
        ht = mz_dir[g.player.to];
        if ((int32_t)d[0] * (ht[0] - hf[0]) + (int32_t)d[1] * (ht[1] - hf[1]) + (int32_t)d[2] * (ht[2] - hf[2]) > 0) {
            mover_reverse(&g.player);
            return;
        }
    }
}

static int8_t player_choose(void *ctx, uint16_t c, int8_t cruising)
{
    (void)ctx;
    if (g.demo) return bot_choose(c);
    if (pend >= 0) {
        int8_t k = view_exit_for(c, (uint8_t)pend);
        if (k >= 0 && OPEN(c, k)) { consume_pending(); return k; }
    }
    return cruising && OPEN(c, g.player.head) ? (int8_t)g.player.head : -1;
}

static void add_score(int16_t pts);
static void start_power(void);
static void set_state(uint8_t s);

static void player_arrived(void *ctx, uint16_t c)
{
    (void)ctx;
    g.arrived = 1;
    if (g.crumb[c]) {
        g.crumb[c] = 0;
        g.crumbs_left--;
        add_score(10);
        g.sfx |= SFX_CRUMB;
        if (g.crumbs_left == 0) {
            g.sfx |= SFX_CLEAR;
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_CLEAR);
            a8_log("pc: clear level=%u score=%ld\n", g.level, g.score);
        }
    } else if (g.key[c]) {
        g.key[c] = 0;
        add_score(50);
        g.sfx |= SFX_KEY;
        start_power();
    }
}

static void player_tick(void)
{
    if (g.player.moving && pend >= 0) {
        /* a screen direction opposite the way we're going: turn round now */
        uint8_t s = view_screen_dir(g.player.from, g.player.exitk);
        if (((s + 2) & 3) == (uint8_t)pend) {
            mover_reverse(&g.player);
            consume_pending();
        }
    }
    mover_advance(&g.player, g.d_player, player_choose, player_arrived, 0);
    if (g.player.moving) g.mouth++;
}

/* ---- the spooks (Ghost.cs) ---- */

static uint16_t grnd(Ghost *gh, uint16_t n)
{
    gh->rng = gh->rng * 1103515245UL + 12345UL;
    return (uint16_t)((gh->rng >> 16) % n);
}

uint8_t ghost_dangerous(const Ghost *gh) { return gh->mode != GM_EATEN && !gh->fright; }

/* the cell `cells` ahead of the player straight on (port: on the grid,
   through the walls, as the original's point ahead on the sphere) */
static uint16_t ahead(uint8_t cells)
{
    uint16_t c = mover_nearest(&g.player);
    uint8_t k = g.player.moving ? g.player.exitk : g.player.head;
    if (g.player.moving && c == g.player.to) k = (uint8_t)((REV(g.player.from, g.player.exitk) + 2) & 3);
    while (cells--) {
        uint16_t n = NB(c, k);
        k = (uint8_t)((REV(c, k) + 2) & 3);
        c = n;
    }
    return c;
}

static void ghost_target(Ghost *gh, int16_t *t)
{
    int16_t pd[3];
    mover_pos(&g.player, pd);
    if (g.scatter) { t[0] = gh->scatter[0]; t[1] = gh->scatter[1]; t[2] = gh->scatter[2]; return; }
    switch (gh->kind) {
    case GK_AMBUSHER: {
        const int8_t *a = mz_dir[ahead(4)];
        t[0] = a[0]; t[1] = a[1]; t[2] = a[2];
        return;
    }
    case GK_FLANKER: {
        const int8_t *pv = mz_dir[ahead(2)];
        int16_t ch[3];
        mover_pos(&g.ghost[0].mv, ch);
        t[0] = 2 * pv[0] - ch[0]; t[1] = 2 * pv[1] - ch[1]; t[2] = 2 * pv[2] - ch[2];
        return;
    }
    case GK_DRIFTER: {
        int16_t gp[3];
        int32_t d;
        mover_pos(&gh->mv, gp);
        d = (int32_t)gp[0] * pd[0] + (int32_t)gp[1] * pd[1] + (int32_t)gp[2] * pd[2];
        /* chase when more than 7 cells away (cos 1.22 rad = 0.34) */
        if (d < 5484) { t[0] = pd[0]; t[1] = pd[1]; t[2] = pd[2]; }
        else { t[0] = gh->scatter[0]; t[1] = gh->scatter[1]; t[2] = gh->scatter[2]; }
        return;
    }
    default:
        t[0] = pd[0]; t[1] = pd[1]; t[2] = pd[2];
    }
}

static int8_t ghost_choose(void *ctx, uint16_t c, int8_t cruising)
{
    Ghost *gh = (Ghost *)ctx;
    uint16_t back = gh->mv.prev;
    uint8_t options = 0, allow_back, k;
    int8_t best = -1;
    int32_t bd = -0x7FFFFFFFL;
    int16_t target[3];
    (void)cruising;
    for (k = 0; k < 4; k++) if (OPEN(c, k) && NB(c, k) != back) options++;
    allow_back = options == 0;
    if (gh->mode == GM_EATEN) {
        int8_t bk = -1;
        int16_t bdist2 = 0x7FFF;
        for (k = 0; k < 4; k++) {
            if (!OPEN(c, k)) continue;
            if (g.nest_dist[NB(c, k)] < bdist2) { bdist2 = g.nest_dist[NB(c, k)]; bk = (int8_t)k; }
        }
        return bk;
    }
    if (gh->fright) {
        uint16_t pick = grnd(gh, options > 0 ? options : 1);
        for (k = 0; k < 4; k++) {
            if (!OPEN(c, k) || (!allow_back && NB(c, k) == back)) continue;
            if (pick-- == 0) return (int8_t)k;
        }
    }
    ghost_target(gh, target);
    for (k = 0; k < 4; k++) {
        int32_t d;
        if (!OPEN(c, k) || (!allow_back && NB(c, k) == back)) continue;
        d = dot3(target, mz_dir[NB(c, k)]);      /* closest by angle = largest dot */
        if (d > bd) { bd = d; best = (int8_t)k; }
    }
    return best;
}

static void ghost_arrived(void *ctx, uint16_t c)
{
    Ghost *gh = (Ghost *)ctx;
    if (gh->mode == GM_EATEN && c == mz_nest) {
        gh->mode = GM_NEST;                 /* home: regrow, out again shortly */
        gh->release_at = g.life + TICKS(120);
    }
}

static void ghost_tick(Ghost *gh)
{
    if (gh->mode == GM_NEST) {
        if (g.life >= gh->release_at) gh->mode = GM_ACTIVE;
        else return;
    }
    /* port: the step's distances made once a level (build_level) */
    mover_advance(&gh->mv, gh->mode == GM_EATEN ? g.d_eaten : gh->fright ? g.d_fright : g.d_ghost, ghost_choose,
                  ghost_arrived, gh);
}

/* ---- the director (GameDirector.cs) ---- */

static void set_state(uint8_t s)
{
    g.state = s;
    g.timer = 0;
    a8_log("pc: state=%u score=%ld lives=%u level=%u crumbs=%u\n", s, g.score, g.lives, g.level, g.crumbs_left);
}

static void add_score(int16_t pts)
{
    g.score += pts;
    if (!g.extra_given && g.score >= EXTRA_LIFE_AT) {
        g.extra_given = 1;
        g.lives++;
        g.sfx |= SFX_EXTRA;
    }
}

static void build_level(void)
{
    uint16_t c;
    uint8_t i, lv = (uint8_t)(g.level - 1);
    uint16_t ps;
    memcpy(g.open, mz_level[lv % NLEVELS], CELLS / 2);
    /* the nest's distances (mz_bfs) */
    {
        uint16_t head = 0, tail = 0;
        uint8_t k;
        for (c = 0; c < CELLS; c++) g.nest_dist[c] = 0x7FFF;
        g.nest_dist[mz_nest] = 0;
        bq[tail++] = (int16_t)mz_nest;
        while (head < tail) {
            c = (uint16_t)bq[head++];
            for (k = 0; k < 4; k++) {
                uint16_t nb = NB(c, k);
                if (!OPEN(c, k) || g.nest_dist[nb] != 0x7FFF) continue;
                g.nest_dist[nb] = g.nest_dist[c] + 1;
                bq[tail++] = (int16_t)nb;
            }
        }
    }
    g.crumbs_total = 0;
    memset(g.crumb, 0, sizeof g.crumb);
    memset(g.key, 0, sizeof g.key);
    for (c = 0; c < CELLS; c++) {
        if (c == mz_start || c == mz_nest) continue;
        for (i = 0; i < 4; i++) if (mz_keys[i] == c) break;
        if (i < 4) { g.key[c] = 1; continue; }
        g.crumb[c] = 1;
        g.crumbs_total++;
    }
    g.crumbs_left = g.crumbs_total;
    ps = 1485 + 77 * lv;                           /* (5.8 + 0.3 lv) * 256 */
    if (ps > 1894) ps = 1894;                      /* 7.4 */
    g.player_speed = ps;
    i = (uint8_t)(84 + 35 * lv / 10 > 97 ? 97 : 84 + 35 * lv / 10);
    g.ghost_speed = (uint16_t)((uint32_t)ps * i / 100);
    /* the distances a step (the original divides by 50 every step) */
    g.d_player = g.player_speed / 25;
    g.d_ghost = g.ghost_speed / 25;
    g.d_fright = (uint16_t)((uint32_t)g.ghost_speed * 55 / 100) / 25;
    g.d_eaten = (uint16_t)((uint32_t)g.ghost_speed * 24 / 10) / 25;
    a8_log("pc: level %u crumbs=%u\n", g.level, g.crumbs_total);
    view_level();
}

static void reset_actors(void)
{
    static const int16_t release[4] = { 13, 75, 150, 225 };     /* 0.5, 3, 6, 9 s */
    uint8_t i;
    int16_t denom = 6 + 4 * g.level;                            /* (0.6 + 0.4 L) * 10 */
    if (denom < 10) denom = 10;
    mover_place(&g.player, mz_start, 0);
    g.powered = 0;
    pend = last = -1;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        mover_place(&gh->mv, mz_nest, i);
        gh->mode = GM_NEST;
        gh->fright = 0;
        gh->release_at = (uint16_t)(release[i] * 10 / denom);
    }
    g.power_left = 0;
    g.mode_index = 0;
    g.mode_timer = 0;
    g.scatter = 1;
    g.life = 0;
    g.arrived = 1;
}

void game_start(uint8_t demo)
{
    g.demo = demo;
    g.score = 0;
    g.lives = START_LIVES;
    g.level = 1;
    g.extra_given = 0;
    build_level();
    reset_actors();
    set_state(GS_READY);
    g.sfx |= SFX_START;
}

static void start_power(void)
{
    uint8_t i;
    int16_t t = TICKS(900) - TICKS(100) * (int16_t)(g.level - 1);
    if (t < TICKS(300)) t = TICKS(300);
    g.power_total = g.power_left = t;
    g.combo = 0;
    g.powered = 1;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (gh->mode == GM_EATEN) continue;
        if (gh->mode == GM_ACTIVE) mover_reverse(&gh->mv);
        gh->fright = 1;
    }
    a8_log("pc: key power=%d\n", t);
}

static void end_power(void)
{
    uint8_t i;
    g.power_left = 0;
    g.powered = 0;
    {
        Ghost *gh = g.ghost;
        for (i = 0; i < 4; i++, gh++) if (gh->mode != GM_EATEN) gh->fright = 0;
    }
}

static void check_catches(void)
{
    int16_t pd[3], gp[3];
    uint8_t i, have = 0;
    Ghost *gh = g.ghost;
    for (i = 0; i < 4; i++, gh++) {
        int16_t dx, dy, dz;
        if (gh->mode == GM_EATEN || gh->mode == GM_NEST) continue;
        if (!near_cells(&gh->mv, &g.player)) continue;     /* port: the quick test first */
        if (!have) { mover_pos(&g.player, pd); have = 1; }
        mover_pos(&gh->mv, gp);
        dx = gp[0] - pd[0]; dy = gp[1] - pd[1]; dz = gp[2] - pd[2];
        if (dx > 20 || dx < -20 || dy > 20 || dy < -20 || dz > 20 || dz < -20) continue;
        if (dx * dx + dy * dy + dz * dz >= CATCH_D2) continue;
        if (gh->fright) {
            int16_t pts = 200 << (g.combo < 3 ? g.combo : 3);
            g.combo++;
            add_score(pts);
            gh->fright = 0;
            gh->mode = GM_EATEN;
            if (!gh->mv.moving && gh->mv.from == mz_nest) ghost_arrived(gh, mz_nest);
            g.popup_pts = pts;
            g.popup_ghost = i;
            g.popup_t = 50;
            g.sfx |= SFX_EAT;
            a8_log("pc: eat %s +%d\n", ghost_name[i], pts);
        } else {
            a8_log("pc: death by %s\n", ghost_name[i]);
            g.sfx |= SFX_DEATH;
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_DYING);
            return;
        }
    }
}

static void tick_playing(void)
{
    uint8_t i;
    g.life++;
    if (g.power_left > 0) {
        if (--g.power_left <= 0) end_power();
    } else if (++g.mode_timer >= mode_ticks[g.mode_index < 7 ? g.mode_index : 7]) {
        g.mode_timer = 0;
        g.mode_index++;
        g.scatter = (g.mode_index % 2) == 0;
        {
            Ghost *gh = g.ghost;
            for (i = 0; i < 4; i++, gh++) if (gh->mode == GM_ACTIVE) mover_reverse(&gh->mv);
        }
    }
    if (g.demo) { player_cells(); bot_dodge(); }
    player_tick();
    player_cells();
    if (g.state != GS_PLAYING) return;          /* the last crumb ends the level mid-tick */
    {
        Ghost *gh = g.ghost;
        for (i = 0; i < 4; i++, gh++) ghost_tick(gh);
    }
    check_catches();
}

void game_init(void)
{
    uint8_t i;
    memset(&g, 0, sizeof g);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        /* the corners (+-1, +-1, +-1) / sqrt 3 */
        gh->scatter[0] = (i % 2 == 0) ? 73 : -73;
        gh->scatter[1] = (i < 2) ? 73 : -73;
        gh->scatter[2] = (i == 1 || i == 2) ? 73 : -73;
        gh->kind = i;
        gh->slot = i;
        gh->rng = 1234 + i;
    }
    g.level = 1;
    build_level();
    reset_actors();
    g.state = GS_TITLE;
}

void game_step(uint8_t held, uint8_t pressed)
{
    uint8_t i;
    held_now = held;
    g.clock++;
    g.timer++;
    if (g.popup_t) g.popup_t--;

    /* buffered steering: a press is remembered until a junction allows it */
    if (pressed & IN_UP)    { pend = 0; last = 0; }
    if (pressed & IN_DOWN)  { pend = 2; last = 2; }
    if (pressed & IN_LEFT)  { pend = 3; last = 3; }
    if (pressed & IN_RIGHT) { pend = 1; last = 1; }
    if (last < 0) {                     /* port: a stick held through a reset counts */
        int8_t d;
        for (d = 0; d < 4; ++d) if (held_dir(d)) { pend = last = d; break; }
    }
    if (last >= 0 && held_dir(last)) pend = last;

    switch (g.state) {
    case GS_TITLE:
    case GS_OVER:
        if (g.state == GS_TITLE) {
            g.life++;
            {
                Ghost *gh = g.ghost;
                for (i = 0; i < 4; i++, gh++) ghost_tick(gh);
            }
        }
        break;
    case GS_READY:
        if (g.timer >= ((g.level == 1 && g.lives == START_LIVES) ? TICKS(220) : TICKS(160))) set_state(GS_PLAYING);
        break;
    case GS_PLAYING:
        tick_playing();
        break;
    case GS_DYING:
        if (g.timer >= TICKS(220)) {
            g.lives--;
            if (g.lives > 0) {
                reset_actors();
                set_state(GS_READY);
            } else
                set_state(GS_OVER);
        }
        break;
    case GS_CLEAR:
        if (g.timer >= TICKS(260)) {
            g.level++;
            build_level();
            reset_actors();
            set_state(GS_READY);
        }
        break;
    }
}

#ifndef A8_RELEASE
/* for test.py's joystick bot (pc_probe): at the cell the chomper is making
   for, the open screen directions (bits 0-3: up right down left) and those
   with a crumb next (bits 4-7) */
uint8_t pc_ways;
void test_ways(void)
{
    uint16_t c = g.player.moving ? g.player.to : g.player.from;
    uint8_t s, w = 0;
    int8_t k;
    for (s = 0; s < 4; ++s) {
        k = view_exit_for(c, s);
        if (k < 0 || !OPEN(c, k)) continue;
        w |= (uint8_t)(1 << s);
        if (g.crumb[NB(c, k)]) w |= (uint8_t)(16 << s);
    }
    pc_ways = w;
}
#endif

