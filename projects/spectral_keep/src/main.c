/*
 * Spectral Keep for the Atari 130XE - the Atari edition of
 * amiga_mcp/examples/spectral_keep (from the Spectrum Next port).
 *
 * Front end (main memory $4000-$7FFF): the loop. The rules (bank 0) step
 * 12.5 times a second (keep.h: STEP).
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "keep.h"
#include "sndreq.h"
#include "view.h"

void os_off(void);
uint8_t take_latch(void);
extern uint8_t in_held;
uint8_t __fastcall__ bank_ok(uint8_t bank);

static uint16_t pad_of(uint8_t in)
{
    uint16_t p = 0;
    if (in & IN_UP) p |= B_UP;
    if (in & IN_DOWN) p |= B_DOWN;
    if (in & IN_LEFT) p |= B_LEFT;
    if (in & IN_RIGHT) p |= B_RIGHT;
    if (in & (IN_FIRE | IN_START)) p |= B_A;
    if (in & IN_OPTION) p |= B_P;
    return p;
}

static uint16_t frames16(void)
{
    uint8_t lo, hi;
    do { hi = *(volatile uint8_t *)0x13; lo = *(volatile uint8_t *)0x14; } while (hi != *(volatile uint8_t *)0x13);
    return ((uint16_t)hi << 8) | lo;
}

int main(void)
{
    uint8_t i, ok = 1, st = 0xFF;
    uint16_t t0, t1, steps = 0, busy = 0, vbusy = 0;
    os_off();
    for (i = 0; i < 4; ++i) ok &= bank_ok(i);
    a8_log("sk: banks ok=%u\n", ok);
    view_init();
    game_init();
    probe_update();
    view_step();
    a8_log("sk: ready\n");
    for (;;) {
        uint8_t pressed;
        t0 = frames16();
        pressed = take_latch();
        game_step(pad_of(in_held), pad_of(pressed));
        probe_update();
        t1 = frames16();
        view_step();
        vbusy += frames16() - t1;
        busy += frames16() - t0;
        if (++steps == 50) {
            a8_log("sk: perf steps=50 frames=%u view=%u room=%s\n", busy, vbusy, R.room->id);
            steps = busy = vbusy = 0;
        }
        if (G.state != st) { st = G.state; }
        while ((uint16_t)(frames16() - t0) < 4) ;
    }
}
