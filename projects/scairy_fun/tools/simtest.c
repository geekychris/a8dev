/* host check of the simulation: cc -I../src simtest.c ../src/world.c ../src/track.c */
#include <stdio.h>
#include <string.h>
#include "sc.h"

static void drain(int verbose)
{
    Event e;
    while (world_pop_event(&e))
        if (verbose && e.type != EV_SAY)
            printf("  f%u seg %d %s a=%d b=%d cover=%d score=%ld\n", w.frame, seg_of(w.car.z),
                   ev_name[e.type], e.a, e.b, w.cover, (long)w.score);
        else if (verbose) printf("  f%u say: %s\n", w.frame, msg_text[e.a]);
}

int main(int argc, char **argv)
{
    int r, perfect = argc > 1 && !strcmp(argv[1], "perfect");
    w.ai_perfect = perfect;
    world_init();
    world_update(0);
    world_update(IN_FIRE);   /* start */
    world_update(0);
    for (r = 0; r < 6000 && w.state != GS_GAMEOVER && w.state != GS_WIN; r++) {
        uint8_t in = 0;
        if (w.state == GS_ARRIVED || w.state == GS_UPDATE) in = (r & 8) ? IN_FIRE : 0;
        world_update(in);
        drain(1);
        if (r % 500 == 0) printf("f%u route %d seg %d/%d v=%d kmh d=%d state %d cover %d score %ld\n",
            w.frame, w.route, seg_of(w.car.z), w.finish, KMH(w.car.v), w.car.d >> 4, w.state, w.cover, (long)w.score);
    }
    printf("end: state %d route %d score %ld cover %d\n", w.state, w.route, (long)w.score, w.cover);
    return 0;
}
