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
    char tmp[16];
    int8_t len = 0, i, o = 0;
    uint32_t u = (uint32_t)(n < 0 ? -n : n);
    do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (n < 0) out[o++] = '-';
    for (i = len - 1; i >= 0; i--) {
        out[o++] = tmp[i];
        if (i && i % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
}

/* div_q14 and the multiplies: maths.s */

uint32_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}
