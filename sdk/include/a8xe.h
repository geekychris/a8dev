/*
 * a8xe.h - 130XE extended memory (4 banks of 16K, 64K more RAM).
 *
 * PORTB on the 130XE: bits 2-3 select the bank, bit 4 = 0 maps it for the
 * CPU and bit 5 = 0 for ANTIC, at $4000-$7FFF (the "bank window"). Bit 0 =
 * OS ROM and bit 1 = BASIC are kept as they are.
 *
 * While a bank is mapped for the CPU, whatever main RAM normally has at
 * $4000-$7FFF is not visible, so the code doing it, its data, the stack,
 * interrupts' code and the screen must be elsewhere. The copy helpers here
 * are in LOWCODE (below $4000, sdk/cfg/a8.cfg; project.mk checks) and leave
 * main memory mapped when they return. Copy to and from buffers outside the
 * window (globals beyond $8000, the screen, or LOWDATA).
 */
#ifndef A8XE_H
#define A8XE_H
#include <stdint.h>

#define XE_BANK_SIZE   0x4000
#define XE_MAIN        0xFF      /* "bank" meaning main memory */

/* number of extended banks found (4 on a 130XE, 0 on 64K machines; also
   0 on a 400/800). Destroys the first byte of each bank. */
extern uint8_t xe_bank_count(void);
/* map bank 0..3 for the CPU (ANTIC keeps main memory), or XE_MAIN */
extern void __fastcall__ xe_bank_select(uint8_t bank);
extern uint8_t xe_bank_current;
/* copy n bytes between main memory and bank `bank` at offset (0..$3FFF) */
extern void xe_bank_write(uint8_t bank, uint16_t offset, const void *src, uint16_t n);
extern void xe_bank_read(uint8_t bank, uint16_t offset, void *dst, uint16_t n);
extern uint8_t xe_bank_peek(uint8_t bank, uint16_t offset);
extern void xe_bank_poke(uint8_t bank, uint16_t offset, uint8_t v);

#endif
