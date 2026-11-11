/*
 * banktest - proves the 130XE extended memory helpers (sdk/include/a8xe.h).
 *
 * Counts the banks, fills each 16K bank with its own pattern (byte i of
 * bank b = (i * 7 + b * 31 + (i >> 8)) & 0xFF), then reads every bank back
 * through the window and checks it, and checks main memory at $4000-$7FFF
 * survived. Logs one line per bank and A8:DONE / A8:FAIL. The host test
 * (test.py) also reads the banks straight from the emulator (bank=N).
 */
#include <string.h>
#include "a8game.h"
#include "a8gfx.h"
#include "a8xe.h"
#include "a8log.h"

/* buffers outside the bank window: this program's BSS ends above $4000,
   so put them in LOWBSS (right after the start-up code, below $4000) */
#pragma bss-name (push, "LOWBSS")
static uint8_t buf[256];
static uint8_t main_sig[16];
#pragma bss-name (pop)

static uint8_t pattern(uint8_t bank, uint16_t i)
{
    return (uint8_t)(i * 7 + bank * 31 + (i >> 8));
}

int main(void)
{
    uint8_t banks, b, bad = 0;
    uint16_t page, i, sum;
    volatile uint8_t *mainwin = (volatile uint8_t *)0x5000;

    gfx_init(GFX_TEXT, 0);
    text_print_at(1, 1, "130XE BANK TEST");

    /* a signature in main memory inside the window */
    for (i = 0; i < 16; ++i) { main_sig[i] = (uint8_t)(0xC0 + i); mainwin[i] = main_sig[i]; }

    banks = xe_bank_count();
    a8_log("banktest: banks=%u portb=%x\n", banks, PORTB);
    text_print_at(1, 3, "BANKS:");
    text_num_at(8, 3, banks, 1);
    if (banks == 0) {
        a8_fail("no extended memory (run on a 130XE)");
        for (;;) ;
    }

    for (b = 0; b < banks; ++b) {
        for (page = 0; page < 64; ++page) {
            for (i = 0; i < 256; ++i) buf[i] = pattern(b, (page << 8) | i);
            xe_bank_write(b, page << 8, buf, 256);
        }
    }
    for (b = 0; b < banks; ++b) {
        sum = 0;
        for (page = 0; page < 64; ++page) {
            memset(buf, 0, sizeof buf);
            xe_bank_read(b, page << 8, buf, 256);
            for (i = 0; i < 256; ++i) {
                if (buf[i] != pattern(b, (page << 8) | i)) {
                    if (!bad) a8_log("banktest: mismatch bank=%u off=%x got=%x\n", b, (page << 8) | i, buf[i]);
                    bad = 1;
                }
                sum += buf[i];
            }
        }
        a8_log("banktest: bank=%u ok=%u sum=%x first=%x\n", b, bad ? 0 : 1, sum, xe_bank_peek(b, 0));
        text_print_at(1, 5 + b, "BANK");
        text_num_at(6, 5 + b, b, 1);
        text_print_at(8, 5 + b, bad ? "BAD" : "OK");
    }
    xe_bank_poke(2, 0x1234, 0x5A);
    if (xe_bank_peek(2, 0x1234) != 0x5A) { a8_log("banktest: poke/peek failed\n"); bad = 1; }
    for (i = 0; i < 16; ++i)
        if (mainwin[i] != main_sig[i]) { a8_log("banktest: main memory at $5000 changed\n"); bad = 1; break; }
    a8_log("banktest: main ok=%u current=%x\n", bad ? 0 : 1, xe_bank_current);
    if (bad) a8_fail("bank check");
    else {
        text_print_at(1, 10, "ALL BANKS OK");
        a8_done();
    }
    for (;;) frame_wait();
}
