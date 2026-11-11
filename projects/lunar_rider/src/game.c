/*
 * Lunar Rider - game logic (port of amiga_mcp/examples/lunar_rider/game.c).
 * Scrolling, physics, collision, terrain generation, enemies, checkpoints.
 * The original's code; changes for the Atari are marked "port:". The main
 * one: the original passes a GameState pointer around; cc65 code that goes
 * through a pointer is two to three times slower than code on a global, so
 * the functions work on the global `gs` (same fields, same order of work).
 */
#include <string.h>
#include "a8game.h"
#include "game.h"

GameState gs;
uint8_t terrain[TERRAIN_LEN];

/* port: the original's rng() is a 32-bit LCG (rng_state * 1103515245 +
   12345, bits 16..30). A 32-bit multiply costs cc65 about 1,000 cycles and
   the generator calls it for every pixel of terrain, so this is the SDK's
   16-bit xorshift, with the same 0..32767 range. */
static WORD rng(void)
{
    return (WORD)(rnd16() & 0x7FFF);
}

/* port: the original's terrain_h[] (another 1K) is worked out from the
   type: a small crater is 20 below the ground, a large one 30, a hill's
   bump (0..8) is in the type's top bits, everything else is at GROUND_Y */
static WORD terrain_height(uint8_t t)
{
    if (t == TERRAIN_CRATER_SM) return GROUND_Y + 20;
    if (t == TERRAIN_CRATER_LG) return GROUND_Y + 30;
    return GROUND_Y - (t >> 4);
}

/* ---- Terrain generation ---- */

/* port: the original generates a 64-pixel chunk at a time (the first one
   640), all in one frame. That is ~40,000 cycles here, more than a frame,
   so this generates the same chunks a few pixels a frame (game_generate):
   one call is one pass of the original's loop. Features stop at the end of
   their chunk as in the original. The position is kept in 16 bits (the
   ring is 1024), the chunk as the pixels left in it, and the feature
   thresholds are worked out once per difficulty. */
uint16_t gen_x;                         /* next world x to generate (low 16 bits) */
static uint16_t chunk_left;             /* pixels left in the chunk */
static uint8_t gen_clear;               /* the start area: no features */
static uint8_t th_sm, th_lg, th_rock, th_hill, th_mine;
static const uint8_t hill_bump[24] = {  /* 8 - (w - 12)^2 / 18, >= 0 */
    0, 0, 1, 2, 3, 5, 6, 6, 7, 8, 8, 8, 8, 8, 8, 8, 7, 6, 6, 5, 3, 2, 1, 0,
};

static void gen_thresholds(void)
{
    uint8_t d = (uint8_t)gs.difficulty;
    th_sm = (uint8_t)(3 + d);
    th_lg = (uint8_t)(5 + d);
    th_rock = (uint8_t)(8 + d * 2);
    th_hill = (uint8_t)(12 + d);
    th_mine = (uint8_t)(14 + d * 2);
}

static void span(uint16_t idx, uint8_t n, uint8_t t)
{
    uint8_t w;
    for (w = 0; w < n; w++) terrain[(idx + w) & TERRAIN_MASK] = t;
}

static void generate_pixel(void)
{
    uint16_t idx = gen_x & TERRAIN_MASK;
    uint8_t r, w, n, step = 1;

    if (!chunk_left) chunk_left = 64;

    terrain[idx] = TERRAIN_FLAT;
    /* (the original sets terrain_h too: see terrain_height) */

    /* Generate features based on world position */
    r = (uint8_t)((uint16_t)rng() % 100);

    /* Increase feature density with difficulty */
    /* port: the original keeps world x 0..30 clear ("leave initial area
       clear") but the buggy stands at x 80..104, so a game could start
       over a crater (the Spectrum port's ORIGINAL_BUGS.md): keep 30 pixels
       beyond the buggy's front clear instead */
    if (gen_clear) {
        if (gen_x > BUGGY_X + BUGGY_W + 30) gen_clear = 0;     /* leave initial area clear */
    }
    if (!gen_clear) {
        if (r < th_sm) {
            /* Small crater */
            n = chunk_left < 16 ? (uint8_t)chunk_left : 16;
            span(idx, n, TERRAIN_CRATER_SM);       /* gap in ground: GROUND_Y + 20 */
            step = 16;
        } else if (r < th_lg) {
            /* Large crater */
            n = chunk_left < 28 ? (uint8_t)chunk_left : 28;
            span(idx, n, TERRAIN_CRATER_LG);       /* GROUND_Y + 30 */
            step = 28;
        } else if (r < th_rock) {
            /* Rock/boulder */
            terrain[idx] = TERRAIN_ROCK;
        } else if (r < th_hill) {
            /* Hill (cosmetic bump): parabolic, max 8 pixels high */
            n = chunk_left < 24 ? (uint8_t)chunk_left : 24;
            for (w = 0; w < n; w++)                 /* port: height base_h - bump */
                terrain[(idx + w) & TERRAIN_MASK] = (uint8_t)(TERRAIN_HILL | (hill_bump[w] << 4));
            step = 24;
        } else if (r < th_mine) {
            /* Ground mine */
            terrain[idx] = TERRAIN_MINE;
        }
    }
    if (step > chunk_left) step = (uint8_t)chunk_left;
    chunk_left -= step;
    gen_x += step;
}

