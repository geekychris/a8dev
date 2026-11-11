/*
 * Rotoball: the floor's stripe rows, built once (see tools/gen_gfx.py for
 * the geometry and floor.s for how a frame picks from them).
 *
 * port: the original works out every floor pixel of both views each frame.
 * Its floor is the same stripes at every camera position, only shifted:
 * row r (dy lines below the horizon) is cells of 5 dy / 3 pixels, A and B
 * in turn, whatever the camera does. So each row's stripes are built here,
 * once, a period and a screen wide, in four copies a pixel apart; a frame
 * only points each display list line at the right byte of the right copy
 * (the camera's sideways place) and adds half a period where the row's
 * cell along the pitch is odd (its place down the pitch).
 *
 * The rows go in LOWRAM ($0C00-$1FFF) and the screen area above the display
 * lists (10K; a row never crosses a 4K boundary, which
 * ANTIC's counter can't).
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "gfx.h"
#include "view.h"

uint8_t ra_lo[4 * FROWS], ra_hi[4 * FROWS];     /* row r, copy c: [4 r + c] */

#pragma bss-name (push, "LOWRAM")
static uint8_t lowram[0x1400];
#pragma bss-name (pop)

static uint8_t *at, *end;
static uint8_t region;

static uint8_t *alloc_row(uint8_t len)
{
    for (;;) {
        uint8_t *p = at;
        /* not across a 4K boundary */
        if ((((uint16_t)p) & 0xF000) != (((uint16_t)p + len - 1) & 0xF000))
            p = (uint8_t *)((((uint16_t)p) + len - 1) & 0xF000);
        if (p + len <= end) {
            at = p + len;
            return p;
        }
        ++region;
        if (region == 1) { at = FLOOR_MEM; end = (uint8_t *)0xC000; }
        else { a8_fail("floor: out of memory"); for (;;) ; }
    }
}

/* row r's stripes from pixel 0: cell m (A when even: colour 1; B: 2)
   covers the pixels u with (u + 0.5) in [m w, (m + 1) w) */
static void stripes(uint8_t *d, uint8_t len, uint16_t w16)
{
    static uint16_t u, n, u1;
    static uint32_t edge;
    static uint8_t m, v, sh;
    n = (uint16_t)len * 4;
    memset(d, 0, len);
    u = 0;
    m = 0;
    edge = w16;                             /* (m + 1) w, 8.8 */
    while (u < n) {
        /* the first pixel of the next cell: (u1 + 0.5) * 256 >= edge */
        u1 = (uint16_t)((edge + 127) >> 8);
        if (u1 > n) u1 = n;
        v = (m & 1) ? 2 : 1;
        for (; u < u1; ++u) {
            sh = (uint8_t)((3 - (u & 3)) << 1);
            d[u >> 2] |= (uint8_t)(v << sh);
        }
        ++m;
        edge += w16;
    }
}

void floor_build(void)
{
    static uint8_t r, c, j, len, sh;
    static uint8_t *row0, *d;
    region = 0;
    at = lowram;
    end = lowram + sizeof lowram;
    for (r = 0; r < FROWS; ++r) {
        len = row_len[r];
        row0 = alloc_row(len);
        stripes(row0, len, row_w16[r]);
        ra_lo[4 * r] = (uint8_t)(uint16_t)row0;
        ra_hi[4 * r] = (uint8_t)((uint16_t)row0 >> 8);
        /* copy c: the stripes c pixels to the left */
        for (c = 1; c < 4; ++c) {
            d = alloc_row(len);
            sh = (uint8_t)(c << 1);
            for (j = 0; j + 1 < len; ++j)
                d[j] = (uint8_t)((row0[j] << sh) | (row0[j + 1] >> (8 - sh)));
            d[len - 1] = (uint8_t)(row0[len - 1] << sh);
            ra_lo[4 * r + c] = (uint8_t)(uint16_t)d;
            ra_hi[4 * r + c] = (uint8_t)((uint16_t)d >> 8);
        }
    }
}
