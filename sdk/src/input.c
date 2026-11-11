/* a8game.h: joysticks, console keys, keyboard. */
#include "a8game.h"

uint8_t input_last;
static uint8_t prev;

uint8_t joy_read(uint8_t port)
{
    uint8_t s = (&STICK0)[port], r = 0;
    if (!(s & 1)) r |= IN_UP;
    if (!(s & 2)) r |= IN_DOWN;
    if (!(s & 4)) r |= IN_LEFT;
    if (!(s & 8)) r |= IN_RIGHT;
    if (!(&STRIG0)[port]) r |= IN_FIRE;
    return r;
}

uint8_t console_read(void)
{
    return (uint8_t)((~CONSOL & 7) << 5);       /* START, SELECT, OPTION -> bits 5..7 */
}

uint8_t key_peek(void) { return CH; }

uint8_t key_get(void)
{
    uint8_t c = CH;
    if (c != KEY_NONE) CH = KEY_NONE;
    return c;
}

/* key held now (POKEY: one key at a time) -> IN_ bits */
static uint8_t keys_held(void)
{
    if (SKSTAT & 4) return 0;                   /* bit 2 = 0 while a key is down */
    switch (KBCODE & 0x3F) {
    case KEY_A: case 6:  return IN_LEFT;        /* A, + (left arrow) */
    case KEY_D: case 7:  return IN_RIGHT;       /* D, * (right arrow) */
    case KEY_W: case 14: return IN_UP;          /* W, - (up arrow) */
    case KEY_S: case 15: return IN_DOWN;        /* S, = (down arrow) */
    case KEY_SPACE:      return IN_FIRE;
    case KEY_RETURN:     return IN_START;
    }
    return 0;
}

uint8_t input_read(void)
{
    prev = input_last;
    input_last = joy_read(0) | console_read() | keys_held();
    return input_last;
}

uint8_t input_pressed(void)
{
    return (uint8_t)(input_last & ~prev);
}

static const char ascii_tab[64] = {
    'L', 'J', 0, 0, 0, 'K', 0, 0, 'O', 0, 'P', 'U', '\n', 'I', '-', '=',
    'V', 0, 'C', 0, 0, 'B', 'X', 'Z', '4', 0, '3', '6', 0, '5', '2', '1',
    ',', ' ', '.', 'N', 0, 'M', '/', 0, 'R', 0, 'E', 'Y', 0, 'T', 'W', 'Q',
    '9', 0, '0', '7', 0, '8', 0, 0, 'F', 'H', 'D', 0, 0, 'G', 'S', 'A',
};

char key_ascii(uint8_t code)
{
    if (code == KEY_NONE) return 0;
    return ascii_tab[code & 0x3F];
}
