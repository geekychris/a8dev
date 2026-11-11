/*
 * Nova Defense - game logic, from amiga_mcp/examples/nova_defense/game.c.
 * The rules, scoring, march speed-up, firing rates, comet, shield erosion,
 * waves and lives are the original's. Port changes are marked "port:".
 */
#pragma static-locals(on)     /* locals in static memory: much faster code from cc65 */
#include <string.h>
#include "a8game.h"
#include "game.h"

uint8_t alive_mask[ALIEN_ROWS];
int16_t grid_x, grid_y;
int8_t swarm_dir;
uint8_t move_timer, move_delay, anim_frame, alive_count, fire_timer, fire_delay;
int16_t player_x;
int8_t lives;
uint32_t score, hiscore;
uint8_t wave, state;
int16_t state_timer;
Bullet player_bullet;
Bullet alien_bullets[MAX_ALIEN_BULLETS];
int16_t ufo_x;
int8_t ufo_dir;
uint8_t ufo_active;
int16_t ufo_timer, ufo_score_val;
uint8_t shield_px[SHIELD_COUNT + 1][SHIELD_H][SHIELD_W];   /* + the pattern */
int16_t shield_x[SHIELD_COUNT];
uint16_t shield_dirty;
int16_t ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
uint8_t ex_timer[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS], ex_kind[MAX_EXPLOSIONS];
uint8_t ex_row[MAX_EXPLOSIONS], ex_col[MAX_EXPLOSIONS];
uint8_t ev_shoot, ev_alien_hit, ev_player_hit, ev_ufo_hit, ev_march;
uint8_t march_note;
static int16_t clear_gx, clear_gy = -1;     /* the swarm's place when it last ate shields */

/* port: the SDK's xorshift instead of the 32-bit LCG (its multiply is slow
   on a 6502); the same 15-bit range */
uint16_t game_rand(void)
{
    return rnd16() & 0x7FFF;
}

static const uint8_t alien_points[] = { 30, 20, 10 };
const uint8_t col_x[ALIEN_COLS + 1] = { 0, 26, 52, 78, 104, 130, 156, 182, 208 };
const uint8_t row_y[ALIEN_ROWS] = { 0, 16, 32, 48, 64, 80 };
#define alien_type_for_row(row) ((row) <= 1 ? ALIEN_TYPE_C : (row) <= 3 ? ALIEN_TYPE_B : ALIEN_TYPE_A)

/* port (speed): the shields are made once and copied for each wave (the
   loop's 1,344 divisions by 9 took several frames) */
static uint8_t shields_made;

static void init_shields(void)
{
    uint8_t s, x, y, inset, d;
    int16_t spacing = SCREEN_W / (SHIELD_COUNT + 1);

    shield_dirty = 0x3FFF;
    for (s = 0; s < SHIELD_COUNT; s++) {
        shield_x[s] = spacing * (s + 1) - SHIELD_W / 2;
        if (shields_made) {
            memcpy(shield_px[s], shield_px[SHIELD_COUNT], sizeof(shield_px[s]));
            continue;
        }
        memset(shield_px[s], 0, sizeof(shield_px[s]));
        /* Crystal hexagon: pointed top and bottom, facet lines inside */
        for (y = 0; y < SHIELD_H; y++) {
            if (y < 5) inset = (5 - y) * 2;
            else if (y >= SHIELD_H - 3) inset = (y - (SHIELD_H - 4)) * 2;
            else inset = 0;
            for (x = inset; x < SHIELD_W - inset; x++) {
                d = x < SHIELD_W / 2 ? SHIELD_W / 2 - x : x - SHIELD_W / 2;
                shield_px[s][y][x] =
                    (d == 0 || (x + y) % 9 == 0 || (x - y + 18) % 9 == 0) ? 2 : 1;
            }
        }
        if (s == 0) {
            memcpy(shield_px[SHIELD_COUNT], shield_px[0], sizeof(shield_px[0]));
            shields_made = 1;
        }
    }
}

