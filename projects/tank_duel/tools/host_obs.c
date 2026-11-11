/* obs.s in C for the host build of the rules */
#include "td_int.h"
pos oq_x, oq_z;
uint8_t oq_extra, oq_from, obs_n;
const Obstacle *obs_base;
uint8_t obs_next(void)
{
    uint8_t k;
    for (k = oq_from; k < obs_n; k++) {
        const Obstacle *o = &obs_base[k];
        pos reach = U(o->r + oq_extra), dx = oq_x - o->x, dz = oq_z - o->z;
        if (!o->hp) continue;
        if (dx < reach && dx > -reach && dz < reach && dz > -reach) return k;
    }
    return 0xFF;
}
