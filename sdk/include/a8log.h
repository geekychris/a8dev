/*
 * a8log.h - debug log for programs running in the a8dev harness.
 *
 * Every byte stored to $D6FF (an I/O address nothing uses on a stock
 * 400/800/XL/XE) is one character of the log; the patched Atari800 captures
 * it: ./a8 log, MCP emu_log / emu_run_until, Emulator.run_until() in tests.
 * On real hardware the stores do nothing, so the calls can stay in release
 * builds; build with A8_NO_LOG=1 (-DA8_NO_LOG) to compile them out.
 *
 * Convention: one line per event, "<program>: <what> key=value...\n".
 * A program without a test.py passes `./a8 test` by logging "A8:DONE"
 * (a8_done()) or fails with "A8:FAIL <why>" (a8_fail()).
 */
#ifndef A8LOG_H
#define A8LOG_H

#define A8_LOG_PORT  (*(volatile unsigned char *)0xD6FF)

#ifdef A8_NO_LOG
#define a8_log(...)     ((void)0)
#define a8_log_s(s)     ((void)0)
#define a8_log_c(c)     ((void)0)
#define a8_log_u(v)     ((void)0)
#define a8_done()       ((void)0)
#define a8_fail(why)    ((void)0)
#define a8_in_harness() 0
#else
/* printf-style (cc65: %d/%u/%x are 16-bit int, %ld/%lu for long). Output is
   cut at 127 characters. Pulls in vsnprintf (~2 KB). */
extern void __cdecl__ a8_log(const char *fmt, ...);
/* A plain string: tiny and fast (no formatting code linked). */
extern void __fastcall__ a8_log_s(const char *s);
/* One character. */
#define a8_log_c(c)  (A8_LOG_PORT = (unsigned char)(c))
/* An unsigned decimal number (no newline), without vsnprintf. */
extern void __fastcall__ a8_log_u(unsigned int v);
/* Test markers: "A8:DONE\n" and "A8:FAIL <why>\n". */
#define a8_done()     a8_log_s("A8:DONE\n")
extern void __fastcall__ a8_fail(const char *why);
/* 1 when running in the a8dev harness (reading $D6FF gives 0xA8 there). */
#define a8_in_harness() (A8_LOG_PORT == 0xA8)
#endif

#endif
