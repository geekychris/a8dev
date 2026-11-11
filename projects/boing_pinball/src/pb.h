/* pb.h - Boing Pinball for the Atari 130XE: the parts shared by the front end */
#ifndef PB_H
#define PB_H
#include <stdint.h>
#include "pbz.h"

extern Table table_0;
extern Game G;
/* banks.s */
extern void __fastcall__ bank_select(uint8_t bank);
extern uint8_t __fastcall__ bank_ok(uint8_t bank);
/* maths.s */
extern void maths_init(void);
/* os.s */
extern void os_off(void);
extern uint8_t take_latch(void);
extern uint8_t in_held;
#endif
