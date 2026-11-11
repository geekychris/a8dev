/*
 * Boing Twist for the Atari 130XE: a port of amiga_mcp/examples/boing_twist
 * (AmigaOS 4), after the Spectrum Next port. A picture wrapped round the
 * Boing ball is cut into tiles and jumbled: twist the bands and flip the
 * halves until it is whole again.
 *
 * Needs a 130XE (or any XL/XE with 128K and 130XE banking): the ball's
 * views are 6502 code in the extended banks (tools/gen_data.py).
 *
 * With SELECT held at start-up it runs the original's self-test,
 * bt_vector_hash() (logged; the original's host tests get 3B792758).
 */
#include <string.h>
#include "a8game.h"
#include "a8pmg.h"
#include "a8log.h"
#include "bt.h"
#include "bt_core.h"
#include "game.h"

static void need_xe(void)
{
    /* a plain text screen: the OS's own, left as it is */
    static const char msg[] = "BOING TWIST NEEDS A 130XE (128K)";
    uint8_t *s = *(uint8_t **)0x58, i;
    for (i = 0; msg[i]; ++i) s[40 * 10 + 4 + i] = (uint8_t)(msg[i] - 0x20);
    a8_log("bt: no banks\n");
    a8_fail("needs a 130XE");
    for (;;) ;
}

int main(void)
{
    uint8_t b, n;
    for (b = 0; b < 4; ++b)
        if (!bank_ok(b)) need_xe();
    os_off();
    if (console_read() & IN_SELECT) {
        uint32_t h;
        a8_log("bt: selftest start\n");
        h = bt_vector_hash();
        a8_log("bt: selftest hash=%08lX\n", h);
    }
    SDMCTL = 0;
    memset((void *)0xA000, 0, 0x1800);    /* not the font at $B800 */
    memset((void *)0xBC00, 0, 0x400);
    pmg_init();
    sound_init();
    frame_init();
    game_init();
    disp_start();
    SDMCTL = 0x22 | 0x1C;                  /* normal playfield, DL DMA, players + missiles single line */
    a8_log("bt: ready pal=%u\n", frame_is_pal() ? 1 : 0);
    for (;;) {
        n = frame_wait();
        game_frame(n);
    }
}
