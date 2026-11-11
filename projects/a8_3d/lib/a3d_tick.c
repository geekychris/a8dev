/* a3d_tick.c - a timer in units of 2 scan lines (VCOUNT) for profiling. */
#include "a8game.h"
#include "a3d_int.h"

uint16_t a3d_ticks(void)
{
    uint8_t f, v, per = frame_is_pal() ? 156 : 131;
    do {
        f = RTCLOK2;
        v = VCOUNT;
    } while (f != RTCLOK2);
    /* the VBI (RTCLOK2 + 1) comes at scan line 248 = VCOUNT 124 */
    v = v >= 124 ? (uint8_t)(v - 124) : (uint8_t)(v + per - 124);
    return (uint16_t)f * per + v;
}

uint16_t a3d_since(uint16_t t0)
{
    uint16_t t = a3d_ticks(), m = frame_is_pal() ? 156u * 256u : 131u * 256u;
    return t >= t0 ? t - t0 : (uint16_t)(t + m - t0);
}