/* port: called every frame: keeps the terrain generated to where the
   original's would be (it generates a chunk when scroll + 420 passes the
   generated end, so up to 64 pixels beyond that) */
void game_generate(GameState *g)
{
    uint8_t n = 4;
    uint16_t want = (uint16_t)g->scroll_px + (SCREEN_W + 100 + 64);
    while (n-- && (int16_t)(want - gen_x) > 0) generate_pixel();
}

/* ---- the entities (struct of arrays, see game.h) ---- */
WORD fb_x[MAX_FWD_BULLETS], fb_y[MAX_FWD_BULLETS];
uint8_t fb_on[MAX_FWD_BULLETS];
WORD ub_x[MAX_UP_BULLETS], ub_y[MAX_UP_BULLETS];
uint8_t ub_on[MAX_UP_BULLETS];
WORD en_x[MAX_ENEMIES], en_y[MAX_ENEMIES];
int8_t en_dx[MAX_ENEMIES], en_dy[MAX_ENEMIES];
uint8_t en_type[MAX_ENEMIES], en_on[MAX_ENEMIES], en_bomb[MAX_ENEMIES], en_flash[MAX_ENEMIES];
int8_t en_hp[MAX_ENEMIES];
WORD bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
uint8_t bm_on[MAX_BOMBS];
WORD ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
uint8_t ex_r[MAX_EXPLOSIONS], ex_on[MAX_EXPLOSIONS];
int8_t ex_life[MAX_EXPLOSIONS];
uint8_t n_fb, n_ub, n_en, n_bm, n_ex;

/* ---- Enemy spawning ---- */

static void try_spawn_enemy(void)
{
    uint8_t i;
    WORD spawn_chance = 2 + gs.difficulty * 2;
    WORD r = rng() % 100;

    if (r >= spawn_chance) return;

    /* Find free slot */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!en_on[i]) {
            WORD type_r = rng() % 100;

            en_on[i] = 1;
            ++n_en;
            en_flash[i] = 0;
            en_bomb[i] = (uint8_t)(60 + (rng() % 60));

            if (type_r < 40) {
                /* Small UFO */
                en_type[i] = ENEMY_UFO_SM;
                en_x[i] = SCREEN_W + 10;
                en_y[i] = 30 + (rng() % 40);
                en_dx[i] = (int8_t)(-2 - (rng() % 2));
                en_dy[i] = 0;
                en_hp[i] = 1;
            } else if (type_r < 65) {
                /* Large UFO */
                en_type[i] = ENEMY_UFO_LG;
                en_x[i] = SCREEN_W + 10;
                en_y[i] = 20 + (rng() % 30);
                en_dx[i] = -1;
                en_dy[i] = 0;
                en_hp[i] = 2;
            } else {
                /* Meteor */
                en_type[i] = ENEMY_METEOR;
                en_x[i] = SCREEN_W + (rng() % 60);
                en_y[i] = -10;
                en_dx[i] = (int8_t)(-(1 + (rng() % 2)));
                en_dy[i] = (int8_t)(2 + (rng() % 2));
                en_hp[i] = 1;
            }
            break;
        }
    }
}

/* ---- Initialization ---- */

