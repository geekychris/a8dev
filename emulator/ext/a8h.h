/* a8h.h - a8dev harness hooks for Atari800 (see emulator/patches/README.md).
 *
 * The patches add calls to these functions in the core; everything else
 * lives in a8h.c, which is #included at the end of memory.c (so it can reach
 * the extended-RAM buffers that memory.c keeps static).
 */
#ifndef A8H_H_
#define A8H_H_

#include <setjmp.h>
#include "atari.h"

#ifdef HAVE_DOWNLOAD
#error "a8h: configure with --disable-download (the harness never fetches ROMs)"
#endif

/* Set when a8h is enabled (-a8h-port given). */
extern int A8H_enabled;
/* Non-zero when the CPU hook must run before every instruction. */
extern int A8H_cpu_check;
/* Target of the longjmp that abandons a frame (load/reset mid-frame). */
extern jmp_buf A8H_jmpbuf;

/* Strip -a8h-* options from argv. Call first in main(). */
void A8H_Init(int *argc, char *argv[]);
/* Open the control socket. Call after Atari800_Initialise(). */
void A8H_Start(void);
/* Main loop: call right after the setjmp at the top of each iteration
   (A8H_LOOP_TOP does both), and after Atari800_Frame(). */
void A8H_FrameStart(void);
void A8H_FrameEnd(void);

/* CPU: called before each instruction while A8H_cpu_check (registers in
   CPU_regXX / CPU_regP). May not return (longjmp to the loop top). */
void A8H_CpuHook(void);
/* An illegal CIM/JAM opcode at CPU_regPC. Returns non-zero when handled. */
int A8H_Cim(UBYTE insn);

/* Write to page $D1 (the PBI page, inert on a stock machine). Returns
   non-zero when a8h consumed it: $D1F0 is the guest log (one character per
   write), $D1F1 a signal (status/exit code). In the harness $D1F0 reads $A8
   and $D1F1 reads $D8 (pbi.c). */
int A8H_D1Put(UWORD addr, UBYTE byte);

/* Input: called from INPUT_Frame() once per frame. */
void A8H_InputKeys(void);
int A8H_InputPort(int num, int value);
int A8H_InputTrig(int num, int value);

/* Window build: called while the CPU is held, to keep the window alive. */
extern void (*A8H_platform_idle)(int redraw);

/* Headless (libatari800) build: the whole program. */
int A8H_HeadlessMain(int argc, char *argv[]);

#define A8H_LOOP_TOP() do { if (A8H_enabled) { (void) setjmp(A8H_jmpbuf); A8H_FrameStart(); } } while (0)

#endif /* A8H_H_ */
