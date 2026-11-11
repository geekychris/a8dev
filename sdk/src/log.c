/* a8log.h: a8_log (printf-style; pulls in vsnprintf, ~1.3K). The plain
   functions are in log_s.c, so a program that only uses those doesn't link
   printf. */
#include <stdarg.h>
#include <stdio.h>
#include "a8log.h"

#ifndef A8_NO_LOG
void __cdecl__ a8_log(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    a8_log_s(buf);
}
#endif
