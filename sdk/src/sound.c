/* a8game.h: POKEY tones, effects and a small music player. */
#include "a8game.h"

#define AUDF(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

extern void (*a8_frame_sound)(void);

typedef struct {
    const a8_note *start, *pos;
    uint8_t left, vol;
} track_t;

typedef struct {
    uint8_t freq, dist, vol, decay, on;
    int8_t sweep;
} sfx_t;

static track_t tracks[4];
static sfx_t sfx[4];

void sound_init(void)
{
    uint8_t i;
    AUDCTL = 0;
    SKCTL = 3;
    for (i = 0; i < 4; ++i) { AUDC(i) = 0; tracks[i].start = 0; sfx[i].on = 0; }
    a8_frame_sound = sound_tick;
}

void snd_tone(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol)
{
    AUDF(ch) = freq;
    AUDC(ch) = dist | (vol & 15);
}

void snd_note(uint8_t ch, uint8_t note, uint8_t vol)
{
    if (note < 48 || note > 107) { AUDC(ch) = 0; return; }
    snd_tone(ch, snd_note_freq[note - 48], SND_PURE, vol);
}

void snd_off(uint8_t ch)
{
    AUDC(ch) = 0;
    sfx[ch].on = 0;
}

void snd_sfx(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol, uint8_t decay, int8_t sweep)
{
    sfx_t *s = &sfx[ch];
    if (!a8_frame_sound) sound_init();
    s->freq = freq; s->dist = dist; s->vol = vol; s->decay = decay ? decay : 1;
    s->sweep = sweep; s->on = 1;
    snd_tone(ch, freq, dist, vol);
}

void music_play(uint8_t ch, const a8_note *track, uint8_t vol)
{
    track_t *t = &tracks[ch];
    if (!a8_frame_sound) sound_init();
    t->start = t->pos = track;
    t->left = 0;
    t->vol = vol;
}

void music_stop(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) {
        if (tracks[i].start && !sfx[i].on) AUDC(i) = 0;
        tracks[i].start = 0;
    }
}

void sound_tick(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) {
        track_t *t = &tracks[i];
        sfx_t *s = &sfx[i];
        if (t->start && t->left == 0) {
            if (t->pos->len == 0) t->pos = t->start;     /* {0,0}: loop */
            t->left = t->pos->len;
            if (!s->on) snd_note(i, t->pos->note, t->vol);
            ++t->pos;
        }
        if (t->start) --t->left;
        if (s->on) {
            if (s->vol <= s->decay) {
                s->on = 0;
                AUDC(i) = 0;
            } else {
                s->vol -= s->decay;
                s->freq += (uint8_t)s->sweep;
                snd_tone(i, s->freq, s->dist, s->vol);
            }
        }
    }
}
