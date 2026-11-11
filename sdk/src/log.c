/* a8log.h: a8_log (printf-style) and a8_log_u. */
#include <stdarg.h>
#include <stdio.h>
#include "a8log.h"

void __cdecl__ a8_log(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    a8_log_s(buf);
}

void __fastcall__ a8_log_u(unsigned int v)
{
    char d[6];
    unsigned char n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) A8_LOG_PORT = d[--n];
}
