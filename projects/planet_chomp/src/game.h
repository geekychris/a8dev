/* game.c: Planet Chomp's rules (the original's, on the cell graph) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "maze.h"

enum { GS_TITLE, GS_READY, GS_PLAYING, GS_DYING, GS_CLEAR, GS_OVER };
enum { GM_NEST, GM_ACTIVE, GM_EATEN };
enum { GK_CHASER, GK_AMBUSHER, GK_FLANKER, GK_DRIFTER };

/* sounds the step asks for (main.c plays them) */
#define SFX_CRUMB 0x01
#define SFX_KEY   0x02
#define SFX_EAT   0x04
#define SFX_DEATH 0x08
#define SFX_CLEAR 0x10
#define SFX_EXTRA 0x20
#define SFX_START 0x40

typedef struct {
    uint16_t from, to, prev;
    uint8_t exitk, moving;
    uint8_t head;                   /* the exit it faces (at from): the way it goes on */
    uint16_t t;                     /* Q16 along the edge */
} Mover;

typedef struct {
    Mover mv;
    uint8_t mode, fright, kind, slot;
    int8_t scatter[3];
    uint16_t release_at;            /* life ticks */
    uint32_t rng;
} Ghost;

typedef struct {
    uint8_t state, demo;
    uint16_t timer, life, clock;    /* ticks in state, in this life, total */
    int32_t score;
    uint8_t lives, level, extra_given;
    uint16_t crumbs_left, crumbs_total;
    int16_t power_left, power_total;
    uint8_t scatter, mode_index, combo;
    int16_t mode_timer;
    uint16_t player_speed, ghost_speed;   /* Q8 units a second */
    uint16_t d_player, d_ghost, d_fright, d_eaten;   /* a step's (port: made once a level) */
    Mover player;
    uint8_t powered;
    Ghost ghost[4];
    uint8_t crumb[CELLS], key[CELLS];
    int16_t nest_dist[CELLS];
    uint8_t open[CELLS / 2];
    uint16_t mouth;
    uint8_t sfx, arrived;           /* sounds asked for; the player reached a cell */
    int16_t popup_pts;
    uint8_t popup_ghost, popup_t;
} Game;

extern Game g;
extern const char *const ghost_name[4];

void game_init(void);
void game_start(uint8_t demo);
void game_step(uint8_t held, uint8_t pressed);
uint8_t open_of(uint16_t c);
uint16_t mover_nearest(const Mover *m);
void mover_pos(const Mover *m, int16_t *p);
uint8_t ghost_dangerous(const Ghost *gh);
#endif
