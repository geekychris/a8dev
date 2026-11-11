/* port.c - what the Spectrum port's logic took from its SDK: keep_log (the
   rest: rle.c, maths.s) */
#include <stdarg.h>
#include <stdio.h>
#include "a8log.h"
#include "keep.h"
#include "zxrle.h"
#include "fixmath.h"

void keep_log(const char *fmt, ...)
{
#ifndef A8_NO_LOG
    static char buf[96];
    va_list ap;
    va_start(ap, fmt);
    buf[0] = 's'; buf[1] = 'k'; buf[2] = ':'; buf[3] = ' ';
    vsnprintf(buf + 4, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
    a8_log("%s", buf);
#else
    (void)fmt;
#endif
}

#ifdef MATHCHECK
/* make MATHCHECK=1: maths.s against C, on many values */
extern uint32_t __fastcall__ sqr16(int16_t a);
extern int32_t __fastcall__ smul16(int16_t a, int16_t b);
static uint16_t c_isqrt(uint32_t v)
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
void math_check(void)
{
    uint16_t i, bad = 0;
    uint32_t v = 1;
    int16_t a = 1, b = -7;
    for (i = 0; i < 600; ++i) {
        uint32_t t = v + (uint32_t)i * 977;
        if (isqrt32(t) != c_isqrt(t)) { ++bad; a8_log("MATHCHECK isqrt %lu\n", t); }
        if (sqr16(a) != (uint32_t)((int32_t)a * a)) { ++bad; a8_log("MATHCHECK sqr %d\n", a); }
        if (smul16(a, b) != (int32_t)a * b) { ++bad; a8_log("MATHCHECK mul %d %d\n", a, b); }
        v = v * 3 + 1;
        a = (int16_t)(a * 5 + 3);
        b = (int16_t)(b * 7 - 11);
    }
    if (isqrt32(0xFFFFFFFFUL) != 65535U || isqrt32(0) != 0 || sqr16(-32767) != 1073676289UL ||
        smul16(-32768, 2) != -65536L || smul16(-300, -300) != 90000L) { ++bad; a8_log("MATHCHECK edge\n"); }
    a8_log("sk: mathcheck bad=%u\n", bad);
}
#endif
