/* a3d's multiply for the host build of the rules (tools/run_port.c) */
#include <stdint.h>
int16_t a3d_mb;
int16_t a3d_mulf_(int16_t a)
{
    int32_t r = ((int32_t)a * a3d_mb) >> 8;
    return (int16_t)(r > 16383 ? 16383 : r < -16383 ? -16383 : r);
}
