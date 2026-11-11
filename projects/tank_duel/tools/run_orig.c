/* the original's rules (tools/orig: the Spectrum port's 32-bit C, the
   original's to the bit) on the host: the autopilot plays each seed;
   prints "seed round kills score ticks over won" */
#include <stdio.h>
#include <stdlib.h>
#include "td.h"
int16_t mul16(int16_t a, int16_t b);
int main(int argc, char **argv)
{
    static TDGame g;
    int seeds = argc > 1 ? atoi(argv[1]) : 20, s;
    for (s = 1; s <= seeds; s++) {
        long t;
        td_new_game(&g, (uint32_t)s * 7919u, 1);
        for (t = 0; t < 12000 && !g.over && !g.won; t++) td_tick(&g, td_bot(&g));
        printf("%d %d %d %ld %ld %d %d\n", s, g.round, g.kills, (long)g.score, t, g.over, g.won);
    }
    return 0;
}
