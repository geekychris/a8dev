/* a8log.h: the harness log (addresses: A8_LOG_ADDR / A8_SIGNAL_ADDR). */
#include <stdarg.h>
#include <stdio.h>
#include "a8log.h"

#ifndef A8_NO_LOG
void __fastcall__ a8_log_s(const char *s)
{
    while (*s) A8_LOG_PORT = *s++;
}

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

void a8_done(void)
{
    a8_log_s("A8:DONE\n");
    a8_signal(0);
}

void __fastcall__ a8_fail(const char *why)
{
    a8_log_s("A8:FAIL ");
    a8_log_s(why);
    A8_LOG_PORT = '\n';
    a8_signal(1);
}
#endif
