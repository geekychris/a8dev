/*
 * Emberkeep's rules: the original's game.cpp in its order.
 *
 * port: C, 16-bit fields, the enemies as arrays; the logic is stepped at a
 * fixed 50 Hz by main.c (the original once a drawn frame); events (gs.events)
 * and the tile a pickup changed tell the screen what to redraw, where the
 * original redraws everything every frame.
 */
#include <string.h>
#include "game.h"

GameState gs;
static uint8_t tick6;                       /* gs.tick % 6 */
uint8_t grid[GRID_H][GRID_W];
int16_t e_x[MAX_ENEMIES], e_y[MAX_ENEMIES], e_min[MAX_ENEMIES], e_max[MAX_ENEMIES];
int8_t e_dx[MAX_ENEMIES], e_dy[MAX_ENEMIES];
uint8_t e_axis[MAX_ENEMIES], e_colour[MAX_ENEMIES], e_alive[MAX_ENEMIES], e_frame[MAX_ENEMIES];

void game_init(void)
{
    memset(&gs, 0, sizeof gs);
    gs.mode = GM_TITLE;
    gs.lives = 3;
    gs.lantern = LANTERN_MAX;
    gs.changed_row = 0xFF;
}

static void spawn_player(void)
{
    gs.px = gs.spawn_x;
    gs.py = gs.spawn_y;
    gs.facing = 2;                          /* face down */
    gs.frame = 0;
}

void game_load_level(uint8_t index)
{
    static uint8_t row, col, k;
    static const char *t;
    static const EnemySpec *e;
    if (index >= NUM_LEVELS) index = 0;
    t = level_tiles[index];
    gs.num_enemies = 0;
    gs.runes_total = gs.runes_remaining = 0;
    memset(e_alive, 0, sizeof e_alive);
    for (row = 0; row < GRID_H; ++row)
        for (col = 0; col < GRID_W; ++col) {
            switch (*t++) {
            case '#': k = T_WALL; break;
            case 'R': k = T_RUNE; ++gs.runes_total; ++gs.runes_remaining; break;
            case 'E': k = T_PORTAL; break;
            case 'O': k = T_OIL; break;
            case '^': k = T_HAZARD; break;
            case 'P':
                gs.spawn_x = col * TILE_W;
                gs.spawn_y = row * TILE_H;
                k = T_FLOOR;
                break;
            default: k = T_FLOOR; break;
            }
            grid[row][col] = k;
        }
    for (e = level_enemies[index]; e->axis != 0xFF && gs.num_enemies < MAX_ENEMIES; ++e) {
        k = gs.num_enemies++;
        e_axis[k] = e->axis;
        e_colour[k] = e->colour;
        e_dx[k] = e->dx;
        e_dy[k] = e->dy;
        e_x[k] = (int16_t)e->x;
        e_y[k] = (int16_t)e->y;
        e_min[k] = (int16_t)e->min_pos;
        e_max[k] = (int16_t)e->max_pos;
        e_alive[k] = 1;
        e_frame[k] = 0;
    }
    gs.lantern = LANTERN_MAX;
    gs.events |= EV_LEVEL;
    spawn_player();
}

static uint8_t tile_at_pixel(int16_t px, int16_t py)
{
    if (px < 0 || py < 0 || px >= PLAY_W || py >= PLAY_H) return T_WALL;
    return grid[(uint8_t)py >> 4][(uint16_t)px >> 4];
}

#define wall_at(x, y) (tile_at_pixel(x, y) == T_WALL)

/* port: touching_enemy and update_enemies are rules.s's */
uint8_t touching_enemy(void);
void update_enemies(void);

static void kill_player(void)
{
    gs.events |= EV_DIE;
    if (gs.lives > 0) --gs.lives;
    if (gs.lives == 0) {
        gs.mode = GM_LOSE;
        gs.state_timer = 150;
    } else {
        game_load_level(gs.level_index);   /* restart_current */
    }
}

