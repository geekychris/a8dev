/* a8game.h: POKEY tones, effects and the music player's setup (the per-frame
   part, sound_tick, is sound_tick.s; its state is byte arrays there). */
#include "a8game.h"

#define AUDF(ch) (*(volatile uint8_t *)(0xD200 + ((ch) << 1)))
#define AUDC(ch) (*(volatile uint8_t *)(0xD201 + ((ch) << 1)))

extern void (*a8_frame_sound)(void);
extern uint8_t snd_active;
extern uint8_t trk_on[4], trk_start_lo[4], trk_start_hi[4], trk_pos_lo[4], trk_pos_hi[4];
extern uint8_t trk_left[4], trk_vol[4];
extern uint8_t sfx_on[4], sfx_freq[4], sfx_dist[4], sfx_vol[4], sfx_decay[4], sfx_sweep[4];

static const uint8_t bit[4] = { 1, 2, 4, 8 };

void sound_init(void)
{
    uint8_t i;
    AUDCTL = 0;
    SKCTL = 3;
    for (i = 0; i < 4; ++i) { AUDC(i) = 0; trk_on[i] = 0; sfx_on[i] = 0; }
    snd_active = 0;
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
    sfx_on[ch] = 0;
    if (!trk_on[ch]) snd_active &= (uint8_t)~bit[ch];
}

void snd_sfx(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol, uint8_t decay, int8_t sweep)
{
    if (!a8_frame_sound) sound_init();
    sfx_freq[ch] = freq;
    sfx_dist[ch] = dist;
    sfx_vol[ch] = vol;
    sfx_decay[ch] = decay ? decay : 1;
    sfx_sweep[ch] = (uint8_t)sweep;
    sfx_on[ch] = 1;
    snd_active |= bit[ch];
    snd_tone(ch, freq, dist, vol);
}

void music_play(uint8_t ch, const a8_note *track, uint8_t vol)
{
    if (!a8_frame_sound) sound_init();
    trk_on[ch] = 0;                       /* (no tick sees a half-set channel) */
    trk_start_lo[ch] = trk_pos_lo[ch] = (uint8_t)(uint16_t)track;
    trk_start_hi[ch] = trk_pos_hi[ch] = (uint8_t)((uint16_t)track >> 8);
    trk_left[ch] = 0;
    trk_vol[ch] = vol;
    trk_on[ch] = 1;
    snd_active |= bit[ch];
}

void music_stop(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) {
        if (trk_on[i] && !sfx_on[i]) AUDC(i) = 0;
        trk_on[i] = 0;
        if (!sfx_on[i]) snd_active &= (uint8_t)~bit[i];
    }
}
