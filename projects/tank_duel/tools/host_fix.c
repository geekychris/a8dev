/* what tools/orig's td_fix.c takes from the Spectrum SDK */
#include <stdint.h>
int32_t mul16(int16_t a, int16_t b) { return (int32_t)a * b; }
uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 0x40000000UL;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (uint16_t)r;
}