/* 4-way walk with tile collision, X then Y (the original's update_player) */
static void update_player(uint8_t input)
{
    static int16_t dx, dy, nx, ny;
    static uint8_t col, row, *k;
    dx = dy = 0;
    if (input & INPUT_LEFT)  { dx = -WALK_SPEED; gs.facing = 3; }
    if (input & INPUT_RIGHT) { dx = +WALK_SPEED; gs.facing = 1; }
    if (input & INPUT_UP)    { dy = -WALK_SPEED; gs.facing = 0; }
    if (input & INPUT_DOWN)  { dy = +WALK_SPEED; gs.facing = 2; }

    nx = gs.px + dx;
    if (dx > 0) {
        if (wall_at(nx + 15, gs.py + 1) || wall_at(nx + 15, gs.py + 14))
            nx = ((gs.px >> 4) + 1) * TILE_W - 16;
    } else if (dx < 0) {
        if (wall_at(nx, gs.py + 1) || wall_at(nx, gs.py + 14))
            nx = (gs.px >> 4) * TILE_W;
    }
    gs.px = nx;

    ny = gs.py + dy;
    if (dy > 0) {
        if (wall_at(gs.px + 1, ny + 15) || wall_at(gs.px + 14, ny + 15))
            ny = ((gs.py >> 4) + 1) * TILE_H - 16;
    } else if (dy < 0) {
        if (wall_at(gs.px + 1, ny) || wall_at(gs.px + 14, ny))
            ny = (gs.py >> 4) * TILE_H;
    }
    gs.py = ny;

    if ((dx | dy) && tick6 == 0) gs.frame = (uint8_t)((gs.frame + 1) & 3);   /* (tick % 6) */

    /* pickups at the player's centre */
    col = (uint8_t)((uint16_t)(gs.px + 8) >> 4);
    row = (uint8_t)((uint16_t)(gs.py + 8) >> 4);
    if (col < GRID_W && row < GRID_H) {
        k = &grid[row][col];
        if (*k == T_RUNE) {
            *k = T_FLOOR;
            if (--gs.runes_remaining == 0) gs.events |= EV_OPEN;
            gs.score += RUNE_SCORE;
            gs.events |= EV_RUNE;
            gs.changed_row = row;
            gs.changed_col = col;
        } else if (*k == T_OIL) {
            *k = T_FLOOR;
            gs.lantern += OIL_REFILL;
            if (gs.lantern > LANTERN_MAX) gs.lantern = LANTERN_MAX;
            gs.score += OIL_SCORE;
            gs.events |= EV_OIL;
            gs.changed_row = row;
            gs.changed_col = col;
        } else if (*k == T_HAZARD) {
            kill_player();
        } else if (*k == T_PORTAL && gs.runes_remaining == 0) {
            gs.score += LEVEL_SCORE + (uint16_t)(gs.lantern >> 1);
            ++gs.level_index;
            gs.events |= EV_PORTAL;
            if (gs.level_index >= NUM_LEVELS) {
                gs.mode = GM_WIN;
                gs.state_timer = 200;
            } else {
                gs.mode = GM_LEVELWIN;
                gs.state_timer = 60;
            }
        }
    }
}

static void drain_lantern(void)
{
    if ((gs.tick & (LANTERN_DRAIN_FRAMES - 1)) == 0 && gs.lantern > 0) --gs.lantern;
    if (gs.lantern == 0) kill_player();
}

void game_tick(uint8_t input)
{
    ++gs.tick;
    if (++tick6 == 6) tick6 = 0;
    if (gs.mode == GM_TITLE) {
        if (input & INPUT_START) {
            gs.level_index = 0;
            gs.lives = 3;
            gs.score = 0;
            game_load_level(0);
            gs.mode = GM_PLAYING;
        }
        return;
    }
    if (gs.mode == GM_WIN || gs.mode == GM_LOSE) {
        if (gs.state_timer) --gs.state_timer;
        if ((input & INPUT_START) && gs.state_timer == 0) gs.mode = GM_TITLE;
        return;
    }
    if (gs.mode == GM_LEVELWIN) {
        if (gs.state_timer) { --gs.state_timer; return; }
        game_load_level(gs.level_index);
        gs.mode = GM_PLAYING;
        return;
    }
    update_player(input);
    if (gs.mode != GM_PLAYING) return;
    update_enemies();
    drain_lantern();
    if (gs.mode != GM_PLAYING) return;
    if (touching_enemy()) kill_player();
}
