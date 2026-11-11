/*
 * sound.c - Boing Twist's sounds on POKEY, after the original's synthesis:
 * the thud of the bounce (a low noise burst), the click of a band (a short
 * high tone), the whoosh of a flip (noise sweeping down), the solved chord
 * (three notes), and a title tune (new, as on the Spectrum).
 */
#include "a8game.h"
#include "sound.h"

static const a8_note tune_lead[] = {
    { 72, 12 }, { 76, 12 }, { 79, 12 }, { 84, 24 }, { 79, 12 }, { 76, 12 }, { 72, 24 },
    { 74, 12 }, { 77, 12 }, { 81, 12 }, { 86, 24 }, { 81, 12 }, { 77, 12 }, { 74, 24 },
    { 72, 12 }, { 76, 12 }, { 79, 12 }, { 84, 12 }, { 83, 12 }, { 79, 12 }, { 74, 12 }, { 71, 12 },
    { 72, 36 }, { 0, 12 }, { 0, 0 },
};
static const a8_note tune_bass[] = {
    { 48, 24 }, { 55, 24 }, { 48, 24 }, { 55, 24 }, { 50, 24 }, { 57, 24 }, { 50, 24 }, { 57, 24 },
    { 48, 24 }, { 52, 24 }, { 55, 24 }, { 50, 24 }, { 48, 48 }, { 0, 0 },
};

static uint8_t music_on;

void snd_play(uint8_t which)
{
    switch (which) {
    case SFX_THUD:  snd_sfx(3, 200, SND_NOISE, 12, 2, 4); break;
    case SFX_CLICK: snd_sfx(2, 40, SND_PURE, 10, 3, 0); break;
    case SFX_FLIP:  snd_sfx(2, 20, SND_NOISE, 10, 1, 3); break;
    case SFX_SOLVED:
        music_stop();
        music_on = 0;
        snd_note(0, 72, 10);
        snd_note(1, 76, 10);
        snd_sfx(2, snd_note_freq[79 - 48], SND_PURE, 12, 0, 0);
        break;
    case SFX_TICK:  snd_sfx(2, 60, SND_PURE, 6, 3, 0); break;
    case SFX_OFF:   snd_off(0); snd_off(1); snd_off(2); break;
    }
}

void snd_music(uint8_t on)
{
    if (on == music_on) return;
    music_on = on;
    if (on) {
        music_play(0, tune_lead, 6);
        music_play(1, tune_bass, 5);
    } else {
        music_stop();
        snd_off(0);
        snd_off(1);
    }
}
