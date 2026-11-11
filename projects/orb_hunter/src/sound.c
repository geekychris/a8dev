/*
 * Orb Hunter - the original's eight effects on POKEY (the original plays
 * them with Paula samples and a MOD; here each is a few frames of tone or
 * noise, on channels 0-2, the newest taking the quietest channel), and a
 * slow bass line on channel 3 while playing.
 */
#include "a8game.h"
#include "game.h"

#define AUDF_(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC_(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

typedef struct { uint8_t f, c; } step_t;
#define T(n, v) {n, (uint8_t)(0xA0 | (v))}
#define N(n, v) {n, (uint8_t)(0x80 | (v))}

static const step_t fx_shoot[] = {T(20, 10), T(24, 8), T(28, 5)};
static const step_t fx_hit[] = {N(30, 10), N(40, 6)};
static const step_t fx_explode[] = {N(60, 14), N(70, 12), N(80, 10), N(90, 8), N(100, 6), N(110, 4), N(120, 2)};
static const step_t fx_powerup[] = {T(60, 12), T(50, 12), T(40, 12), T(30, 12), T(25, 10), T(20, 8), T(16, 6)};
static const step_t fx_jump[] = {T(80, 8), T(70, 7), T(60, 6), T(50, 4)};
static const step_t fx_player_hit[] = {T(120, 14), N(100, 12), T(140, 10), N(120, 8)};
static const step_t fx_missile[] = {N(10, 12), N(14, 10), N(18, 8), N(22, 6)};
static const step_t fx_door[] = {T(100, 12), T(90, 12), T(100, 10), T(90, 10), T(80, 8)};

static const step_t *cur[3];
static uint8_t left[3];

static void play(const step_t *s, uint8_t n)
{
    uint8_t ch, best = 0;
    for (ch = 1; ch < 3; ++ch)                 /* a free channel, else the most finished */
        if (left[ch] < left[best]) best = ch;
    cur[best] = s;
    left[best] = (uint8_t)(n + 1);
}

#define PLAY(fx) play(fx, sizeof fx / sizeof fx[0])
void sfx_shoot(void)      { PLAY(fx_shoot); }
void sfx_hit(void)        { PLAY(fx_hit); }
void sfx_explode(void)    { PLAY(fx_explode); }
void sfx_powerup(void)    { PLAY(fx_powerup); }
void sfx_jump(void)       { PLAY(fx_jump); }
void sfx_player_hit(void) { PLAY(fx_player_hit); }
void sfx_missile(void)    { PLAY(fx_missile); }
void sfx_door(void)       { PLAY(fx_door); }

/* a bass line in the caverns: A2 A2 C3 A2 G2 G2 E2 G2 (an octave up) */
static const uint8_t bass[8] = {57, 57, 60, 57, 55, 55, 52, 55};
static uint8_t bass_on, bpos, btimer;

void snd_init(void)
{
    uint8_t ch;
    AUDCTL = 0;
    SKCTL = 3;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
}

void snd_music(uint8_t on)
{
    bass_on = on;
    bpos = btimer = 0;
    if (!on) AUDC_(3) = 0;
}

void snd_quiet(void)
{
    uint8_t ch;
    for (ch = 0; ch < 4; ++ch) AUDC_(ch) = 0;
    left[0] = left[1] = left[2] = 0;
    bass_on = 0;
}

void snd_tick(void)
{
    uint8_t ch;
    for (ch = 0; ch < 3; ++ch) {
        if (!left[ch]) continue;
        if (!--left[ch]) { AUDC_(ch) = 0; continue; }
        AUDF_(ch) = cur[ch]->f;
        AUDC_(ch) = cur[ch]->c;
        ++cur[ch];
    }
    if (bass_on && !btimer--) {
        btimer = 11;
        AUDF_(3) = snd_note_freq[bass[bpos] - 48];
        AUDC_(3) = 0xA4;
        bpos = (uint8_t)((bpos + 1) & 7);
    }
}
