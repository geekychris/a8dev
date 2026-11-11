/*
 * Rotoball - sound on POKEY. The original has two samples, made by its
 * sound.c from sine partials: CLANG (a goal: a long inharmonic metal hit)
 * and PING (a pickup or a tackle: a short high blip). Here CLANG is two
 * pure tones a little out of tune on channels 0 and 1 (beating, as a bell
 * does), decaying over 0.4 s; PING a high tone on channel 2 for 70 ms;
 * full time three falling notes.
 */
#include "a8game.h"
#include "game.h"
#include "sound.h"

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

static uint8_t clang_t, ping_t, full_t;

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
    clang_t = ping_t = full_t = 0;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
}

void snd_events(uint8_t ev)
{
    if (ev & (EV_GOAL1 | EV_GOAL2)) clang_t = 20;
    if (ev & (EV_PICKUP | EV_TACKLE)) ping_t = 4;
    if (ev & EV_FULL) full_t = 48;
}

void snd_tick(void)
{
    if (clang_t) {
        uint8_t v = (uint8_t)(clang_t >> 1);
        --clang_t;
        AUDF_(0) = 40;
        AUDC_(0) = (uint8_t)(0xA0 | v);
        AUDF_(1) = 57;
        AUDC_(1) = (uint8_t)(0xA0 | (v > 2 ? v - 2 : 0));
    } else if (!full_t) {
        AUDC_(0) = 0;
        AUDC_(1) = 0;
    }
    if (ping_t) {
        --ping_t;
        AUDF_(2) = 12;
        AUDC_(2) = (uint8_t)(0xA0 | (ping_t * 3 + 2));
    } else {
        AUDC_(2) = 0;
    }
    if (full_t) {
        --full_t;
        AUDF_(0) = full_t >= 32 ? 60 : full_t >= 16 ? 76 : 91;
        AUDC_(0) = (uint8_t)(0xA0 | ((full_t & 15) >> 1));
    }
}
