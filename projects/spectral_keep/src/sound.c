/*
 * Spectral Keep: sound on POKEY (the SDK's effects and music player, ticked
 * in the vertical blank: disp.s). The original composes its music from a
 * seed for each keep; port: a short loop for each keep and the title on two
 * channels (src/tunes.c), and the original's effects (its table: a jump, a
 * landing, a pickup, a relic, a key, a gate, a death, a door, talk, a bark,
 * a keep won, the start, a menu blip) as POKEY sweeps on the other two.
 */
#include "a8game.h"
#include "keep.h"
#include "sndreq.h"

extern const a8_note tune_lead[4][33], tune_bass[4][17];   /* tunes.c (main memory: the VBI plays them) */

/* freq, distortion, volume, decay a frame, sweep a frame */
static const uint8_t fx[SFX_COUNT][5] = {
    {90, SND_PURE, 10, 1, (uint8_t)-6},    /* jump: a rising chirp */
    {200, SND_NOISE, 8, 2, 0},             /* land: a thud */
    {40, SND_PURE, 12, 1, (uint8_t)-2},    /* pickup */
    {30, SND_PURE, 14, 1, (uint8_t)-1},    /* relic: long and bright */
    {50, SND_PURE, 12, 1, (uint8_t)-3},    /* key */
    {160, SND_BUZZ, 12, 1, 3},             /* gate: a grinding fall */
    {60, SND_PURE, 14, 1, 6},              /* death: falling */
    {120, SND_NOISE, 6, 1, 0},             /* door */
    {70, SND_PURE, 8, 2, 2},               /* talk */
    {140, SND_BUZZ, 12, 3, 0},             /* bark */
    {80, SND_PURE, 14, 1, (uint8_t)-2},    /* the keep won */
    {100, SND_PURE, 12, 1, (uint8_t)-4},   /* start */
    {60, SND_PURE, 8, 4, 0},               /* blip */
};

static uint8_t music_on = 1, cur_track = 0xFF;

void snd_service(void)
{
    uint8_t i, ch = 0;
    uint16_t req = sfx_req;
    sfx_req = 0;
    for (i = 0; i < SFX_COUNT && req; ++i, req >>= 1)
        if (req & 1) {
            const uint8_t *f = fx[i];
            snd_sfx(ch, f[0], f[1], f[2], f[3], (int8_t)f[4]);
            ch = 3;                         /* a second at once: the other channel */
        }
    if (music_on_req != music_on) {
        music_on = music_on_req;
        if (!music_on) music_stop();
        else cur_track = 0xFF;
    }
    if (music_req != 0xFF) {
        if (music_req != cur_track) {
            cur_track = music_req;
            music_stop();
            if (music_on && cur_track < 4) {
                music_play(1, tune_lead[cur_track], 6);
                music_play(2, tune_bass[cur_track], 5);
            }
        }
        music_req = 0xFF;
    }
}
