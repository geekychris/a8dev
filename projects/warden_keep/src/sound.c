/*
 * Warden Keep: sound on POKEY (the SDK's effects and music player). The
 * original has none (its README lists audio as follow-up work); as in the
 * Spectrum port: a slow tune in A minor on the title, and effects for a
 * step, a push, a bump, a jewel, a room won, the clock's last ten seconds
 * and time up.
 */
#include "a8game.h"
#include "game.h"
#include "sound.h"

/* MIDI notes: A minor, a bar of 4 x 12 frames */
static const a8_note tune[] = {
    {69, 24}, {72, 12}, {76, 12}, {74, 24}, {72, 12}, {71, 12},
    {69, 24}, {64, 12}, {67, 12}, {69, 36}, {0, 12},
    {72, 24}, {74, 12}, {76, 12}, {77, 24}, {76, 12}, {74, 12},
    {72, 24}, {71, 12}, {68, 12}, {69, 36}, {0, 12},
    {0, 0}};
static const a8_note bass[] = {
    {57, 48}, {53, 48}, {57, 48}, {52, 48},
    {57, 48}, {50, 48}, {52, 48}, {57, 48},
    {0, 0}};

void snd_title(void)
{
    sound_init();
    music_play(1, tune, 6);
    music_play(2, bass, 4);
}

void snd_quiet(void)
{
    sound_init();
}

void snd_events(uint8_t ev, uint8_t seconds)
{
    if (ev & EV_TIMEUP) snd_sfx(0, 60, SND_PURE, 12, 1, 4);
    else if (ev & EV_CLEAR) snd_sfx(0, 80, SND_PURE, 12, 1, -3);
    else if (ev & EV_JEWEL) snd_sfx(0, 30, SND_PURE, 12, 1, -1);
    else if (ev & EV_PUSH) snd_sfx(0, 40, SND_NOISE, 10, 1, 0);
    else if (ev & EV_BUMP) snd_sfx(0, 200, SND_BUZZ, 10, 2, 0);
    else if (ev & EV_STEP) snd_sfx(0, 120, SND_NOISE, 5, 1, 0);
    if ((ev & EV_SECOND) && seconds <= 10 && seconds) snd_sfx(3, 50, SND_PURE, 6, 2, 0);
}