void game_init(GameState *g)
{
    memset(g, 0, sizeof(GameState));
    memset(fb_on, 0, sizeof fb_on);
    memset(ub_on, 0, sizeof ub_on);
    memset(en_on, 0, sizeof en_on);
    memset(bm_on, 0, sizeof bm_on);
    memset(ex_on, 0, sizeof ex_on);
    n_fb = n_ub = n_en = n_bm = n_ex = 0;

    gs.lives = 3;
    gs.score = 0;
    gs.state = STATE_TITLE;
    gs.frame = 0;
    gs.difficulty = 0;

    gs.scroll_px = 0;
    gs.scroll_sub = 0;
    gs.scroll_speed = SPEED_NORMAL;
    gen_x = 0;
    gen_clear = 1;

    /* Init buggy */
    gs.buggy.x = BUGGY_X;
    gs.buggy.y = BUGGY_GROUND_Y;
    gs.buggy.vy = 0;
    gs.buggy.vk = 15;
    gs.buggy.on_ground = 1;
    gs.buggy.alive = 1;
    gs.buggy.wheel_frame = 0;

    /* Init terrain - flat for a while then obstacles */
    memset(terrain, TERRAIN_FLAT, sizeof(terrain));

    gs.checkpoint_cur = 0;
    gs.checkpoint_dist = CHECKPOINT_DIST;

    /* Generate initial terrain: the original's first chunk is 640 pixels */
    gen_thresholds();
    chunk_left = 640;
    while (gen_x < 640) generate_pixel();
}

/* ---- Spawn explosion ---- */

/* port: tell the renderer a terrain column changed after it was drawn */
static void mark_dirty(uint32_t world_x)
{
    if (gs.n_dirty < 4) gs.dirty[gs.n_dirty++] = world_x;
}

static void spawn_explosion(WORD x, WORD y)
{
    uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_on[i]) {
            ex_x[i] = x;
            ex_y[i] = y;
            ex_r[i] = 2;
            ex_life[i] = 12;
            ex_on[i] = 1;
            ++n_ex;
            break;
        }
    }
}

static void kill_buggy(uint8_t cause)
{
    gs.death_cause = cause;
    gs.buggy.alive = 0;
    gs.state = STATE_DYING;
    gs.state_timer = 40;
    gs.ev_death = 1;
}

/* ---- Update functions ---- */

/* port: vy / 100 is a signed division every frame (~700 cycles in cc65).
   vy is always JUMP_VEL + k * GRAVITY (a jump starts it at JUMP_VEL, the
   ground sets it to 0 = k 15, gravity adds 40), so the buggy keeps k too
   (Buggy.vk) and this is vy / 100 for each k, truncated as C does. */
static const int8_t vy_div100[128] = {-6, -5, -5, -4, -4, -4, -3, -3, -2, -2, -2, -1, -1, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 12, 13, 13, 14, 14, 14, 15, 15, 16, 16, 16, 17, 17, 18, 18, 18, 19, 19, 20, 20, 20, 21, 21, 22, 22, 22, 23, 23, 24, 24, 24, 25, 25, 26, 26, 26, 27, 27, 28, 28, 28, 29, 29, 30, 30, 30, 31, 31, 32, 32, 32, 33, 33, 34, 34, 34, 35, 35, 36, 36, 36, 37, 37, 38, 38, 38, 39, 39, 40, 40, 40, 41, 41, 42, 42, 42, 43, 43, 44, 44, 44};

