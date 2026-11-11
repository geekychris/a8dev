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

/* shell.s in C for the host build */
Shell *fly_sh;
uint8_t fly_n, fly_obs, fly_tanks, fly_hit;
uint8_t shell_fly(void)
{
    uint8_t done = 0, j;
    Shell *sh = fly_sh;
    fly_hit = 0;
    while (done < fly_n) {
        ++done;
        sh->x += sh->dx;
        sh->z += sh->dz;
        if (sh->x > 16000 || sh->x < -16000 || sh->z > 16000 || sh->z < -16000) { fly_hit = 1; break; }
        if (fly_obs) {
            oq_x = sh->x; oq_z = sh->z; oq_extra = 0; oq_from = 0;
            if (obs_next() != 0xFF) { fly_hit = 1; break; }
        }
        for (j = 0; j < 8; j++)
            if (fly_tanks & (1 << j)) {
                pos dx = sh->x - G.tk[j].x, dz = sh->z - G.tk[j].z;
                if (dx < 448 && dx > -448 && dz < 448 && dz > -448) { fly_hit = 1; break; }
            }
        if (fly_hit) break;
    }
    return done;
}
