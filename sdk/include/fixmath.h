/*
 * fixmath.h - integer helpers: 16 x 16 -> 32 multiplies and square roots
 * (used by fix16.h and vec3.h). In C; the multiplies are the hot spot to
 * rewrite in 6502 asm when a game needs it.
 */
#ifndef A8_FIXMATH_H
#define A8_FIXMATH_H
#include <stdint.h>

extern uint32_t mulu16(uint16_t a, uint16_t b);    /* a * b */
extern int32_t mul16(int16_t a, int16_t b);        /* a * b */
extern int16_t mulq14(int16_t a, int16_t b);       /* (a * b) >> 14 (floor) */
extern int16_t mulq8(int16_t a, int16_t b);        /* (a * b) >> 8 (floor) */
extern uint16_t isqrt32(uint32_t v);               /* floor(sqrt(v)) */

#endif
