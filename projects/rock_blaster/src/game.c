/*
 * Rock Blaster - game logic (port of amiga_mcp/examples/rock_blaster/game.c,
 * following the ZX Spectrum port's 16-bit sums).
 *
 * The rules, waves, scoring, speeds and the order of the update are the
 * original's. Changes are marked "port:".
 */
#include <stdint.h>
#include "a8game.h"
#include "game.h"

Fixed ship_x, ship_y, ship_dx, ship_dy;
uint8_t ship_angle, ship_alive, ship_thrust_on, ship_invuln;
int16_t ship_px, ship_py;

uint16_t bl_x[MAX_BULLETS], bl_y[MAX_BULLETS];
int16_t bl_dx[MAX_BULLETS], bl_dy[MAX_BULLETS];
int16_t bl_px[MAX_BULLETS], bl_py[MAX_BULLETS];
uint8_t bl_active[MAX_BULLETS], bl_life[MAX_BULLETS];

uint16_t rk_x[MAX_ROCKS], rk_y[MAX_ROCKS];
int16_t rk_dx[MAX_ROCKS], rk_dy[MAX_ROCKS];
int16_t rk_px[MAX_ROCKS], rk_py[MAX_ROCKS];
uint8_t rk_active[MAX_ROCKS], rk_size[MAX_ROCKS], rk_shape[MAX_ROCKS];

uint16_t pt_x[MAX_PARTICLES], pt_y[MAX_PARTICLES];
int16_t pt_dx[MAX_PARTICLES], pt_dy[MAX_PARTICLES];
uint8_t pt_active[MAX_PARTICLES], pt_life[MAX_PARTICLES];
uint8_t particles_live;

uint32_t gs_score;
int8_t gs_lives;
uint8_t gs_level, gs_state;
int16_t gs_state_timer;
int8_t gs_rock_count;
uint16_t gs_frame;

const uint8_t rock_radius_tab[3] = {20, 12, 6};

/* port: the SDK's xorshift instead of the 32-bit LCG (as the Spectrum port) */
static uint16_t rng(void)
{
    return rnd16() & 0x7FFF;
}

/* port (speed): the multiplies in 6502 (phys.s) */
static int16_t mulq8(int16_t a, int16_t b)          /* (a * b) >> 8, floor */
{
    ma = a;
    mb = b;
    mul16s();
    return (int16_t)(mres >> 8);
}

static int16_t rng_range(int16_t lo, int16_t hi)
{
    ma = (int16_t)rng();
    mb = hi - lo;
    mul16s();
    return lo + (int16_t)(mres >> 15);
}

/* port: (v * 15) / 16 with C's truncation, without 32-bit multiply/divide */
static Fixed slow15(Fixed v)
{
    if (v >= 0) return v - ((v + 15) >> 4);
    v = -v;
    return -(v - ((v + 15) >> 4));
}

static void ship_reset(void)
{
    ship_x = FX16(WORLD_W / 2);
    ship_y = FX16(WORLD_H / 2);
    ship_dx = 0;
    ship_dy = 0;
    ship_alive = 1;
    ship_invuln = SHIP_INVULN_TIME;
    ship_px = WORLD_W / 2;
    ship_py = WORLD_H / 2;
}

void game_init(void)
{
    uint8_t i;
    gs_score = 0;
    gs_lives = TUNE_START_LIVES;
    gs_level = 1;
    gs_state = STATE_PLAYING;
    gs_state_timer = 0;
    gs_rock_count = 0;
    gs_frame = 0;

    ship_reset();
    ship_angle = 192;                /* pointing up */
    ship_thrust_on = 0;

    for (i = 0; i < MAX_BULLETS; i++) bl_active[i] = 0;
    for (i = 0; i < MAX_ROCKS; i++) rk_active[i] = 0;
    for (i = 0; i < MAX_PARTICLES; i++) pt_active[i] = 0;
    particles_live = 0;

    game_spawn_rocks(TUNE_START_ROCKS);
    hits_build();
}

/* rock speeds in 1/128 px a frame: the tunable 0.80 px, x1.5 medium, x2 small */
#define ROCK_BASE ((int16_t)(TUNE_ROCK_SPEED * 128L / 100))

static void rock_launch(uint8_t i, int16_t base_speed)
{
    uint8_t a = (uint8_t)rng();
    int16_t spd = rng_range(base_speed / 2, base_speed);
    rk_dx[i] = mulq8(fix_cos(a), spd);
    rk_dy[i] = mulq8(fix_sin(a), spd);
    rk_shape[i] = (uint8_t)(rng() & 3);
    /* port: the original also drew a visual angle and spin here, which its
       drawing never used */
    rk_active[i] = 1;
    rk_px[i] = rk_x[i] >> 7;
    rk_py[i] = rk_y[i] >> 7;
}

