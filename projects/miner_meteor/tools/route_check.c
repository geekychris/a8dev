/*
 * Host-side route check: plays tools/routes.json through each cavern with
 * the game's own game_tick() (src/game.c compiled for the host) and passes a
 * cavern when it is cleared without losing a life and without the player
 * ever standing with no floor under the feet (the original's landing "bob",
 * fixed in src/plat.c).
 *
 *   tools/solve.sh            (runs this after the search)
 *   build/route_check tools/routes.json
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

/* a solid tile under either foot; a crumbling floor that just went is
   still under the feet the tick before, so a check looks at both ticks */
static int floor_under(const GameState *s)
{
    int16_t x = s->player.b.x, y = s->player.b.y + 16;
    uint8_t a, b;
    if (y >= GRID_H * TILE_H) return 1;
    a = s->kind[(y >> 4) * GRID_W + ((x + 1) >> 4)];
    b = s->kind[(y >> 4) * GRID_W + ((x + 14) >> 4)];
    return a == T_SOLID || a == T_COLLAPSE || a == T_CONV_L || a == T_CONV_R ||
           b == T_SOLID || b == T_COLLAPSE || b == T_CONV_L || b == T_CONV_R;
}

int main(int argc, char **argv)
{
    static GameState prev;
    FILE *f = fopen(argc > 1 ? argv[1] : "tools/routes.json", "r");
    static int fl[4000], cnt[4000];
    int c, depth = 0, have = 0, num = 0, first = -1, cleared = 0;
    uint8_t level = 0;
    int nruns = 0;
    if (!f) { perror("routes.json"); return 2; }
    /* [[[flags, n], ...], ...]: depth 2 opens a cavern, closing it plays it */
    while ((c = fgetc(f)) != EOF) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); have = 1; continue; }
        if (have) {
            if (first < 0) first = num;
            else { fl[nruns] = first; cnt[nruns++] = num; first = -1; }
            num = 0; have = 0;
        }
        if (c == '[') { if (++depth == 2) nruns = 0; }
        if (c == ']' && depth-- == 2 && level < num_levels) {
            int r, n, ticks = 0, done = 0, bobs = 0;
            uint8_t lives;
            game_init();
            game_load_level(level);
            gs.mode = GM_PLAYING;
            gs.level_index = level;
            lives = gs.lives;
            prev = gs;
            for (r = 0; r < nruns && !done; r++)
                for (n = 0; n < cnt[r] && !done; n++) {
                    game_tick((uint8_t)fl[r]);
                    ticks++;
                    if (gs.events & EV_LEVEL) done = 1;
                    else if ((gs.events & EV_DIED) || gs.lives < lives || gs.mode != GM_PLAYING) done = -1;
                    else if (gs.player.b.on_ground && !floor_under(&gs) && !floor_under(&prev)) bobs++;
                    prev = gs;
                }
            if (done == 1 && !bobs) cleared++;
            printf("cavern %u %s after %d ticks%s\n", level + 1,
                   done == 1 ? "cleared" : done < 0 ? "lost a life" : "not cleared", ticks,
                   bobs ? " (stood on air: the bob)" : "");
            level++;
        }
    }
    fclose(f);
    printf("%d of %u caverns cleared\n", cleared, num_levels);
    return cleared == num_levels ? 0 : 1;
}
