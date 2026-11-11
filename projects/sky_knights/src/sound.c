/*
 * Sky Knights - sound on POKEY (the original plays Paula samples: a flap
 * and a smash, and its effects). Channel 0: the wing beat while flapping
 * (a short noise every 6 frames, as the Spectrum port's); channel 1: the
 * joust, the pod, a lost knight, the wave fanfare.
 */
#include "a8game.h"
#include "game.h"
#include "sound.h"

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

typedef struct { uint8_t f, c; } step_t;
#define T(n, v) {n, (uint8_t)(0xA0 | (v))}
#define N(n, v) {n, (uint8_t)(0x80 | (v))}

static const step_t fx_kill[] = {N(20, 14), T(40, 12), T(30, 12), T(20, 10), T(15, 8)};
static const step_t fx_egg[] = {T(40, 10), T(30, 10), T(20, 10), T(15, 8)};
static const step_t fx_die[] = {T(60, 14), T(80, 13), T(100, 12), T(120, 11), T(140, 10), T(160, 8),
                                T(180, 6), T(200, 4)};
static const step_t fx_wave[] = {T(60, 12), T(60, 12), T(47, 12), T(47, 12), T(40, 12), T(40, 12),
                                 T(30, 12), T(30, 12), T(30, 10), T(30, 8)};
static const step_t *cur;
static uint8_t left, flap_t;

void snd_init(void)
{
    uint8_t ch;
    AUDCTL = 0;
    SKCTL = 3;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
}

static void play(const step_t *s, uint8_t n)
{
    cur = s;
    left = (uint8_t)(n + 1);
}

void snd_events(uint8_t ev)
{
    if (ev & EV_DIE) play(fx_die, sizeof fx_die / 2);
    else if (ev & EV_KILL) play(fx_kill, sizeof fx_kill / 2);
    else if (ev & EV_EGG) play(fx_egg, sizeof fx_egg / 2);
    else if (ev & EV_WAVE) play(fx_wave, sizeof fx_wave / 2);
    /* the wing beat */
    if (ev & EV_FLAP) {
        if (!flap_t) {
            AUDF_(0) = 0x60;
            AUDC_(0) = 0x84;
            flap_t = 6;
        }
    }
    if (flap_t) {
        if (--flap_t == 4) AUDC_(0) = 0;
    }
    if (left) {
        if (!--left) AUDC_(1) = 0;
        else {
            AUDF_(1) = cur->f;
            AUDC_(1) = cur->c;
            ++cur;
        }
    }
}

void snd_quiet(void)
{
    uint8_t ch;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
    left = flap_t = 0;
}
