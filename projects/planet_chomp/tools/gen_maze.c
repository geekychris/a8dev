/*
 * gen_maze.c - Planet Chomp's maze data for the Atari, made by the
 * original's maze.c (tools/orig, from amiga_mcp/examples/planet_chomp):
 * the cube-sphere's cells (6 faces of 9 x 9), each cell's four
 * neighbours and the exit back, its direction (Q7), its face and the
 * handedness of its exits; the start, the nest and the keys; and for
 * levels 1..NLEVELS the original's carved maze (seed 2026 + 7919 level),
 * an exit's passage bits per cell.
 *
 *   cc -O1 -Itools/orig -o build/gen_maze tools/gen_maze.c tools/orig/maze.c
 *   build/gen_maze > src/maze_data.c
 *   LEVELS=1 build/gen_maze | sed -n '/levels 1/,$p' > src/maze_levels.c   (with the #include)
 */
#include <stdio.h>
#include <stdlib.h>
#include "pc.h"

#define NLEVELS 16       /* (maze.h) */

int main(void)
{
    int c, k, lv;
    mz_build(2026UL + 7919UL);
    printf("/* made by tools/gen_maze.c from the original's maze.c - do not edit */\n");
    printf("#include \"maze.h\"\n\n");
    printf("/* the neighbour through each exit: its low byte, and bit k of mz_nbh its 9th bit */\n");
    printf("const uint8_t mz_nbl[CELLS][4] = {\n");
    for (c = 0; c < CELLS; c++)
        printf("    { %d, %d, %d, %d },\n", mz_nb[c][0] & 255, mz_nb[c][1] & 255, mz_nb[c][2] & 255, mz_nb[c][3] & 255);
    printf("};\nconst uint8_t mz_nbh[CELLS] = {");
    for (c = 0; c < CELLS; c++)
        printf("%s%d", c == 0 ? "\n    " : c % 16 ? ", " : ",\n    ",
               (mz_nb[c][0] >> 8) | (mz_nb[c][1] >> 8) << 1 | (mz_nb[c][2] >> 8) << 2 | (mz_nb[c][3] >> 8) << 3);
    printf("\n};\n/* the exit back from each neighbour, 2 bits each (exit 0 in bits 0-1) */\n");
    printf("const uint8_t mz_rev[CELLS] = {");
    for (c = 0; c < CELLS; c++)
        printf("%s%d", c == 0 ? "\n    " : c % 16 ? ", " : ",\n    ", mz_rev[c][0] | mz_rev[c][1] << 2 | mz_rev[c][2] << 4 | mz_rev[c][3] << 6);
    printf("\n};\n/* each cell's direction from the planet's centre, Q7 */\n");
    printf("const int8_t mz_dir[CELLS][3] = {\n");
    for (c = 0; c < CELLS; c++) {
        printf("    { %ld, %ld, %ld },\n", (mz_dir[c].x * 127 + 8192) >> 14, (mz_dir[c].y * 127 + 8192) >> 14,
               (mz_dir[c].z * 127 + 8192) >> 14);
    }
    printf("};\n/* the face (0..5) and, bit 7, exits running clockwise seen from outside */\n");
    printf("const uint8_t mz_face[CELLS] = {");
    for (c = 0; c < CELLS; c++) {
        int f = c / (GRID_N * GRID_N);
        /* exits 0, 1 step along a, b: their cross product against the face normal */
        int *s0 = mz_step[c][0], *s1 = mz_step[c][1], *n = mz_face[c];
        long cr = (long)(s0[1] * s1[2] - s0[2] * s1[1]) * n[0] + (long)(s0[2] * s1[0] - s0[0] * s1[2]) * n[1] +
                  (long)(s0[0] * s1[1] - s0[1] * s1[0]) * n[2];
        printf("%s%d", c == 0 ? "\n    " : c % 16 ? ", " : ",\n    ", f | (cr < 0 ? 0x80 : 0));
    }
    printf("\n};\nconst uint16_t mz_start = %d, mz_nest = %d;\n", mz_start, mz_nest);
    printf("const uint16_t mz_keys[4] = { %d, %d, %d, %d };\n", mz_keys[0], mz_keys[1], mz_keys[2], mz_keys[3]);
    if (getenv("LEVELS") == NULL) return 0;
    printf("/* levels 1..%d: the passages, exit k open in bit k */\n", NLEVELS);
    printf("const uint8_t mz_level[NLEVELS][CELLS / 2] = {\n");
    for (lv = 1; lv <= NLEVELS; lv++) {
        mz_build(2026UL + 7919UL * (unsigned long)lv);
        printf("    {");
        for (c = 0; c < CELLS; c += 2) {
            int a = 0, b = 0;
            for (k = 0; k < 4; k++) { a |= mz_open[c][k] << k; b |= mz_open[c + 1][k] << k; }
            printf("%s%d", c == 0 ? "\n     " : c % 32 ? ", " : ",\n     ", a | b << 4);
        }
        printf(" },\n");
    }
    printf("};\n");
    return 0;
}
