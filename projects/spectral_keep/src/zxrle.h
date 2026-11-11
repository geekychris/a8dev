/* the Spectrum SDK's zxrle unpacker (the level cells are packed with it):
   c < 0x80: c + 1 literals; c > 0x80: one byte c - 0x7E times; 0x80: end */
#ifndef ZXRLE_H
#define ZXRLE_H
#include <stdint.h>
extern uint16_t rle_unpack(uint8_t *dst, const uint8_t *src);
#endif
