/*
 * maze.h - Planet Chomp's planet: the original's cube-sphere maze (its
 * maze.c, run on the host by tools/gen_maze.c): 6 faces of 9 x 9 cells, each
 * with four exits (0..3 round the cell: on half the faces clockwise, on the
 * others anticlockwise, seen from outside: mz_face bit 7).
 */
#ifndef MAZE_H
#define MAZE_H
#include <stdint.h>

#define GRID_N   9
#define CELLS    486
#define NLEVELS  16                       /* the original's mazes for levels 1..16 (then again) */
#define NONE     0xFFFF

extern const uint8_t mz_nbl[CELLS][4];    /* the neighbour through each exit: low byte */
extern const uint8_t mz_nbh[CELLS];       /*   and its 9th bit (bit k for exit k) */
#define NB(c, k)    ((uint16_t)(mz_nbl[c][k] | (uint16_t)((mz_nbh[c] >> (k)) & 1) << 8))
extern const uint8_t mz_rev[CELLS];       /* the exit back from it, 2 bits an exit */
extern const int8_t mz_dir[CELLS][3];     /* the direction from the centre, Q7 */
extern const uint8_t mz_face[CELLS];      /* face 0..5; bit 7: exits run the other way */
extern const uint16_t mz_start, mz_nest, mz_keys[4];
extern const uint8_t mz_level[NLEVELS][CELLS / 2];   /* passages: exit k open = bit k (two cells a byte) */

#define REV(c, k)   ((uint8_t)((mz_rev[c] >> ((k) * 2)) & 3))
#define FLIPPED(c)  (mz_face[c] & 0x80)
#endif