void game_spawn_rocks(uint8_t count)
{
    uint8_t i, j;
    for (j = 0; j < count && gs_rock_count < MAX_ROCKS; j++) {
        for (i = 0; i < MAX_ROCKS; i++) {
            if (!rk_active[i]) {
                switch ((uint8_t)(rng() & 3)) {          /* spawn at an edge */
                case 0: rk_x[i] = 0;  rk_y[i] = (uint16_t)rng_range(0, (int16_t)(H7 / 2)) << 1; break;
                case 1: rk_x[i] = W7 - 1; rk_y[i] = (uint16_t)rng_range(0, (int16_t)(H7 / 2)) << 1; break;
                case 2: rk_x[i] = (uint16_t)rng_range(0, (int16_t)(W7 / 2)) << 1; rk_y[i] = 0; break;
                default: rk_x[i] = (uint16_t)rng_range(0, (int16_t)(W7 / 2)) << 1; rk_y[i] = H7 - 1; break;
                }
                /* port: W7 - 1 / H7 - 1 for the original's W / H, which its
                   wrap() moved to 0 on the next frame */
                rk_size[i] = ROCK_LARGE;
                rock_launch(i, ROCK_BASE);
                gs_rock_count++;
                break;
            }
        }
    }
}

/* two smaller rocks where a rock was destroyed */
static void split_rock(uint8_t parent)
{
    uint8_t new_size, j, i;
    int16_t base_speed = ROCK_BASE;

    if (rk_size[parent] == ROCK_SMALL) return;
    new_size = rk_size[parent] + 1;
    if (new_size == ROCK_MEDIUM) base_speed = (base_speed * 3) / 2;
    if (new_size == ROCK_SMALL) base_speed = base_speed * 2;

    for (j = 0; j < 2 && gs_rock_count < MAX_ROCKS; j++) {
        for (i = 0; i < MAX_ROCKS; i++) {
            if (!rk_active[i]) {
                rk_x[i] = rk_x[parent];
                rk_y[i] = rk_y[parent];
                rk_size[i] = new_size;
                rock_launch(i, base_speed);
                gs_rock_count++;
                break;
            }
        }
    }
}

/* port (speed): a particle's velocity is one of 64 made at start-up the
   original's way (a random direction, a random speed 0..3 px), picked at
   random: the two multiplies each took longer than a frame's worth of
   particles to move */
static int16_t pv_dx[64], pv_dy[64];

void particles_table(void)
{
    uint8_t k, a;
    int16_t spd;
    for (k = 0; k < 64; ++k) {
        a = (uint8_t)rng();
        spd = rng_range(0, V7(3));
        pv_dx[k] = mulq8(fix_cos(a), spd);
        pv_dy[k] = mulq8(fix_sin(a), spd);
    }
}

void game_spawn_particles(uint16_t x, uint16_t y, uint8_t count)
{
    uint8_t j, i = 0, k;
    for (j = 0; j < count; j++) {
        for (; i < MAX_PARTICLES; i++) {
            if (!pt_active[i]) {
                k = (uint8_t)rnd16() & 63;
                pt_x[i] = x;
                pt_y[i] = y;
                pt_dx[i] = pv_dx[k];
                pt_dy[i] = pv_dy[k];
                pt_life[i] = 10 + (uint8_t)(rng() % 20);
                pt_active[i] = 1;
                particles_live++;
                break;
            }
        }
    }
}

#define BULLET_SPD ((int16_t)(TUNE_BULLET_SPEED * 128L / 100))
#define THRUST_8   ((int16_t)((TUNE_SHIP_THRUST * 65536L / 100) >> 8))

static void fire_bullet(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bl_active[i]) {
            bl_x[i] = (uint16_t)(ship_x >> 9);
            bl_y[i] = (uint16_t)(ship_y >> 9);
            bl_dx[i] = mulq8(fix_cos(ship_angle), BULLET_SPD);
            bl_dy[i] = mulq8(fix_sin(ship_angle), BULLET_SPD);
            bl_life[i] = BULLET_LIFE;
            bl_active[i] = 1;
            bl_px[i] = bl_x[i] >> 7;
            bl_py[i] = bl_y[i] >> 7;
            break;
        }
    }
}

