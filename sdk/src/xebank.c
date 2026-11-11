/* a8xe.h: bank copies (the copy itself is xe.s, in LOWCODE). */
#include "a8xe.h"

extern uint8_t xe_cp_bank, xe_cp_dir;
extern uint16_t xe_cp_win, xe_cp_len;
extern const void *xe_cp_mem;
extern void xe_bank_copy(void);

static void copy(uint8_t bank, uint16_t offset, const void *mem, uint16_t n, uint8_t dir)
{
    if (offset >= XE_BANK_SIZE) return;
    if (n > XE_BANK_SIZE - offset) n = XE_BANK_SIZE - offset;
    xe_cp_bank = bank;
    xe_cp_dir = dir;
    xe_cp_win = 0x4000 + offset;
    xe_cp_mem = mem;
    xe_cp_len = n;
    xe_bank_copy();
}

void xe_bank_write(uint8_t bank, uint16_t offset, const void *src, uint16_t n) { copy(bank, offset, src, n, 0); }
void xe_bank_read(uint8_t bank, uint16_t offset, void *dst, uint16_t n) { copy(bank, offset, dst, n, 1); }

/* the bounce byte is in LOWDATA (xe.s): a static here could be in the window */
extern uint8_t xe_byte;

uint8_t xe_bank_peek(uint8_t bank, uint16_t offset)
{
    copy(bank, offset, &xe_byte, 1, 1);
    return xe_byte;
}

void xe_bank_poke(uint8_t bank, uint16_t offset, uint8_t v)
{
    xe_byte = v;
    copy(bank, offset, &xe_byte, 1, 0);
}
