/* a8log.h: a8_log_s, a8_log_u, a8_done, a8_fail, a8_in_harness (no printf;
   a8_log itself is log.c). Addresses: A8_LOG_ADDR / A8_SIGNAL_ADDR. */
#include "a8log.h"

#ifndef A8_NO_LOG
void __fastcall__ a8_log_s(const char *s)
{
    while (*s) A8_LOG_PORT = *s++;
}

void __fastcall__ a8_log_u(unsigned int v)
{
    char d[6];
    unsigned char n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) A8_LOG_PORT = d[--n];
}

unsigned char a8_in_harness(void)
{
    unsigned char i;
    for (i = 0; i < 2; ++i)
        if (A8_LOG_PORT != 0xA8 || A8_SIGNAL_PORT != 0xD8) return 0;
    return 1;
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
