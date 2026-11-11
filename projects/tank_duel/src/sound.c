/*
 * Tank Duel: sound on POKEY (the SDK's effects). The original synthesises
 * its effects (it has no music); port: POKEY sweeps in the same spirit:
 * noise for shots and explosions, a whistle for a lob, a ring for a
 * glance, a clank for a hit, a rising run for a round won, a falling tone
 * for defeat.
 */
#include "a8game.h"
#include "td.h"
#include "sound.h"

void snd_event(uint8_t type)
{
    switch (type) {
    case TDE_FIRE:        snd_sfx(0, 40, SND_NOISE, 12, 1, 3); break;
    case TDE_EFIRE:       snd_sfx(1, 60, SND_NOISE, 8, 1, 3); break;
    case TDE_LOB:         snd_sfx(2, 30, SND_PURE, 8, 1, 2); break;
    case TDE_GLANCE:      snd_sfx(3, 20, SND_PURE, 12, 1, 0); break;
    case TDE_BOUNCE:      snd_sfx(3, 50, SND_PURE, 6, 2, 0); break;
    case TDE_HIT:         snd_sfx(3, 90, SND_BUZZ, 12, 2, 0); break;
    case TDE_KILL:
    case TDE_CRATE_BREAK: snd_sfx(1, 200, SND_NOISE, 15, 1, 2); break;
    case TDE_HURT:        snd_sfx(2, 160, SND_BUZZ, 14, 2, 1); break;
    case TDE_SPLASH:      snd_sfx(2, 120, SND_NOISE, 9, 1, 1); break;
    case TDE_ROUND_CLEAR: snd_sfx(0, 80, SND_PURE, 14, 1, (int8_t)-2); break;
    case TDE_DEAD:        snd_sfx(0, 40, SND_PURE, 15, 1, 4); break;
    }
}
