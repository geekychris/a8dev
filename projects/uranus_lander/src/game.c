/*
 * Uranus Lander - game logic (port of amiga_mcp/examples/uranus_lander/game.c).
 *
 * The rules, terrain generator (its random numbers and seeds, so every
 * level's ground and pads are the Amiga's), pads, physics, landing test and
 * scoring are the original's, in its 320x256 world with 16.16 fixed point.
 * Port changes are marked "port:".
 */
#include <string.h>
#include "game.h"

GameState gs;

Tunables g_tune = {
    GRAVITY_DEFAULT,
    THRUST_DEFAULT,
    FUEL_DEFAULT,
    SAFE_VY_DEFAULT,
    SAFE_VX_DEFAULT,
    SAFE_ANGLE_DEFAULT,
    TURN_RATE_DEFAULT,
    START_LIVES_DEFAULT
};

/* port: the particles and stars as arrays of fields (game.h) */
Fixed pt_x[MAX_PARTICLES], pt_y[MAX_PARTICLES], pt_vx[MAX_PARTICLES], pt_vy[MAX_PARTICLES];
uint8_t pt_life[MAX_PARTICLES], pt_on[MAX_PARTICLES];
uint8_t pt_live;
WORD st_x[MAX_STARS];
uint8_t st_y[MAX_STARS], st_speed[MAX_STARS], st_accum[MAX_STARS];

/* port: the original's thrust for each angle, (sin_tab[a] * thrust) >> 16
   and (-cos_tab[a] * thrust) >> 16 from its own 16.16 sine table
   (tables.c, tools/gen_tables.py): the same values, no 32-bit multiply */
extern const int16_t thrust_x[256], thrust_y[256];
extern uint8_t ship_thrust;
extern void ship_step(void);       /* physics.s */

#define HI16(v) (*(int16_t *)((uint8_t *)&(v) + 2))      /* v >> 16 (floor) */

/* RNG: the original's LCG, so every level's terrain is the original's.
   port: in 6502 (rng.s), the same numbers: cc65's 32-bit multiply made a
   level take a second to generate and a crash's sparks a third of one */
extern ULONG rng_state;
extern uint16_t rng_next(void);

/* Generate stars with parallax speeds.
   port: their places are the original's generator from seed 777777, worked
   out beforehand (tables.c): 32-bit divisions cost a third of a second */
extern const int16_t star_x0[MAX_STARS];
extern const uint8_t star_y0[MAX_STARS];

void stars_init(void)
{
    uint8_t i;
    for (i = 0; i < MAX_STARS; i++) {
        st_x[i] = star_x0[i];
        st_y[i] = star_y0[i];
        st_accum[i] = 0;
        /* bright = fastest parallax layer, dim = slowest (the original's
           brightness is the layer: view.c draws all of them the same) */
        st_speed[i] = i < 15 ? 3 : i < 35 ? 2 : 1;
    }
}

/* Width based on multiplier */
static WORD pad_width(WORD mult)
{
    switch (mult) {
        case 1: return 40;
        case 2: return 30;
        case 3: return 22;
        default: return 14; /* x5 */
    }
}

/* Multipliers get harder as level increases */
static const uint8_t mult_table[4][4] = {
    {1, 1, 2, 2},   /* level 1-2 */
    {1, 2, 2, 3},   /* level 3-4 */
    {2, 2, 3, 5},   /* level 5-6 */
    {2, 3, 5, 5},   /* level 7+ */
};

/* Generate terrain for current level */
static void generate_terrain(void)
{
    static WORD x, i, y, roughness, *t;    /* (statics: cc65 reaches them directly) */
    static uint16_t span;

    y = 200;
    roughness = 2 + gs.level;
    if (roughness > 8) roughness = 8;
    span = (uint16_t)(roughness * 2 + 1);

    /* Seed RNG based on level (the 68k's 32-bit int) */
    rng_state = (ULONG)((LONG)gs.level * 31337L + 54321L);

    /* Generate random terrain */
    t = gs.terrain_y;
    for (x = 0; x < TERRAIN_W; x++) {
        y += (WORD)(rng_next() % span) - roughness;
        if (y < TERRAIN_MIN) y = TERRAIN_MIN + 5;
        if (y > TERRAIN_MAX) y = TERRAIN_MAX - 5;
        *t++ = y;
    }

    /* Place landing pads */
    gs.num_pads = 0;
    {
        WORD num_zones = 4;
        WORD zone_w = TERRAIN_W / num_zones;
        WORD diff = (gs.level - 1) / 2;
        LandingPad *pd = gs.pads;
        if (diff > 3) diff = 3;

        for (i = 0; i < num_zones && gs.num_pads < MAX_PADS; i++, pd++) {
            WORD mult = mult_table[diff][i];
            WORD width = pad_width(mult);
            WORD zone_start = i * zone_w + 10;
            WORD pad_x, pad_y, px;

            /* Pick X position within zone */
            pad_x = zone_start + (WORD)(rng_next() % (uint16_t)(zone_w - width - 20));
            if (pad_x < 5) pad_x = 5;
            if (pad_x + width >= TERRAIN_W - 5) pad_x = TERRAIN_W - width - 5;

            /* Use average height at pad location */
            pad_y = gs.terrain_y[pad_x + width / 2];
            if (pad_y < TERRAIN_MIN + 10) pad_y = TERRAIN_MIN + 10;
            if (pad_y > TERRAIN_MAX - 5) pad_y = TERRAIN_MAX - 5;

            /* Flatten terrain at pad */
            for (px = pad_x; px < pad_x + width && px < TERRAIN_W; px++)
                gs.terrain_y[px] = pad_y;

            /* Smooth transitions at pad edges */
            if (pad_x > 2) {
                WORD diff_l = gs.terrain_y[pad_x - 1] - pad_y;
                if (diff_l > 3 || diff_l < -3)
                    gs.terrain_y[pad_x - 1] = pad_y + diff_l / 2;
            }
            if (pad_x + width < TERRAIN_W - 2) {
                WORD diff_r = gs.terrain_y[pad_x + width] - pad_y;
                if (diff_r > 3 || diff_r < -3)
                    gs.terrain_y[pad_x + width] = pad_y + diff_r / 2;
            }

            pd->x = pad_x;
            pd->width = width;
            pd->y = pad_y;
            pd->multiplier = mult;
            gs.num_pads++;
        }
    }
}

