/* a3d_view.c - the projection: centre and the f*256/z table. */
#include "a3d_int.h"

uint8_t a3d_aspect = 109;

void a3d_view(uint8_t cx, uint8_t cy, uint8_t f)
{
    uint16_t z, n = (uint16_t)f << 8, v;
    a3d_vcx = cx;
    a3d_vcy = cy;
    a3d_pxlo[0] = a3d_pxhi[0] = 0xFF;
    for (z = 1; z < 256; ++z) {
        v = n / z;
        a3d_pxlo[z] = (uint8_t)v;
        a3d_pxhi[z] = (uint8_t)(v >> 8);
    }
}