static void spawn_explosion(int16_t x, int16_t y, uint8_t kind, uint8_t r, uint8_t c)
{
    uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_active[i]) {
            ex_x[i] = x;
            ex_y[i] = y;
            ex_timer[i] = 10;
            ex_active[i] = 1;
            ex_kind[i] = kind;
            ex_row[i] = r;
            ex_col[i] = c;
            return;
        }
    }
}

void game_init(void)
{
    state = STATE_TITLE;
    lives = 3;
    wave = 1;
    player_x = SCREEN_W / 2 - PLAYER_W / 2;
}

void game_init_wave(void)
{
    uint8_t i;

    memset(alive_mask, 0xFF, sizeof(alive_mask));
    grid_x = ALIEN_START_X;
    grid_y = ALIEN_START_Y;
    swarm_dir = 1;
    move_timer = 0;
    anim_frame = 0;
    alive_count = ALIEN_ROWS * ALIEN_COLS;
    fire_timer = 0;

    move_delay = 2;
    if (wave >= 3) move_delay = 1;
    {
        int16_t fd = 30 - (int16_t)(wave - 1) * 5;
        fire_delay = fd < 8 ? 8 : (uint8_t)fd;
    }

    player_bullet.active = 0;
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) alien_bullets[i].active = 0;
    ufo_active = 0;
    ufo_timer = 300 + (game_rand() % 400);
    for (i = 0; i < MAX_EXPLOSIONS; i++) ex_active[i] = 0;
    init_shields();
    player_x = SCREEN_W / 2 - PLAYER_W / 2;
    state = STATE_PLAYING;
    state_timer = 0;
    march_note = 0;
    clear_gy = -1;
    ev_shoot = ev_alien_hit = ev_player_hit = ev_ufo_hit = ev_march = 0;
}

/* Damage shield pixels in a radius around (sx, sy) relative to shield.
   port (speed, same pixels): the radius is always 2, so the disc
   (dx*dx + dy*dy <= 5) is a table of row spans */
static const int8_t disc_dx[5] = { 1, 2, 2, 2, 1 };    /* |dx| <= this, for dy = -2..2 */

/* port (speed): each shield's pixels (no 448-byte multiply per access),
   and display.s's clear_span: zero a span, say whether it changed */
static uint8_t *const shield_base[SHIELD_COUNT] = { shield_px[0][0], shield_px[1][0], shield_px[2][0] };
extern uint8_t *span_ptr;
#pragma zpsym ("span_ptr")
extern uint8_t __fastcall__ clear_span(uint8_t n);
extern uint8_t rect_w;
extern uint8_t __fastcall__ clear_rect(uint8_t h);