static void update_buggy(InputState *inp)
{
    static WORD by, vy;                /* (static: cc65 reaches them directly) */
    static uint16_t terrain_idx, sx;
    static WORD ground_h;
    static uint8_t on_ground;

    if (!gs.buggy.alive) return;
    sx = (uint16_t)gs.scroll_px;

    /* Speed control */
    if (inp->right) {
        gs.scroll_speed = SPEED_FAST;
    } else if (inp->left) {
        gs.scroll_speed = SPEED_SLOW;
    } else {
        gs.scroll_speed = SPEED_NORMAL;
    }

    by = gs.buggy.y;
    vy = gs.buggy.vy;
    on_ground = gs.buggy.on_ground;

    /* Jump */
    if ((inp->jump) && on_ground) {
        vy = JUMP_VEL;
        gs.buggy.vk = 0;
        on_ground = 0;
        gs.ev_jump = 1;
    }

    /* Gravity */
    if (!on_ground) {
        vy += GRAVITY;
        if (gs.buggy.vk < 127) ++gs.buggy.vk;
    }

    /* Apply vertical velocity (fixed point /100) */
    by += vy_div100[gs.buggy.vk];          /* vy / 100 */

    /* Get ground height at buggy position */
    terrain_idx = (sx + BUGGY_X + BUGGY_W / 2) & TERRAIN_MASK;
    ground_h = terrain_height(terrain[terrain_idx]) - BUGGY_H;

    /* Check ground collision */
    if (by >= ground_h) {
        uint8_t t_type = terrain[terrain_idx];

        if (t_type == TERRAIN_CRATER_SM || t_type == TERRAIN_CRATER_LG) {
            /* Falling into crater = death */
            if (on_ground || vy > 0) {
                /* Only die if not jumping over */
                if (by >= GROUND_Y - BUGGY_H + 5) {
                    gs.buggy.y = by;
                    gs.buggy.vy = vy;
                    gs.buggy.on_ground = on_ground;
                    kill_buggy(DEATH_CRATER);
                    spawn_explosion(BUGGY_X + BUGGY_W / 2, by + BUGGY_H / 2);
                    return;
                }
            }
        } else {
            by = ground_h;
            vy = 0;
            gs.buggy.vk = 15;
            on_ground = 1;
        }
    } else {
        on_ground = 0;
    }
    gs.buggy.y = by;
    gs.buggy.vy = vy;
    gs.buggy.on_ground = on_ground;

    /* Check rock collision */
    {
        uint16_t rock_idx = (sx + BUGGY_X + BUGGY_W) & TERRAIN_MASK;
        if (terrain[rock_idx] == TERRAIN_ROCK && on_ground) {
            /* Hit a rock! */
            kill_buggy(DEATH_ROCK);
            spawn_explosion(BUGGY_X + BUGGY_W / 2, by + BUGGY_H / 2);
            return;
        }
    }

    /* Check mine collision */
    if (terrain[terrain_idx] == TERRAIN_MINE && on_ground) {
        kill_buggy(DEATH_MINE);
        gs.ev_explode = 1;
        spawn_explosion(BUGGY_X + BUGGY_W / 2, GROUND_Y);
        /* Remove the mine */
        terrain[terrain_idx] = TERRAIN_FLAT;
        mark_dirty(gs.scroll_px + BUGGY_X + BUGGY_W / 2);
        return;
    }

    /* Wheel animation */
    gs.buggy.wheel_frame = (uint8_t)((sx >> 1) & 1);

    /* Fire bullets */
    if (inp->fire) {
        uint8_t i;
        uint8_t fired = 0;

        /* Forward bullet */
        for (i = 0; i < MAX_FWD_BULLETS; i++) {
            if (!fb_on[i]) {
                fb_x[i] = BUGGY_X + BUGGY_W;
                fb_y[i] = by + 4;
                fb_on[i] = 1;
                ++n_fb;
                fired = 1;
                break;
            }
        }

        /* Upward bullet */
        for (i = 0; i < MAX_UP_BULLETS; i++) {
            if (!ub_on[i]) {
                ub_x[i] = BUGGY_X + BUGGY_W / 2;
                ub_y[i] = by;
                ub_on[i] = 1;
                ++n_ub;
                fired = 1;
                break;
            }
        }

        if (fired) {
            gs.ev_shoot = 1;
        }
    }
}

static void update_bullets(void)
{
    static uint8_t i;                  /* (statics: cc65 reaches them directly) */
    static WORD x;

    if (n_fb) for (i = 0; i < MAX_FWD_BULLETS; i++) {
        if (!fb_on[i]) continue;
        x = fb_x[i] += 5;
        if (x > SCREEN_W + 10) {
            fb_on[i] = 0;
            --n_fb;
            continue;
        }
        /* Check if bullet hits a rock */
        {
            uint16_t tidx = ((uint16_t)gs.scroll_px + x) & TERRAIN_MASK;
            if (terrain[tidx] == TERRAIN_ROCK && fb_y[i] >= GROUND_Y - 12) {
                fb_on[i] = 0;
                --n_fb;
                terrain[tidx] = TERRAIN_FLAT;
                mark_dirty(gs.scroll_px + x);
                gs.score += 100;
                gs.ev_explode = 1;
                spawn_explosion(x, GROUND_Y - 6);
            }
        }
    }

    if (n_ub) for (i = 0; i < MAX_UP_BULLETS; i++) {
        if (!ub_on[i]) continue;
        if ((ub_y[i] -= 5) < -10) {
            ub_on[i] = 0;
            --n_ub;
        }
    }
}