/* Reset ship to starting position */
static void reset_ship(void)
{
    gs.ship.x = FIX16(SCREEN_W / 2);
    gs.ship.y = FIX16(20);
    gs.ship.vx = 0;
    gs.ship.vy = 0;
    gs.ship.angle = 0;
    gs.ship.fuel = (WORD)g_tune.fuel_max;
    gs.ship.alive = 1;
    gs.ship.thrusting = 0;
}

/* port: n % 16000 for n 0..32767 (the generator's range): two subtractions
   at most instead of a division */
static uint16_t mod16000(uint16_t n)
{
    while (n >= 16000) n -= 16000;
    return n;
}

/* Spawn explosion particles */
static void spawn_explosion(Fixed x, Fixed y)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        WORD r;
        if (pt_on[i]) continue;
        pt_x[i] = x;
        pt_y[i] = y;
        r = (WORD)mod16000(rng_next()) - 8000;
        pt_vx[i] = (Fixed)r << 2;
        r = (WORD)mod16000(rng_next()) - 12000;
        pt_vy[i] = (Fixed)r << 2;
        pt_life[i] = (uint8_t)(20 + (rng_next() % 30u));
        /* the colour (red 1 in 4, else orange or yellow): view.c draws the
           sparks in one colour, but the calls keep the generator in step */
        if ((rng_next() & 3) != 0) rng_next();
        pt_on[i] = 1;
        pt_live++;
    }
}

/* port: x += vx, y += vy, vy += 600 (particle gravity), life--, off at 0:
   fx.s (in C, 32 particles of 32-bit sums are a frame) */
extern void particles_update(void);

static void update_particles(void)
{
    if (pt_live) particles_update();
}

void game_init(void)
{
    memset(&gs, 0, sizeof(GameState));
    memset(pt_on, 0, sizeof pt_on);
    pt_live = 0;

    /* Restore level-dependent physics defaults so a fresh game does not
     * inherit the harder tune left over from previous game_new_level runs. */
    g_tune.gravity  = GRAVITY_DEFAULT;
    g_tune.fuel_max = FUEL_DEFAULT;

    gs.lives = (WORD)g_tune.start_lives;
    gs.level = 1;
    gs.score = 0;
    gs.state = STATE_PLAYING;

    stars_init();
    generate_terrain();
    reset_ship();
}

void game_new_level(void)
{
    gs.level++;

    /* Increase difficulty */
    g_tune.gravity = GRAVITY_DEFAULT + (gs.level - 1) * 150L;
    g_tune.fuel_max = FUEL_DEFAULT - (gs.level - 1) * 30L;
    if (g_tune.fuel_max < 200) g_tune.fuel_max = 200;

    memset(pt_on, 0, sizeof pt_on);
    pt_live = 0;

    generate_terrain();
    reset_ship();
}

static void crash(WORD timer)
{
    gs.ship.alive = 0;
    gs.state = STATE_CRASHING;
    gs.state_timer = timer;
    gs.ev_crash = 1;
}

