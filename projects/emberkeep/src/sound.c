/*
 * Emberkeep - sound on POKEY. The original's audio.cpp only logs (no
 * sound); here: a chime for a rune, a glug for oil, a fall for a death,
 * a rising run when the portal opens and when you go through, and a soft
 * tick every second while the lantern is under a quarter.
 */
#include "a8game.h"
#include "game.h"
#include "sound.h"

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

typedef struct { uint8_t f, c; } step_t;
#define T(n, v) {n, (uint8_t)(0xA0 | (v))}
#define N(n, v) {n, (uint8_t)(0x80 | (v))}

static const step_t fx_rune[] = {T(40, 12), T(30, 12), T(20, 10), T(20, 8), T(15, 6), T(15, 4)};
static const step_t fx_oil[] = {T(120, 10), T(100, 10), T(130, 8), T(110, 8), T(140, 6)};
static const step_t fx_die[] = {N(30, 14), T(80, 12), T(100, 12), T(120, 10), T(140, 10), T(160, 8),
                                T(180, 6), T(200, 4), T(220, 2)};
static const step_t fx_open[] = {T(80, 10), T(64, 10), T(53, 10), T(40, 10), T(32, 8), T(32, 6)};
static const step_t fx_portal[] = {T(60, 12), T(47, 12), T(40, 12), T(30, 12), T(23, 12), T(20, 10),
                                   T(20, 8), T(20, 6), T(20, 4)};
static const step_t *cur;
static uint8_t left, wait, low_t;

void snd_init(void)
{
    uint8_t ch;
    AUDCTL = 0;
    SKCTL = 3;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
}

void snd_quiet(void)
{
    uint8_t ch;
    left = 0;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
}

static void play(const step_t *s, uint8_t n)
{
    cur = s;
    left = n;
    wait = 0;
}

void snd_events(uint8_t ev)
{
    if (ev & EV_DIE) play(fx_die, sizeof fx_die / 2);
    else if (ev & EV_PORTAL) play(fx_portal, sizeof fx_portal / 2);
    else if (ev & EV_OPEN) play(fx_open, sizeof fx_open / 2);
    else if (ev & EV_RUNE) play(fx_rune, sizeof fx_rune / 2);
    else if (ev & EV_OIL) play(fx_oil, sizeof fx_oil / 2);
}

void snd_tick(uint8_t low)
{
    if (left) {
        if (wait) {
            --wait;
        } else {
            AUDF_(0) = cur->f;
            AUDC_(0) = cur->c;
            ++cur;
            --left;
            wait = 2;
        }
    } else {
        AUDC_(0) = 0;
    }
    /* the low lantern's tick */
    if (low) {
        if (++low_t >= 50) low_t = 0;
        AUDF_(1) = 200;
        AUDC_(1) = low_t < 2 ? 0xA4 : 0;
    } else {
        AUDC_(1) = 0;
    }
}