static void update_enemies(void)
{
    static uint8_t i;
    static WORD x, y;

    if (n_en) for (i = 0; i < MAX_ENEMIES; i++) {
        if (!en_on[i]) continue;

        x = en_x[i] += en_dx[i];
        y = en_y[i] += en_dy[i];
        en_flash[i]++;

        /* Off screen? */
        if (x < -30 || y > SCREEN_H + 10) {
            en_on[i] = 0;
            --n_en;
            continue;
        }

        /* UFO bomb dropping */
        if (en_type[i] != ENEMY_METEOR && en_bomb[i] > 0) {
            if (--en_bomb[i] == 0) {
                uint8_t j;
                for (j = 0; j < MAX_BOMBS; j++) {
                    if (!bm_on[j]) {
                        bm_x[j] = x + 4;
                        bm_y[j] = y + 10;
                        bm_on[j] = 1;
                        ++n_bm;
                        break;
                    }
                }
                /* Large UFO drops multiple bombs */
                if (en_type[i] == ENEMY_UFO_LG) {
                    en_bomb[i] = (uint8_t)(40 + (rng() % 30));
                } else {
                    en_bomb[i] = 0; /* single bomb */
                }
            }
        }

        /* Meteor hits ground */
        if (en_type[i] == ENEMY_METEOR && y >= GROUND_Y - 4) {
            en_on[i] = 0;
            --n_en;
            gs.ev_explode = 1;
            spawn_explosion(x, GROUND_Y - 4);
        }
    }

    /* Try spawning new enemies periodically */
    if (gs.frame60 == 0) {
        try_spawn_enemy();
    }
}

static void update_bombs(void)
{
    static uint8_t i;
    static WORD bx, by, x, y;

    bx = gs.buggy.x;
    by = gs.buggy.y;
    if (n_bm) for (i = 0; i < MAX_BOMBS; i++) {
        if (!bm_on[i]) continue;

        x = bm_x[i];
        y = bm_y[i] += 2;

        /* Hit ground */
        if (y >= GROUND_Y) {
            bm_on[i] = 0;
            --n_bm;
            gs.ev_explode = 1;
            spawn_explosion(x, GROUND_Y);

            /* Check proximity to buggy */
            if (gs.buggy.alive) {
                WORD dx = x - (bx + BUGGY_W / 2);
                if (dx < 0) dx = -dx;
                if (dx < 20) {
                    kill_buggy(DEATH_BOMB);
                    spawn_explosion(bx + BUGGY_W / 2, by + BUGGY_H / 2);
                }
            }
            continue;
        }

        /* Direct hit on buggy */
        if (gs.buggy.alive) {
            if (x >= bx && x <= bx + BUGGY_W && y >= by && y <= by + BUGGY_H) {
                bm_on[i] = 0;
                --n_bm;
                kill_buggy(DEATH_BOMB);
                gs.ev_explode = 1;
                spawn_explosion(bx + BUGGY_W / 2, by + BUGGY_H / 2);
            }
        }
    }
}

/* one bullet (x, y) against the enemies; 1 if it hit one. The original has
   this loop twice, for the forward and the upward bullets. */
static WORD bx_, by_;                   /* the bullet bullet_vs_enemies() tests */

static uint8_t bullet_vs_enemies(void)
{
    static uint8_t j;
    static WORD ex, ey, ew, eh;
    WORD x = bx_, y = by_;
    for (j = 0; j < MAX_ENEMIES; j++) {
        if (!en_on[j]) continue;

        ex = en_x[j];
        ey = en_y[j];
        if (en_type[j] == ENEMY_UFO_LG) { ew = 20; eh = 14; }
        else { ew = 12; eh = 10; }

        if (x >= ex && x <= ex + ew && y >= ey && y <= ey + eh) {
            if (--en_hp[j] <= 0) {
                en_on[j] = 0;
                --n_en;
                gs.ev_explode = 1;
                spawn_explosion(ex + ew / 2, ey + eh / 2);

                switch (en_type[j]) {
                    case ENEMY_UFO_SM: gs.score += 200; break;
                    case ENEMY_UFO_LG: gs.score += 500; break;
                    case ENEMY_METEOR: gs.score += 300; break;
                }
            }
            return 1;
        }
    }
    return 0;
}

static void check_bullet_enemy_collisions(void)
{
    uint8_t i;
    if (!n_en) return;
    /* Forward bullets vs enemies */
    if (n_fb) for (i = 0; i < MAX_FWD_BULLETS; i++)
        if (fb_on[i]) { bx_ = fb_x[i]; by_ = fb_y[i]; if (bullet_vs_enemies()) { fb_on[i] = 0; --n_fb; } }
    /* Upward bullets vs enemies */
    if (n_ub) for (i = 0; i < MAX_UP_BULLETS; i++)
        if (ub_on[i]) { bx_ = ub_x[i]; by_ = ub_y[i]; if (bullet_vs_enemies()) { ub_on[i] = 0; --n_ub; } }
}

