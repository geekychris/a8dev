/* port.c's rle_unpack for the host sim */
#include <stdint.h>
uint16_t rle_unpack(uint8_t *dst, const uint8_t *src)
{
    uint8_t *d = dst, c, n, v;
    for (;;) {
        c = *src++;
        if (c == 0x80) break;
        if (c < 0x80) { n = (uint8_t)(c + 1); while (n--) *d++ = *src++; }
        else { n = (uint8_t)(c - 0x7E); v = *src++; while (n--) *d++ = v; }
    }
    return (uint16_t)(d - dst);
}
