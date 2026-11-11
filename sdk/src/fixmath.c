/* fixmath.h in C. */
#include "fixmath.h"

uint32_t mulu16(uint16_t a, uint16_t b) { return (uint32_t)a * b; }
int32_t mul16(int16_t a, int16_t b) { return (int32_t)a * b; }
int16_t mulq14(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 14); }
int16_t mulq8(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 8); }

uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint16_t)r;
}
