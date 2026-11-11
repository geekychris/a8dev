/*
 * Uranus Lander - sound on POKEY (the original plays a MOD and Paula
 * effects; the ZX Spectrum port's 128K sounds are the model here).
 *
 *   channel 0: the thrust rumble (noise) while the engine burns
 *   channel 1: the crash (a falling noise burst), the landing (a rising
 *              sweep), the low-fuel beep
 *   channel 2: a slow arpeggio in place of the original's MOD (M: on/off)
 */
#include "a8game.h"
#include "sound.h"

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

/* the Spectrum port's arpeggio (MIDI, an octave up for POKEY's range) */
static const uint8_t melody[32] = {
    57, 64, 69, 72, 76, 72, 69, 64,   53, 60, 65, 69, 72, 69, 65, 60,
    55, 62, 67, 71, 74, 71, 67, 62,   52, 59, 64, 67, 71, 67, 64, 59,
};
static uint8_t music_on, mpos, mtimer;
static uint8_t fx, fx_t;               /* channel 1's effect and its frame */
#define FX_NONE  0
#define FX_CRASH 1
#define FX_LAND  2
#define FX_BEEP  3

void snd_init(void)
{
    AUDCTL = 0;
    SKCTL = 3;
    snd_quiet();
}

void snd_quiet(void)
{
    uint8_t ch;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
    fx = FX_NONE;
}

void snd_music(uint8_t on)
{
    music_on = on;
    mpos = mtimer = 0;
    if (!on) AUDC_(2) = 0;
}

void snd_frame(uint8_t thrust, uint8_t crash, uint8_t land, uint8_t low_fuel)
{
    static uint8_t rumble;
    /* the thrust: a low noise that wavers */
    if (thrust) {
        AUDF_(0) = (uint8_t)(0x38 + (++rumble & 7));
        AUDC_(0) = 0x86;
    } else {
        AUDC_(0) = 0;
    }
    if (crash) { fx = FX_CRASH; fx_t = 0; }
    else if (land) { fx = FX_LAND; fx_t = 0; }
    else if (low_fuel && fx == FX_NONE) { fx = FX_BEEP; fx_t = 0; }
    switch (fx) {
    case FX_CRASH:                     /* 45 frames of noise, falling and fading */
        AUDF_(1) = (uint8_t)(20 + fx_t * 2);
        AUDC_(1) = (uint8_t)(0x80 | (15 - fx_t / 3));
        if (++fx_t == 45) fx = FX_NONE;
        break;
    case FX_LAND:                      /* a sweep up, 24 frames */
        AUDF_(1) = (uint8_t)(120 - fx_t * 4);
        AUDC_(1) = (uint8_t)(0xA0 | (12 - fx_t / 3));
        if (++fx_t == 24) fx = FX_NONE;
        break;
    case FX_BEEP:                      /* 5 frames */
        AUDF_(1) = 40;
        AUDC_(1) = 0xAA;
        if (++fx_t == 5) fx = FX_NONE;
        break;
    default:
        AUDC_(1) = 0;
        break;
    }
    if (music_on && !mtimer--) {
        mtimer = 8;
        AUDF_(2) = snd_note_freq[melody[mpos] - 48];
        AUDC_(2) = 0xA5;
        mpos = (uint8_t)((mpos + 1) & 31);
    }
}
