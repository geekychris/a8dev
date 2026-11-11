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
   so put them in LOWBSS (right after the start-up code, below $4000) and
   LOWRAM ($0C00-$1FFF, no DOS: the harness and the arcade disk) */
#pragma bss-name (push, "LOWBSS")
static uint8_t main_sig[16];
#pragma bss-name (pop)
#pragma bss-name (push, "LOWRAM")
uint8_t buf[256];      /* (not static: src/page.s fills them) */
uint8_t want[256];
#pragma bss-name (pop)

/* pattern(b, i) = (i * 7 + b * 31 + (i >> 8)) & 0xFF. Within page p
   (i = p * 256 + j) that is j * 7 + (b * 31 + p): a table plus one add, so
   the 6502 never multiplies. The first version computed it per byte in
   16-bit C and took 5,200 frames for the 64K; page.s fills a page with an
   indexed asm loop. The log reports the frame counts. */
uint8_t times7[256];
extern void __fastcall__ fill_buf(uint8_t base);
extern void __fastcall__ fill_want(uint8_t base);

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

    a8_log("banktest: start frames=%u\n", RTCLOK1 * 256 + RTCLOK2);
    { uint8_t j = 0, v = 0; do { times7[j] = v; v += 7; } while (++j); }
    for (b = 0; b < banks; ++b)
        for (page = 0; page < 64; ++page) {
            fill_buf((uint8_t)(b * 31 + page));
            xe_bank_write(b, page << 8, buf, 256);
        }
    a8_log("banktest: filled frames=%u\n", RTCLOK1 * 256 + RTCLOK2);
    for (b = 0; b < banks; ++b) {
        sum = 0;
        for (page = 0; page < 64; ++page) {
            fill_want((uint8_t)(b * 31 + page));
            xe_bank_read(b, page << 8, buf, 256);
            if (memcmp(buf, want, 256)) {
                if (!bad) a8_log("banktest: mismatch bank=%u page=%u\n", b, page);
                bad = 1;
            }
            sum += buf[page];
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
    a8_log("banktest: checked frames=%u\n", RTCLOK1 * 256 + RTCLOK2);
    a8_log("banktest: main ok=%u current=%x\n", bad ? 0 : 1, xe_bank_current);
    if (bad) a8_fail("bank check");
    else {
        text_print_at(1, 10, "ALL BANKS OK");
        a8_done();
    }
    for (;;) frame_wait();
}
