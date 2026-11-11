/* host stand-in for cc65.h (tools/host_check.sh) */
#include <stdint.h>
static inline uint32_t umul16x16r32(uint16_t a, uint16_t b) { return (uint32_t)a * b; }
