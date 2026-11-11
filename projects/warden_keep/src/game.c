/* Warden Keep's rules (game.h): the original's game.cpp */
#include <string.h>
#include "game.h"

#pragma static-locals (on)

GameState gs;
uint8_t obj_kind[MAX_OBJ], obj_x[MAX_OBJ], obj_y[MAX_OBJ], num_obj;

void game_load_room(uint8_t room)
{
    const uint8_t *d = room_data[room];
    gs.room = room;
    gs.px = d[0];
    gs.py = d[1];
    gs.facing = 0;
    d += 2;
    num_obj = 0;
    gs.jewels_left = 0;
    while (*d) {
        obj_kind[num_obj] = d[0];
        obj_x[num_obj] = d[1];
        obj_y[num_obj] = d[2];
        if (d[0] == O_JEWEL) ++gs.jewels_left;
        ++num_obj;
        d += 3;
    }
    gs.jewels_total = gs.jewels_left;
    gs.seconds = ROOM_SECONDS;
    gs.sub = 0;
    gs.room_score = gs.score;
    gs.mode = GM_PLAYING;
    gs.moved_obj = gs.taken_obj = 0xFF;
}

/* object_at: the first object on a tile (0xFF none); the walls are the
   room's north and west edges */
static uint8_t object_at(int8_t x, int8_t y)
{
    uint8_t i;
    for (i = 0; i < num_obj; ++i)
        if (obj_kind[i] && obj_x[i] == (uint8_t)x && obj_y[i] == (uint8_t)y) return i;
    return 0xFF;
}

static uint8_t outside(int8_t x, int8_t y)
{
    return (uint8_t)x >= WORLD_W || (uint8_t)y >= WORLD_H;
}

static uint8_t blocked(int8_t x, int8_t y)
{
    uint8_t o;
    if (outside(x, y) || x == 0 || y == 0) return 1;
    o = object_at(x, y);
    return o != 0xFF && obj_kind[o] != O_JEWEL;
}

static void try_move(int8_t dx, int8_t dy)
{
    int8_t tx, ty, bx, by;
    uint8_t o;
    if (dx > 0) gs.facing = 1;
    else if (dx < 0) gs.facing = 3;
    else if (dy < 0) gs.facing = 0;
    else gs.facing = 2;
    tx = gs.px + dx;
    ty = gs.py + dy;
    if (tx == 0 || ty == 0) { gs.events |= EV_BUMP; return; }   /* the walls */
    o = object_at(tx, ty);
    if (o != 0xFF) {
        if (obj_kind[o] == O_BLOCK) { gs.events |= EV_BUMP; return; }
        if (obj_kind[o] == O_BARREL) {
            bx = tx + dx;
            by = ty + dy;
            if (blocked(bx, by)) { gs.events |= EV_BUMP; return; }
            gs.moved_obj = o;
            gs.moved_ox = obj_x[o];
            gs.moved_oy = obj_y[o];
            obj_x[o] = bx;
            obj_y[o] = by;
            gs.events |= EV_PUSH;
        }
    }
    if (outside(tx, ty)) { gs.events |= EV_BUMP; return; }
    gs.px = tx;
    gs.py = ty;
    gs.events |= EV_STEP;
    /* pickup_at: the first object here (a barrel pushed onto a jewel
       hides it, as in the original) */
    o = object_at(tx, ty);
    if (o != 0xFF && obj_kind[o] == O_JEWEL) {
        obj_kind[o] = O_NONE;
        gs.taken_obj = o;
        gs.taken_x = (uint8_t)tx;
        gs.taken_y = (uint8_t)ty;
        gs.score += 100;
        gs.events |= EV_JEWEL;
        if (--gs.jewels_left == 0) {
            gs.bonus = gs.relaxed ? 0 : (uint16_t)gs.seconds * BONUS;
            gs.score += gs.bonus;
            gs.mode = GM_CLEARED;
            gs.events |= EV_CLEAR;
        }
    }
}

void game_tick(uint8_t input)
{
    gs.events = 0;
    if (gs.mode != GM_PLAYING) return;
    if (input & INPUT_UP) try_move(0, -1);
    else if (input & INPUT_DOWN) try_move(0, 1);
    else if (input & INPUT_LEFT) try_move(-1, 0);
    else if (input & INPUT_RIGHT) try_move(1, 0);
    if (gs.mode != GM_PLAYING || gs.relaxed) return;
    /* port: the clock */
    if (++gs.sub >= gs.tps) {
        gs.sub = 0;
        gs.events |= EV_SECOND;
        if (--gs.seconds == 0) {
            gs.mode = GM_OVER;
            gs.events |= EV_TIMEUP;
        }
    }
}
