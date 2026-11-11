/*
 * a8hw.h - Atari 8-bit hardware registers and OS shadows (no code).
 *
 * Write the OS *shadow* (COLOR0, SDMCTL, PCOLR0...) for anything the OS
 * copies to the chip every vertical blank; writing the chip register
 * directly lasts one frame. Registers with no shadow (HPOSP0, SIZEP0,
 * AUDF1...) are written directly.
 */
#ifndef A8HW_H
#define A8HW_H
#include <stdint.h>

/* SDK version (sdk/README.md has the changelog and the compatibility rule):
   major * 10000 + minor * 100 + patch. Check with #if A8_SDK_VERSION >= ... */
#define A8_SDK_VERSION 10201      /* 1.2.1 */

#define A8_REG(a)    (*(volatile uint8_t *)(a))
#define A8_REG16(a)  (*(volatile uint16_t *)(a))

/* ---- OS page 0 / 2 shadows */
#define RTCLOK0   A8_REG(0x12)    /* real-time clock, +1 every VBI in RTCLOK2 */
#define RTCLOK1   A8_REG(0x13)
#define RTCLOK2   A8_REG(0x14)
#define ATRACT    A8_REG(0x4D)    /* attract mode counter: clear it to stop colour cycling */
#define SAVMSC    A8_REG16(0x58)  /* OS screen memory */
#define VDSLST    A8_REG16(0x200) /* display list interrupt vector */
#define VVBLKI    A8_REG16(0x222)
#define VVBLKD    A8_REG16(0x224)
#define SDMCTL    A8_REG(0x22F)   /* -> DMACTL */
#define SDLSTL    A8_REG16(0x230) /* -> DLISTL/H */
#define GPRIOR    A8_REG(0x26F)   /* -> PRIOR */
#define STICK0    A8_REG(0x278)
#define STICK1    A8_REG(0x279)
#define STRIG0    A8_REG(0x284)
#define STRIG1    A8_REG(0x285)
#define PCOLR0    A8_REG(0x2C0)   /* player colours 0x2C0..0x2C3 */
#define COLOR0    A8_REG(0x2C4)   /* playfield 0..3 = 0x2C4..0x2C7 */
#define COLOR1    A8_REG(0x2C5)
#define COLOR2    A8_REG(0x2C6)
#define COLOR3    A8_REG(0x2C7)
#define COLOR4    A8_REG(0x2C8)   /* background / border */
#define MEMTOP    A8_REG16(0x2E5)
#define CHBAS     A8_REG(0x2F4)   /* -> CHBASE, charset page */
#define CH        A8_REG(0x2FC)   /* last key code, 0xFF = none */
#define CHACT     A8_REG(0x2F3)   /* -> CHACTL */

/* ---- GTIA */
#define HPOSP0    A8_REG(0xD000)  /* +n for player n */
#define HPOSM0    A8_REG(0xD004)  /* +n for missile n */
#define SIZEP0    A8_REG(0xD008)  /* +n */
#define SIZEM     A8_REG(0xD00C)
#define GRAFP0    A8_REG(0xD00D)
#define P0PF      A8_REG(0xD004)  /* read: player n to playfield collisions (+n) */
#define M0PF      A8_REG(0xD000)  /* read: missile n to playfield (+n) */
#define M0PL      A8_REG(0xD008)  /* read: missile n to player (+n) */
#define P0PL      A8_REG(0xD00C)  /* read: player n to player (+n) */
#define TRIG0     A8_REG(0xD010)
#define PAL       A8_REG(0xD014)  /* read: bits 1-3 = 0 on PAL, 7 on NTSC */
#define COLPM0    A8_REG(0xD012)
#define COLPF0    A8_REG(0xD016)
#define COLBK     A8_REG(0xD01A)
#define PRIOR     A8_REG(0xD01B)
#define GRACTL    A8_REG(0xD01D)
#define HITCLR    A8_REG(0xD01E)
#define CONSOL    A8_REG(0xD01F)  /* read: bit0 START, bit1 SELECT, bit2 OPTION, 0 = down */

/* ---- POKEY */
#define AUDF1     A8_REG(0xD200)  /* AUDFn = 0xD200 + 2*(n-1), AUDCn = +1 */
#define AUDC1     A8_REG(0xD201)
#define AUDCTL    A8_REG(0xD208)
#define KBCODE    A8_REG(0xD209)
#define RANDOM    A8_REG(0xD20A)
#define IRQEN     A8_REG(0xD20E)
#define SKCTL     A8_REG(0xD20F)
#define SKSTAT    A8_REG(0xD20F)

/* ---- PIA */
#define PORTA     A8_REG(0xD300)
#define PORTB     A8_REG(0xD301)  /* XL/XE: memory control (bank select on the 130XE) */
#define PACTL     A8_REG(0xD302)
#define PBCTL     A8_REG(0xD303)

/* ---- ANTIC */
#define DMACTL    A8_REG(0xD400)
#define CHACTL    A8_REG(0xD401)
#define DLISTL    A8_REG(0xD402)
#define HSCROL    A8_REG(0xD404)
#define VSCROL    A8_REG(0xD405)
#define PMBASE    A8_REG(0xD407)
#define CHBASE    A8_REG(0xD409)
#define WSYNC     A8_REG(0xD40A)
#define VCOUNT    A8_REG(0xD40B)
#define NMIEN     A8_REG(0xD40E)

/* ---- the a8dev memory map (sdk/cfg/a8.cfg) */
#define A8_LOWRAM_ADDR  0x0C00    /* $0C00-$1FFF: segment LOWRAM, see below */
#define A8_LOWRAM_SIZE  0x1400
#define A8_STACK_ADDR   0x9000
#define A8_PMG_ADDR     0x9800
#define A8_SCREEN_ADDR  0xA000
#define A8_XE_WINDOW    0x4000    /* 130XE bank window $4000-$7FFF */

/* LOWRAM: 5K more for programs loaded without DOS (the harness, the arcade
   disk). Uninitialised: set it up in code. Not for programs run from DOS
   (DOS lives there). Outside the 130XE bank window, so fine for bank buffers.

     #pragma bss-name (push, "LOWRAM")
     static int16_t verts[512];
     #pragma bss-name (pop)
*/

/* colour = hue (0..15) * 16 + luminance (0..14, even) */
#define A8_COLOR(hue, lum) ((uint8_t)(((hue) << 4) | ((lum) & 0x0E)))

#endif
