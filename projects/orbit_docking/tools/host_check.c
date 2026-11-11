/* host_check.c - the autopilot on every station, 10 seeds each, on the host:
   the port's rules (src/od_core.c, -DPORT) or the Amiga original's
   (od_core.c on the amiga3d engine). tools/host_check.sh builds both and
   prints them side by side: the port should dock about as often. */
#include <stdio.h>
#include <stdlib.h>
#include "od.h"

#ifndef PORT
static ODGame g;
#endif

int main(void)
{
    int lv, s, k;
    for (lv = 1; lv <= 6; lv++) {
        int docks = 0, crashes = 0, ticks = 0;
        for (s = 1; s <= 10; s++) {
#ifdef PORT
            uint8_t a, e;
            uint16_t b;
            od_new_game(100 * s + lv, lv);
            for (k = 0; k < 6000 && !G.over && !G.won && G.level == lv && !G.docks; k++) {
                od_tick(od_bot());
                while ((e = od_pop_event(&a, &b))) if (e == ODE_CRASH || e == ODE_DEBRIS) crashes++;
            }
            if (G.docks) { docks++; ticks += k; }
#else
            ODEvent e;
            od_new_game(&g, 100 * s + lv, lv);
            for (k = 0; k < 6000 && !g.over && !g.won && g.level == lv && !g.docks; k++) {
                od_tick(&g, od_bot(&g));
                while (od_pop_event(&g, &e)) if (e.type == ODE_CRASH || e.type == ODE_DEBRIS) crashes++;
            }
            if (g.docks) { docks++; ticks += k; }
#endif
        }
        printf("station %d: docked %2d/10, %2d crashes, %4d ticks to dock\n", lv, docks, crashes, docks ? ticks / docks : 0);
    }
    return 0;
}
