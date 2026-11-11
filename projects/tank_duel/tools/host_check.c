/* host_check.c - the autopilot fighting every round, 10 seeds each, on the
   host: the port's rules (src/td_core.c, -DPORT) or the Amiga original's
   (td_core.c on the amiga3d engine). tools/host_check.sh builds both. */
#include <stdio.h>
#include "td.h"

#ifndef PORT
static TDGame g;
#endif

int main(void)
{
    int r, s, k;
    for (r = 1; r <= 8; r++) {
        int cleared = 0, dead = 0, ticks = 0, kills = 0, hurt = 0;
        for (s = 1; s <= 10; s++) {
#ifdef PORT
            td_new_game(100 * s + r, r);
            for (k = 0; k < 6000 && !G.over && !G.won && G.round == r && !G.between; k++) td_tick(td_bot());
            if (G.between) { cleared++; ticks += k; }
            if (G.over) dead++;
            kills += G.kills; hurt += G.damage_taken;
#else
            td_new_game(&g, 100 * s + r, r);
            for (k = 0; k < 6000 && !g.over && !g.won && g.round == r && !g.between; k++) td_tick(&g, td_bot(&g));
            if (g.between) { cleared++; ticks += k; }
            if (g.over) dead++;
            kills += g.kills; hurt += g.damage_taken;
#endif
        }
        printf("round %d: cleared %2d/10, died %2d, %2d kills, %4d damage, %4d ticks to clear\n", r, cleared, dead, kills, hurt,
               cleared ? ticks / cleared : 0);
    }
    return 0;
}
