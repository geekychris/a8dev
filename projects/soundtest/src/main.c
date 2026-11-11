/*
 * soundtest - the SDK's POKEY player (sound.c + sound_tick.s), checked from
 * the host frame by frame against a model of the original C player
 * (test.py), and timed (idle vs. busy).
 *
 * 30 idle frames ("soundtest: idle"), then two music tracks (channels 0
 * and 2, one looping) and an effect on channel 1, an effect taking over
 * channel 0 at tick 20, music_stop at tick 60. Logs "t N" after each frame.
 */
#include "a8game.h"
#include "a8log.h"

static const a8_note track_a[] = { {60, 3}, {64, 2}, {0, 2}, {67, 4}, {0, 0} };
static const a8_note track_b[] = { {48, 5}, {55, 1}, {200, 3}, {0, 0} };

int main(void)
{
    uint8_t t;
    sound_init();
    frame_init();
    for (t = 0; t < 30; ++t) frame_wait();
    a8_log("soundtest: idle\n");
    frame_wait();
    music_play(0, track_a, 8);
    music_play(2, track_b, 6);
    snd_sfx(1, 100, SND_PURE, 12, 1, 3);
    a8_log("soundtest: start\n");
    for (t = 1; t < 80; ++t) {
        frame_wait();
        a8_log("t %u\n", t);
        if (t == 20) snd_sfx(0, 40, SND_NOISE, 15, 4, -2);
        if (t == 60) music_stop();
    }
    a8_done();
    for (;;) frame_wait();
}
