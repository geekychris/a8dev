/*
 * pb_common.c - what the rules (bank 1) and the front end (main memory)
 * both use: the missions' table, the number format, the random numbers
 * (port: moved here from pb_rules.c and zmath.c; the code is the
 * original's).
 */
#include <stdint.h>
#include "pbz.h"

const MissionDef pb_missions[5] = {
    { "POP BUMPERS", 25, MI_BUMPER, 40000L },
    { "TARGET BANKS", 2, MI_BANK,   50000L },
    { "SPINS",       40, MI_SPIN,   45000L },
    { "RAMP SHOTS",   3, MI_RAMP,   60000L },
    { "LANE SETS",    2, MI_LANES,  55000L },
};

void pb_fmt(char *out, int32_t n)
{
    /* port: the digits by subtracting powers of ten (cc65 divides 32 bits
       in ~1,500 cycles a digit; the same text) */
    static const uint32_t p10[10] = { 1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
                                      10000UL, 1000UL, 100UL, 10UL, 1UL };
    int8_t i, o = 0, started = 0;
    uint32_t u = (uint32_t)(n < 0 ? -n : n);
    char d;
    if (n < 0) out[o++] = '-';
    for (i = 0; i < 10; i++) {
        d = '0';
        while (u >= p10[i]) { u -= p10[i]; d++; }
        if (d != '0' || started || i == 9) {
            started = 1;
            out[o++] = d;
            if (i == 0 || i == 3 || i == 6) out[o++] = ',';   /* 9, 6 or 3 digits to go */
        }
    }
    out[o] = 0;
}

/* div_q14 and the multiplies: maths.s */

uint32_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}
