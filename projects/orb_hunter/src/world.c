/*
 * Orb Hunter - the world (port: the original keeps all 48 rooms as
 * 15,360 bytes; here they are its own rooms run-length coded, 1,541 bytes
 * (data.c), the current one decoded into room[][], and what the game
 * changes in them (doors a missile opened) in a list applied when a room
 * is decoded. So the whole world fits a 64K machine with room to spare).
 */
#include <string.h>
#include "game.h"
#include "view.h"

extern const uint8_t world_rle[];
extern const uint16_t world_off[48];

UBYTE room[ROOM_H][ROOM_W];
static uint8_t cur_room;

/* the changes: room, tile (ty * 20 + tx), new tile */
#define MAX_CHANGES 48
#pragma bss-name (push, "PMGBSS")      /* (orb_hunter.cfg: $9800-$9AFF) */
static uint8_t ch_room[MAX_CHANGES], ch_tile[MAX_CHANGES], ch_val[MAX_CHANGES];
#pragma bss-name (pop)
static uint8_t n_changes;

/* the original's place_items() (levels.c): room x, y, tile x, y, type */
static const ItemSpawn items_start[13] = {
    { 1, 4, 10, 6, 2, 0 },
    { 2, 1, 10, 12, 1, 0 },
    { 3, 2, 15, 12, 3, 0 },
    { 5, 1, 10, 11, 4, 0 },
    { 6, 2, 10, 11, 5, 0 },
    { 3, 3, 5, 12, 6, 0 },
    { 5, 2, 15, 11, 8, 0 },
    { 6, 3, 16, 12, 7, 0 },
    { 1, 1, 10, 5, 0, 0 },
    { 6, 1, 5, 11, 0, 0 },
    { 4, 4, 10, 10, 0, 0 },
    { 1, 3, 5, 12, 1, 0 },
    { 5, 3, 15, 11, 1, 0 },
};
ItemSpawn g_items[MAX_ITEMS];
uint8_t g_num_items;

void items_init(void)
{
    memcpy(g_items, items_start, sizeof items_start);
    g_num_items = 13;
}

void world_reset(void)
{
    n_changes = 0;
}

void world_load_room(int8_t rx, int8_t ry)
{
    const uint8_t *s;
    uint8_t *d = &room[0][0];
    uint16_t left = ROOM_W * ROOM_H;
    uint8_t i;
    cur_room = (uint8_t)(ry * WORLD_W + rx);
    s = world_rle + world_off[cur_room];
    while (left) {
        uint8_t b = *s++, n = b >> 4, t = b & 15;
        left -= n;
        while (n--) *d++ = t;
    }
    for (i = 0; i < n_changes; ++i)
        if (ch_room[i] == cur_room) (&room[0][0])[ch_tile[i]] = ch_val[i];
}

void world_set_tile(uint8_t tx, uint8_t ty, UBYTE t)
{
    uint8_t k = (uint8_t)(ty * ROOM_W + tx), i;
    room[ty][tx] = t;
    view_tile_changed(tx, ty);              /* (redrawn at once) */
    for (i = 0; i < n_changes; ++i)
        if (ch_room[i] == cur_room && ch_tile[i] == k) { ch_val[i] = t; return; }
    if (n_changes < MAX_CHANGES) {
        ch_room[n_changes] = cur_room;
        ch_tile[n_changes] = k;
        ch_val[n_changes] = t;
        ++n_changes;
    }
}
