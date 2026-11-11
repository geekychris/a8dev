/* this port's rules (src/) on the host: the autopilot plays each seed;
   prints "seed round kills score ticks over won" (ticks in the original's 40 ms) */
#include <stdio.h>
#include <stdlib.h>
#include "td.h"
int main(int argc, char **argv)
{
    int seeds = argc > 1 ? atoi(argv[1]) : 20, s;
    for (s = 1; s <= seeds; s++) {
        long t;
        td_new_game((uint32_t)s * 7919u, 1);
        for (t = 0; t < 6000 && !G.over && !G.won; t++) td_tick(td_bot());
        printf("%d %d %d %ld %ld %d %d\n", s, G.round, G.kills, (long)G.score, t * 2, G.over, G.won);
    }
    return 0;
}