static const uint16_t row_bit[SHIELD_H] = {
    1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

static void damage_shield(uint8_t s, int8_t sx, int8_t sy)
{
    static int8_t y, x0, x1, k;
    static uint8_t *row;
    row = shield_base[s];
    for (k = 0, y = sy - 2; k < 5; ++k, ++y) {
        if (y < 0 || y >= SHIELD_H) continue;
        x0 = sx - disc_dx[k];
        x1 = sx + disc_dx[k];
        if (x0 < 0) x0 = 0;
        if (x1 >= SHIELD_W) x1 = SHIELD_W - 1;
        if (x0 <= x1) memset(row + ((uint8_t)y << 5) + x0, 0, x1 - x0 + 1);
        shield_dirty |= row_bit[y];
    }
}

static uint8_t check_bullet_shield(Bullet *b)
{
    uint8_t s;
    int16_t bx, by;
    by = b->y - SHIELD_Y;
    if (by < 0 || by >= SHIELD_H) return 0;
    for (s = 0; s < SHIELD_COUNT; s++) {
        bx = b->x - shield_x[s];
        if (bx >= 0 && bx < SHIELD_W && shield_base[s][((uint8_t)by << 5) + (uint8_t)bx]) {
            damage_shield(s, (int8_t)bx, (int8_t)by);
            b->active = 0;
            return 1;
        }
    }
    return 0;
}

static int8_t find_lowest_alien(uint8_t col)
{
    int8_t row;
    uint8_t bit = 1 << col;
    for (row = ALIEN_ROWS - 1; row >= 0; row--)
        if (alive_mask[row] & bit) return row;
    return -1;
}

static void alien_fire(void)
{
    int8_t slot = -1, row;
    uint8_t i, tries, col;
    for (i = 0; i < MAX_ALIEN_BULLETS; i++)
        if (!alien_bullets[i].active) { slot = (int8_t)i; break; }
    if (slot < 0) return;
    for (tries = 0; tries < 22; tries++) {
        col = game_rand() % ALIEN_COLS;
        row = find_lowest_alien(col);
        if (row >= 0) {
            alien_bullets[slot].x = ALIEN_X(row, col) + ALIEN_W / 2;
            alien_bullets[slot].y = ALIEN_Y(row) + ALIEN_H;
            alien_bullets[slot].active = 1;
            return;
        }
    }
}

/* lowest / highest column alive in a row mask (0xFF: none) */
static uint8_t low_col(uint8_t m)
{
    uint8_t c = 0;
    if (!m) return 0xFF;
    while (!(m & 1)) { m >>= 1; ++c; }
    return c;
}

static uint8_t high_col(uint8_t m)
{
    uint8_t c = 7;
    if (!m) return 0xFF;
    while (!(m & 0x80)) { m <<= 1; --c; }
    return c;
}

static void update_playing(uint8_t left, uint8_t right, uint8_t fire_pressed)
{
    uint8_t i, r, c;
    int16_t bullet_prev_y, ax, ay;

    ev_shoot = ev_alien_hit = ev_player_hit = ev_ufo_hit = ev_march = 0;

    if (left) player_x -= PLAYER_SPEED;
    if (right) player_x += PLAYER_SPEED;
    if (player_x < PLAYER_MIN_X) player_x = PLAYER_MIN_X;
    if (player_x > PLAYER_MAX_X) player_x = PLAYER_MAX_X;

    if (fire_pressed && !player_bullet.active) {
        player_bullet.x = player_x + PLAYER_W / 2;
        player_bullet.y = PLAYER_Y - 2;
        player_bullet.active = 1;
        ev_shoot = 1;
    }

    bullet_prev_y = player_bullet.y;
    if (player_bullet.active) {
        player_bullet.y -= PLAYER_BULLET_SPEED;
        if (player_bullet.y < 30) player_bullet.active = 0;
        check_bullet_shield(&player_bullet);
    }

    /* Player bullet vs aliens - swept Y check across previous->current.
       port (speed, same result): only the rows the sweep crosses, and the
       column under the bullet (a row's aliens don't overlap: one candidate),
       rows top first as the original's loop */
    if (player_bullet.active) {
        int16_t bx = player_bullet.x, by_lo = player_bullet.y, by_hi = bullet_prev_y, rel;
        for (r = 0; r < ALIEN_ROWS; r++) {
            ay = ALIEN_Y(r);
            if (!(by_hi >= ay && by_lo < ay + ALIEN_H)) continue;
            rel = bx - grid_x - ALIEN_ROW_SHIFT(r);
            if (rel < 0 || rel >= ALIEN_COLS * ALIEN_CELL_W) continue;
            for (c = 0; rel >= col_x[c + 1]; ++c)
                ;
            if (rel - col_x[c] >= ALIEN_W) continue;
            if (!(alive_mask[r] & (1 << c))) continue;
            ax = ALIEN_X(r, c);
            alive_mask[r] &= ~(1 << c);
            alive_count--;
            player_bullet.active = 0;
            score += alien_points[alien_type_for_row(r)];
            if (score > hiscore) hiscore = score;
            ev_alien_hit = 1;
            spawn_explosion(ax + ALIEN_W / 2, ay + ALIEN_H / 2, EXP_ALIEN, r, c);
            break;
        }
    }

    /* Player bullet vs UFO */
    if (player_bullet.active && ufo_active) {
        int16_t bx = player_bullet.x, by = player_bullet.y;
        if (bx >= ufo_x && bx < ufo_x + UFO_W && by >= UFO_Y && by < UFO_Y + UFO_H) {
            static const uint16_t ufo_scores[] = {50, 100, 150, 300};
            ufo_active = 0;
            player_bullet.active = 0;
            ufo_score_val = ufo_scores[game_rand() % 4];
            score += ufo_score_val;
            if (score > hiscore) hiscore = score;
            ev_ufo_hit = 1;
            spawn_explosion(ufo_x + UFO_W / 2, UFO_Y + UFO_H / 2, EXP_UFO, 0, 0);
        }
    }

    /* Alien movement */
    move_timer++;
    {
        uint8_t alive = alive_count, delay, step;
        if (alive <= 20) delay = 1;
        else if (alive <= 35) delay = 2;
        else delay = move_delay;
        if (alive <= 1) step = 10;
        else if (alive <= 5) step = 8;
        else if (alive <= 10) step = 5;
        else if (alive <= 20) step = 4;
        else step = 3;

        if (move_timer >= delay) {
            int16_t left_edge = SCREEN_W, right_edge = 0;
            uint8_t drop = 0, k;
            move_timer = 0;
            anim_frame ^= 1;
            ev_march = 1;
            march_note = march_note == 2 ? 0 : march_note + 1;

            /* Actual swarm bounds (port: from each row's first and last
               alien, the same as the loop over all of them) */
            for (r = 0; r < ALIEN_ROWS; r++) {
                k = low_col(alive_mask[r]);
                if (k == 0xFF) continue;
                ax = ALIEN_X(r, k);
                if (ax < left_edge) left_edge = ax;
                ax = ALIEN_X(r, high_col(alive_mask[r])) + ALIEN_CELL_W;
                if (ax > right_edge) right_edge = ax;
            }
            if (swarm_dir > 0 && right_edge >= SCREEN_W - 8) drop = 1;
            if (swarm_dir < 0 && left_edge <= 8) drop = 1;
            if (drop) {
                grid_y += 6;
                swarm_dir = -swarm_dir;
            } else {
                grid_x += swarm_dir > 0 ? step : -step;
            }
        }
    }

    /* Alien firing */
    if (++fire_timer >= fire_delay) {
        fire_timer = 0;
        alien_fire();
    }

    /* Alien bullets */
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        Bullet *b = &alien_bullets[i];
        if (!b->active) continue;
        b->y += ALIEN_BULLET_SPEED;
        if (b->y > SCREEN_H) {
            b->active = 0;
            continue;
        }
        check_bullet_shield(b);
        if (!b->active) continue;
        if (b->x >= player_x && b->x < player_x + PLAYER_W && b->y >= PLAYER_Y && b->y < PLAYER_Y + PLAYER_H) {
            b->active = 0;
            ev_player_hit = 1;
            state = STATE_DYING;
            state_timer = 60;
            spawn_explosion(player_x + PLAYER_W / 2, PLAYER_Y + PLAYER_H / 2, EXP_PLAYER, 0, 0);
        }
    }

    /* UFO */
    if (!ufo_active) {
        if (--ufo_timer <= 0) {
            ufo_active = 1;
            ufo_dir = (game_rand() & 1) ? 1 : -1;
            ufo_x = ufo_dir > 0 ? -UFO_W : SCREEN_W;
        }
    } else {
        ufo_x += ufo_dir * UFO_SPEED;
        if (ufo_x < -UFO_W - 10 || ufo_x > SCREEN_W + 10) {
            ufo_active = 0;
            ufo_timer = 400 + (game_rand() % 600);
        }
    }

    /* Explosions */
    for (i = 0; i < MAX_EXPLOSIONS; i++)
        if (ex_active[i] && --ex_timer[i] == 0) ex_active[i] = 0;

    /* Aliens destroy shields on contact */
    {
        int16_t bottom_alien_y = 0;
        for (r = ALIEN_ROWS; r-- > 0;)
            if (alive_mask[r]) { bottom_alien_y = ALIEN_Y(r) + ALIEN_H; break; }

        if (bottom_alien_y >= SHIELD_Y && (grid_x != clear_gx || grid_y != clear_gy)) {
            /* port (speed, same pixels): only when the swarm has moved (the
               shields can't grow back), only the alien rows crossing the
               shields' band, and after a sideways step only the strip the
               step uncovered (the rest was cleared before); everything after
               a drop. The rectangles are cleared by display.s. */
            static uint8_t s, m, y0, y1, wy, full;
            static int16_t x0, x1, sx, d;
            full = grid_y != clear_gy;
            d = grid_x - clear_gx;
            for (r = 0; r < ALIEN_ROWS; r++) {
                m = alive_mask[r];
                ay = ALIEN_Y(r);
                if (!m || ay + ALIEN_H <= SHIELD_Y || ay >= SHIELD_Y + SHIELD_H) continue;
                y0 = ay > SHIELD_Y ? (uint8_t)(ay - SHIELD_Y) : 0;
                y1 = ay + ALIEN_H < SHIELD_Y + SHIELD_H ? (uint8_t)(ay + ALIEN_H - SHIELD_Y) : SHIELD_H;
                for (c = 0; c < ALIEN_COLS; c++, m >>= 1) {
                    if (!(m & 1)) continue;
                    ax = ALIEN_X(r, c);
                    x0 = ax;
                    x1 = ax + ALIEN_W;
                    if (!full) {                    /* the strip the step uncovered */
                        if (d > 0 && d < ALIEN_W) x0 = x1 - d;
                        else if (d < 0 && -d < ALIEN_W) x1 = x0 - d;
                    }
                    for (s = 0; s < SHIELD_COUNT; s++) {
                        sx = shield_x[s];
                        if (x1 <= sx || x0 >= sx + SHIELD_W) continue;
                        span_ptr = shield_base[s] + ((uint8_t)y0 << 5) + (uint8_t)(x0 > sx ? x0 - sx : 0);
                        rect_w = (uint8_t)((x1 < sx + SHIELD_W ? x1 - sx : SHIELD_W) - (x0 > sx ? x0 - sx : 0));
                        if (clear_rect(y1 - y0))
                            for (wy = y0; wy < y1; wy++) shield_dirty |= row_bit[wy];
                    }
                }
            }
            clear_gx = grid_x;
            clear_gy = grid_y;
        }
        if (bottom_alien_y >= PLAYER_Y) {
            state = STATE_GAMEOVER;
            state_timer = 120;
        }
    }

    if (alive_count == 0) {
        state = STATE_WAVE_CLEAR;
        state_timer = 90;
    }
}

void game_update(uint8_t left, uint8_t right, uint8_t fire_pressed)
{
    switch (state) {
    case STATE_TITLE:
        if (fire_pressed) {
            score = 0;
            lives = 3;
            wave = 1;
            game_init_wave();
        }
        break;
    case STATE_PLAYING:
        update_playing(left, right, fire_pressed);
        break;
    case STATE_DYING:
        if (--state_timer <= 0) {
            if (--lives <= 0) {
                state = STATE_GAMEOVER;
                state_timer = 150;
            } else {
                state = STATE_PLAYING;
                player_x = SCREEN_W / 2 - PLAYER_W / 2;
            }
        }
        break;
    case STATE_GAMEOVER:
        /* port: main.c goes on to the high-score table */
        if (--state_timer <= 0 || fire_pressed) state = STATE_TITLE;
        break;
    case STATE_WAVE_CLEAR:
        if (--state_timer <= 0) {
            wave++;
            game_init_wave();
        }
        break;
    }
}
