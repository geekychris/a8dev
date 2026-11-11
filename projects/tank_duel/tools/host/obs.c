/* host stand-in for src/obs.s (tools/host_check.sh) */
#include "td.h"
extern int16_t Q_x, Q_z, Q_dx, Q_dz, Q_reach;
extern uint8_t Q_r, Q_k;
uint8_t near_obs(void)
{
    for (; Q_k < nobs; ++Q_k) {
        if (o_kind[Q_k] == OB_CRATE && !o_hp[Q_k]) continue;
        Q_reach = (o_r[Q_k] + Q_r) * TD_P;
        Q_dx = Q_x - o_x[Q_k];
        if (Q_dx >= Q_reach || Q_dx <= -Q_reach) continue;
        Q_dz = Q_z - o_z[Q_k];
        if (Q_dz >= Q_reach || Q_dz <= -Q_reach) continue;
        return Q_k++;
    }
    return 0xFF;
}
uint8_t grid_lo[64], grid_hi[64];       /* (the asm's grid: unused here) */