static void ship_controls(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire)
{
    if (!ship_alive) return;
    if (joy_left) ship_angle -= SHIP_TURN_SPEED;
    if (joy_right) ship_angle += SHIP_TURN_SPEED;

    ship_thrust_on = joy_up;
    if (joy_up) {
        /* FIX_MUL(cos, thrust) = (cos >> 8) * (thrust >> 8). port: the
           SDK's 8.8 table for cos >> 8 (rounded, the original floored) */
        ship_dx += (int16_t)(fix_cos(ship_angle) * THRUST_8);
        ship_dy += (int16_t)(fix_sin(ship_angle) * THRUST_8);
        {
            int16_t vx = (int16_t)(ship_dx >> 8), vy = (int16_t)(ship_dy >> 8);
            int32_t sq;
            ma = mb = vx;
            mul16s();
            sq = mres;
            ma = mb = vy;
            mul16s();
            if (sq + mres > (int32_t)(SHIP_MAX_SPEED >> 8) * (SHIP_MAX_SPEED >> 8)) {
                ship_dx = slow15(ship_dx);
                ship_dy = slow15(ship_dy);
            }
        }
    }

    /* phys.s: dx = (dx * 253) / 256, dy too (drag, C's truncation); x += dx,
       y += dy, wrapped (if x < 0 x += W; if x >= W x -= W); px, py = the
       whole parts */
    ship_move();

    if (ship_invuln > 0) ship_invuln--;

    if (joy_fire && (gs_frame & 7) == 0) {     /* one shot every 8 frames */
        fire_bullet();
        sfx_shoot();
    }
}

void game_update(uint8_t joy_left, uint8_t joy_right, uint8_t joy_up, uint8_t joy_fire)
{
    uint8_t i, j, sz;

    gs_frame++;

    if (gs_state == STATE_GAMEOVER) {
        /* port: the original started a new game here; this one goes on to
           the high-score table and the title (main.c) */
        if (--gs_state_timer <= 0) gs_state = STATE_TITLE;
        goto update_particles;
    }

    if (gs_state == STATE_DEAD) {
        if (--gs_state_timer <= 0) {
            ship_reset();
            gs_state = STATE_PLAYING;
        }
        goto update_rocks_and_particles;
    }

    ship_controls(joy_left, joy_right, joy_up, joy_fire);

    /* -- bullets -- */
    bullets_move();
    for (i = 0; i < MAX_BULLETS; i++)
        if (bl_active[i] && --bl_life[i] == 0) bl_active[i] = 0;

    /* -- bullet vs rock -- (port: circle tests in whole pixels, the
       difference of the whole pixels: at most 1 px off the original; the
       loop is bullet_scan in objs.s) */
    for (i = 0; (i = bullet_scan(i)) != 0xFF; ++i) {
        j = hit_rock;
        bl_active[i] = 0;
        sz = rk_size[j];
        gs_score += sz == ROCK_LARGE ? SCORE_LARGE : (sz == ROCK_MEDIUM ? SCORE_MEDIUM : SCORE_SMALL);
        game_spawn_particles(rk_x[j], rk_y[j], sz == ROCK_LARGE ? 12 : (sz == ROCK_MEDIUM ? 8 : 5));
        split_rock(j);
        rk_active[j] = 0;
        gs_rock_count--;
        if (sz == ROCK_LARGE) sfx_explode_large();
        else sfx_explode_small();
        hits_build();                   /* the rocks changed */
    }

    /* -- ship vs rock -- */
    if (ship_alive && ship_invuln == 0) {
        hit_px = ship_px;
        hit_py = ship_py;
        if (hit_first(SHIP_RADIUS) != 0xFF) {
            ship_alive = 0;
            game_spawn_particles((uint16_t)(ship_x >> 9), (uint16_t)(ship_y >> 9), 20);
            sfx_die();
            if (--gs_lives <= 0) {
                gs_state = STATE_GAMEOVER;
                gs_state_timer = 150;
            } else {
                gs_state = STATE_DEAD;
                gs_state_timer = 100;
            }
        }
    }

update_rocks_and_particles:
    rocks_move();
    if (gs_rock_count <= 0 && gs_state == STATE_PLAYING) {
        gs_level++;
        game_spawn_rocks(TUNE_START_ROCKS + (gs_level - 1) * TUNE_ROCKS_PER_LEVEL);
        hits_build();
    }

update_particles:
    /* port: a particle that leaves the world is gone (the original kept
       moving it, invisible), and v - (v >> 4) for (v * 240) / 256 */
    if (particles_live) particles_move();
}
