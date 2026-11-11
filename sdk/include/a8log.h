/*
 * a8log.h - debug log for programs running in the a8dev harness.
 *
 * Every byte stored to A8_LOG_ADDR ($D1F0) is one character of the log ($0A
 * or ATASCII $9B ends a line); a byte stored to A8_SIGNAL_ADDR ($D1F1) is a
 * status/exit code event. The patched Atari800 captures both: ./a8 log, MCP
 * emu_log / emu_run_until, Emulator.run_until() in tests. $D1xx is the PBI
 * page, which a stock 400/800/XL/XE ignores unless a PBI device is selected
 * through $D1FF (so not $D6xx, where Covox sound often sits).
 *
 * Release builds emit nothing: with A8_RELEASE, NDEBUG or A8_NO_LOG defined
 * (project.mk: RELEASE=1 or A8_NO_LOG=1) every call compiles away, so a
 * shipped game never touches these addresses.
 *
 * Convention: one line per event, "<program>: <what> key=value...\n".
 * A program without a test.py passes `./a8 test` by logging "A8:DONE"
 * (a8_done(), signal 0) or fails with "A8:FAIL <why>" (a8_fail(), signal 1).
 */
#ifndef A8LOG_H
#define A8LOG_H

/* the only place the addresses are defined (C and, via a8_log_port, asm) */
#define A8_LOG_ADDR     0xD1F0
#define A8_SIGNAL_ADDR  0xD1F1
#define A8_LOG_PORT     (*(volatile unsigned char *)A8_LOG_ADDR)
#define A8_SIGNAL_PORT  (*(volatile unsigned char *)A8_SIGNAL_ADDR)

#if defined(A8_RELEASE) || defined(NDEBUG)
#ifndef A8_NO_LOG
#define A8_NO_LOG
#endif
#endif

#ifdef A8_NO_LOG
#define a8_log(...)     ((void)0)
#define a8_log_s(s)     ((void)0)
#define a8_log_c(c)     ((void)0)
#define a8_log_u(v)     ((void)0)
#define a8_signal(v)    ((void)0)
#define a8_done()       ((void)0)
#define a8_fail(why)    ((void)0)
#define a8_in_harness() 0
#else
/* printf-style (cc65: %d/%u/%x are 16-bit int, %ld/%lu for long). Output is
   cut at 127 characters. Pulls in vsnprintf (~2 KB). */
extern void __cdecl__ a8_log(const char *fmt, ...);
/* A plain string: tiny and fast (no formatting code linked: a program that
   uses only a8_log_s / a8_log_u / a8_log_c / a8_done / a8_fail saves ~1.3K). */
extern void __fastcall__ a8_log_s(const char *s);
/* One character. */
#define a8_log_c(c)  (A8_LOG_PORT = (unsigned char)(c))
/* An unsigned decimal number (no newline), without vsnprintf. */
extern void __fastcall__ a8_log_u(unsigned int v);
/* A status/exit-code event (the harness can stop on it). */
#define a8_signal(v) (A8_SIGNAL_PORT = (unsigned char)(v))
/* Test markers: "A8:DONE\n" + signal 0, "A8:FAIL <why>\n" + signal 1. */
extern void a8_done(void);
extern void __fastcall__ a8_fail(const char *why);
/* 1 when running in the a8dev harness: there $D1F0 reads $A8 and $D1F1
   reads $D8. Both are checked, twice, because an unmapped PBI read on real
   hardware returns the floating bus and could match once by chance. Use it
   for test conveniences only, never for game logic. */
extern unsigned char a8_in_harness(void);
#endif

#endif
