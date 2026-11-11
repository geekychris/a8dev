/* maths.s in C, for the host sim (tools/sim.c) */
#include <stdint.h>
int32_t smul16(int16_t a, int16_t b) { return (int32_t)a * b; }
uint32_t sqr16(int16_t a) { return (uint32_t)((int32_t)a * a); }
uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 0x40000000UL;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (uint16_t)r;
}