static void check_enemy_buggy_collision(void)
{
    uint8_t i;
    WORD bx = gs.buggy.x, by = gs.buggy.y;

    if (!gs.buggy.alive || !n_en) return;

    for (i = 0; i < MAX_ENEMIES; i++) {
        WORD ex, ey, ew;
        if (!en_on[i]) continue;

        ex = en_x[i];
        ey = en_y[i];
        ew = (en_type[i] == ENEMY_UFO_LG) ? 20 : 12;

        /* Check overlap */
        if (bx + BUGGY_W > ex && bx < ex + ew &&
            by + BUGGY_H > ey && by < ey + 10) {
            kill_buggy(DEATH_ENEMY);
            en_on[i] = 0;
            --n_en;
            gs.ev_explode = 1;
            spawn_explosion(bx + BUGGY_W / 2, by + BUGGY_H / 2);
            spawn_explosion(ex + ew / 2, ey + 5);
            break;
        }
    }
}

static void update_explosions(void)
{
    static uint8_t i;
    if (n_ex) for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_on[i]) continue;
        if (--ex_life[i] > 6) {
            ex_r[i] += 2;
        }
        if (ex_life[i] <= 0) {
            ex_on[i] = 0;
            --n_ex;
        }
    }
}

static void update_scrolling(void)
{
    uint16_t s;
    uint8_t whole_px = 0;

    /* port: scroll_x += scroll_speed in 1/100 px; the original's separate
       scroll_frac for the checkpoints counts the same whole pixels */
    s = gs.scroll_sub + (uint16_t)gs.scroll_speed;
    while (s >= 100) {
        s -= 100;
        whole_px++;
    }
    gs.scroll_sub = (uint8_t)s;
    gs.scroll_px += whole_px;

    /* Generate new terrain as needed */
    game_generate(&gs);

    /* Checkpoint tracking */
    gs.checkpoint_dist -= whole_px;
    if (gs.checkpoint_dist <= 0) {
        gs.checkpoint_cur++;
        if (gs.checkpoint_cur >= 26) {
            /* Completed course - loop with higher difficulty */
            gs.checkpoint_cur = 0;
            gs.difficulty++;
            gen_thresholds();
        }
        gs.checkpoint_dist = CHECKPOINT_DIST;
        gs.score += 1000;
        gs.ev_checkpoint = 1;
        gs.state = STATE_CHECKPOINT;
        gs.state_timer = 60;
    }
}

/* ---- Main update ---- */

void game_update(GameState *g, InputState *inp)
{
    (void)g;
    gs.frame++;
    if (++gs.frame60 == 60) gs.frame60 = 0;

    /* Clear event flags */
    gs.ev_shoot = 0;
    gs.ev_explode = 0;
    gs.ev_jump = 0;
    gs.ev_checkpoint = 0;
    gs.ev_death = 0;

    switch (gs.state) {
        case STATE_PLAYING:
            update_scrolling();
            update_buggy(inp);
            update_bullets();
            update_enemies();
            update_bombs();
            check_bullet_enemy_collisions();
            check_enemy_buggy_collision();
            update_explosions();
            break;

        case STATE_DYING:
            update_explosions();
            gs.state_timer--;
            if (gs.state_timer <= 0) {
                gs.lives--;
                if (gs.lives <= 0) {
                    gs.state = STATE_GAMEOVER;
                    gs.state_timer = 180;
                } else {
                    /* Respawn buggy */
                    gs.buggy.alive = 1;
                    gs.buggy.y = BUGGY_GROUND_Y;
                    gs.buggy.vy = 0;
                    gs.buggy.vk = 15;
                    gs.buggy.on_ground = 1;
                    gs.scroll_speed = SPEED_NORMAL;
                    gs.state = STATE_PLAYING;
                }
            }
            break;

        case STATE_GAMEOVER:
            gs.state_timer--;
            /* Stay in gameover until player presses fire or timer runs out */
            if (gs.state_timer <= 0) {
                gs.state = STATE_TITLE;
            }
            break;

        case STATE_CHECKPOINT:
            /* Brief celebration, then back to playing */
            update_scrolling();
            update_buggy(inp);
            update_bullets();
            update_explosions();
            gs.state_timer--;
            if (gs.state_timer <= 0) {
                gs.state = STATE_PLAYING;
            }
            break;

        case STATE_TITLE:
            /* Handled in main loop */
            break;
    }
}
