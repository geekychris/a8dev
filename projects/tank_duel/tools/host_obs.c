/* obs.s in C for the host build of the rules */
#include "td_int.h"
pos oq_x, oq_z;
uint8_t oq_extra, oq_from, obs_n;
const Obstacle *obs_base;
uint8_t obs_grid[64][4];
uint8_t obs_next(void)
{
    uint8_t n, k;
    int cx = (oq_x + 16384) >> 12, cz = (oq_z + 16384) >> 12;
    if (cx < 0) cx = 0; if (cx > 7) cx = 7; if (cz < 0) cz = 0; if (cz > 7) cz = 7;
    for (n = 0; n < 4; n++) {
        k = obs_grid[cz * 8 + cx][n];
        if (k == 0xFF) break;
        if (k < oq_from) continue;
        {
        const Obstacle *o = &obs_base[k];
        pos reach = U(o->r + oq_extra), dx = oq_x - o->x, dz = oq_z - o->z;
        if (!o->hp) continue;
        if (dx < reach && dx > -reach && dz < reach && dz > -reach) return k;
        }
    }
    return 0xFF;
}