/* Check landing on terrain/pad */
static void check_collision(void)
{
    static WORD sx, sy, ship_bottom, terrain_h, angle;
    static uint8_t i;
    static int8_t pad_idx;
    static Fixed abs_vx, abs_vy;
    static LandingPad *pd;

    sx = FIX_ROUND(gs.ship.x);
    sy = FIX_ROUND(gs.ship.y);
    ship_bottom = sy + 6; /* ship extends ~6px below center */

    if (sx < 0 || sx >= TERRAIN_W) return;

    terrain_h = gs.terrain_y[sx];

    if (ship_bottom < terrain_h) return; /* still airborne */

    /* Hit the ground - check if on a pad */
    pad_idx = -1;
    pd = gs.pads;
    for (i = 0; i < (uint8_t)gs.num_pads; i++, pd++) {
        if (sx >= pd->x && sx < pd->x + pd->width) {
            pad_idx = (int8_t)i;
            break;
        }
    }

    if (pad_idx < 0) {
        /* Hit rough terrain - crash! */
        crash(80);
        spawn_explosion(gs.ship.x, gs.ship.y);
        return;
    }

    /* On a pad - check landing conditions */
    abs_vx = gs.ship.vx < 0 ? -gs.ship.vx : gs.ship.vx;
    abs_vy = gs.ship.vy < 0 ? -gs.ship.vy : gs.ship.vy;
    angle = gs.ship.angle;
    if (angle > 128) angle = 256 - angle;

    if (gs.ship.vy < 0 ||
        abs_vy > g_tune.safe_vy ||
        abs_vx > g_tune.safe_vx ||
        angle > g_tune.safe_angle) {
        /* Bad landing - crash! */
        crash(80);
        spawn_explosion(gs.ship.x, gs.ship.y);
        return;
    }

    /* Successful landing! */
    gs.landed_pad = pad_idx;
    gs.land_bonus = (LONG)pd->multiplier * 50L + (LONG)gs.ship.fuel;
    gs.score += gs.land_bonus;
    gs.ship.vx = 0;
    gs.ship.vy = 0;
    gs.ship.thrusting = 0;
    gs.state = STATE_LANDED;
    gs.state_timer = 100;
    gs.ev_land = 1;
}

void game_update(InputState *inp)
{
    gs.ev_thrust = 0;
    gs.ev_crash = 0;
    gs.ev_land = 0;
    gs.ev_low_fuel = 0;

    switch (gs.state) {
    case STATE_PLAYING:
    {
        /* (gs.ship's fields directly: cc65 is much slower through a pointer) */

        /* Rotation */
        if (inp->left)  gs.ship.angle = (gs.ship.angle - (WORD)g_tune.turn_rate) & 255;
        if (inp->right) gs.ship.angle = (gs.ship.angle + (WORD)g_tune.turn_rate) & 255;

        /* Thrust */
        gs.ship.thrusting = 0;
        ship_thrust = 0;
        if ((inp->thrust || inp->up) && gs.ship.fuel > 0) {
            ship_thrust = 1;                /* vx += thrust_x[angle], vy += thrust_y[angle] */
            gs.ship.fuel--;
            gs.ship.thrusting = 1;
            gs.ev_thrust = 1;
        }

        /* Gravity (always pulls down = positive Y), then the move:
           vy += gravity; x += vx; y += vy (port: physics.s, the same 16.16
           sums; the thrust is the original's table, see above) */
        ship_step();

        /* Clamp X to screen bounds (port: the tests on the integer parts,
           read as bytes: the same results, without cc65's 32-bit compares) */
        if (HI16(gs.ship.x) < 5) { gs.ship.x = FIX16(5); gs.ship.vx = 0; }
        if (HI16(gs.ship.x) >= SCREEN_W - 5 && gs.ship.x > FIX16(SCREEN_W - 5)) {
            gs.ship.x = FIX16(SCREEN_W - 5);
            gs.ship.vx = 0;
        }

        /* If ship goes too high, let it (but clamp for sanity) */
        if (HI16(gs.ship.y) < -40) gs.ship.y = FIX16(-40);

        /* If ship goes below screen, crash: round(y) > 256 */
        if (HI16(gs.ship.y) >= SCREEN_H && FIX_ROUND(gs.ship.y) > SCREEN_H)
            crash(60);

        if (gs.ship.alive)
            check_collision();

        /* Low fuel warning */
        if (gs.ship.fuel > 0 && gs.ship.fuel < 80 && (gs.frame & 31) == 0)
            gs.ev_low_fuel = 1;

        /* Count down the post-respawn invulnerability grace period so
         * draw_ship stops blinking after ~50 ticks. */
        if (gs.state_timer > 0)
            gs.state_timer--;
        break;
    }

    case STATE_LANDED:
        gs.state_timer--;
        if (gs.state_timer <= 0) {
            game_new_level();
            gs.state = STATE_PLAYING;
        }
        break;

    case STATE_CRASHING:
        update_particles();
        gs.state_timer--;
        if (gs.state_timer <= 0) {
            gs.lives--;
            if (gs.lives <= 0) {
                gs.state = STATE_GAMEOVER;
                gs.state_timer = 60; /* delay before accepting input */
            } else {
                reset_ship();
                gs.state = STATE_PLAYING;
                gs.state_timer = 50; /* brief invulnerability */
            }
        }
        break;

    case STATE_GAMEOVER:
        update_particles();
        if (gs.state_timer > 0) {
            gs.state_timer--;
        } else if (inp->thrust || inp->up) {
            gs.state = STATE_ENTER_NAME;   /* port: main.c: initials if it qualifies */
        }
        break;

    default:
        break;
    }

    /* Always update particles */
    if (gs.state == STATE_PLAYING || gs.state == STATE_LANDED)
        update_particles();

    gs.frame++;
}
