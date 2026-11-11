/* a8pmg.h: player/missile graphics at $9800 (single-line resolution). */
#include <string.h>
#include "a8pmg.h"

static uint8_t old_y[4], old_h[4];

uint8_t *pmg_player_mem(uint8_t n) { return (uint8_t *)(A8_PMG_ADDR + 0x400 + ((uint16_t)n << 8)); }
uint8_t *pmg_missile_mem(void) { return (uint8_t *)(A8_PMG_ADDR + 0x300); }

void pmg_init(void)
{
    uint8_t i;
    memset((void *)(A8_PMG_ADDR + 0x300), 0, 0x500);
    for (i = 0; i < 4; ++i) {
        old_h[i] = 0;
        pmg_x(i, 0);
        pmg_missile_x(i, 0);
        pmg_size(i, 0);
    }
    SIZEM = 0;
    PMBASE = A8_PMG_ADDR >> 8;
    SDMCTL |= 0x1C;           /* player + missile DMA, single-line */
    GRACTL = 3;
    GPRIOR = 1;
    HITCLR = 0;
}

void pmg_off(void)
{
    SDMCTL &= (uint8_t)~0x1C;
    GRACTL = 0;
}

void pmg_clear(uint8_t n)
{
    if (old_h[n]) memset(pmg_player_mem(n) + old_y[n], 0, old_h[n]);
    old_h[n] = 0;
}

void pmg_set(uint8_t n, uint8_t y, const uint8_t *shape, uint8_t h)
{
    uint8_t *p = pmg_player_mem(n);
    pmg_clear(n);
    if ((uint16_t)y + h > 256) h = (uint8_t)(256 - y);
    memcpy(p + y, shape, h);
    old_y[n] = y;
    old_h[n] = h;
}
