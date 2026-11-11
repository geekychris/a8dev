/*
 * Lunar Rider - sound on POKEY (the original's sound.c: five effects on
 * Paula's channels 0-2 and a bouncy bass on channel 3).
 *
 * Effects are a frame-by-frame list of (AUDF, AUDC) on POKEY channels 0..2,
 * as on the Amiga each effect has its channel, so a new one only cuts the
 * one on its own channel. The pitches are the Spectrum port's (AY periods,
 * converted: POKEY's 64 kHz divider N = 2P/7 - 1 gives the same note). The
 * bass is the SDK's music player on channel 3.
 */
#include "a8game.h"
#include "sound.h"

typedef struct { uint8_t f, c; } step_t;   /* AUDF, AUDC (distortion | volume) */

#define T(n, v) {n, (uint8_t)(0xA0 | (v))}  /* pure tone */
#define N(n, v) {n, (uint8_t)(0x80 | (v))}  /* noise */
#define B(n, v) {n, (uint8_t)(0xC0 | (v))}  /* buzz (tone + noise) */

static const step_t fx_shoot[] = {T(8, 13), T(8, 10), T(8, 6)};
static const step_t fx_explode[] = {
    N(18, 15), N(24, 14), N(30, 13), N(36, 11), N(42, 10),
    N(48, 8), N(54, 7), N(60, 5), N(72, 4), N(84, 2),
};
static const step_t fx_jump[] = {
    T(52, 12), T(42, 12), T(34, 11), T(29, 10), T(24, 9), T(20, 8), T(17, 7), T(15, 6),
};
static const step_t fx_checkpoint[] = {
    T(34, 13), T(27, 13), T(20, 13), T(17, 13), T(34, 12), T(27, 12), T(20, 12),
    T(17, 12), T(34, 11), T(27, 11), T(20, 11), T(17, 11), T(34, 10), T(27, 10),
    T(20, 10), T(17, 10), T(20, 9), T(17, 9), T(17, 8), T(17, 7),
};
static const step_t fx_death[] = {
    B(10, 14), B(12, 14), B(14, 13), B(16, 12), B(19, 12), B(21, 11), B(24, 10), B(26, 9),
    B(28, 9), B(31, 8), B(33, 7), B(36, 6), B(38, 5), B(40, 5), B(45, 4),
};

/* the original's bass: C3 C3 G3 G3 A3 A3 F3 F3 E3 E3 G3 G3 C3 D3 E3 F3, 7
   frames a note, on channel 3. (Its own few lines: the SDK's music player
   ticks four channels in C, ~2,000 cycles a frame.) */
static const uint8_t bass[16] = {48, 48, 55, 55, 57, 57, 53, 53, 52, 52, 55, 55, 48, 50, 52, 53};
#define BASS_TEMPO 7
#define BASS_VOL   6
static uint8_t bass_on, bass_pos, bass_timer;

static const step_t *cur[3];
static uint8_t left[3];

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

static void play(uint8_t ch, const step_t *s, uint8_t n)
{
    cur[ch] = s;
    left[ch] = (uint8_t)(n + 1);       /* n steps, then silence */
}

void snd_shoot(void)      { play(0, fx_shoot, sizeof fx_shoot / sizeof fx_shoot[0]); }
void snd_explode(void)    { play(1, fx_explode, sizeof fx_explode / sizeof fx_explode[0]); }
void snd_jump(void)       { play(2, fx_jump, sizeof fx_jump / sizeof fx_jump[0]); }
void snd_checkpoint(void) { play(2, fx_checkpoint, sizeof fx_checkpoint / sizeof fx_checkpoint[0]); }
void snd_death(void)      { play(1, fx_death, sizeof fx_death / sizeof fx_death[0]); }

void snd_init(void)
{
    AUDCTL = 0;                         /* 64 kHz clock, 4 channels */
    SKCTL = 3;
    snd_silence();
}

void snd_music(uint8_t on)
{
    bass_on = on;
    bass_pos = 0;
    bass_timer = 0;
    if (!on) AUDC_(3) = 0;
}

void snd_silence(void)
{
    uint8_t ch;
    bass_on = 0;
    for (ch = 0; ch < 4; ++ch) {
        AUDC_(ch) = 0;
        if (ch < 3) left[ch] = 0;
    }
}

void snd_tick(void)
{
    uint8_t ch;
    if (bass_on && !bass_timer--) {
        bass_timer = BASS_TEMPO - 1;
        AUDF_(3) = snd_note_freq[bass[bass_pos] - 48];
        AUDC_(3) = (uint8_t)(SND_PURE | BASS_VOL);
        bass_pos = (uint8_t)((bass_pos + 1) & 15);
    }
    for (ch = 0; ch < 3; ++ch) {
        if (!left[ch]) continue;
        if (!--left[ch]) {
            AUDC_(ch) = 0;
            continue;
        }
        AUDF_(ch) = cur[ch]->f;
        AUDC_(ch) = cur[ch]->c;
        ++cur[ch];
    }
}
