/* temporary: how long the original's logic takes per step on the 6502 */
#include "a8game.h"
#include "a8log.h"
#include "keep.h"
void snd_play(uint8_t sfx) { (void)sfx; }
void snd_music(uint8_t track) { (void)track; }
void snd_duck(int16_t ticks) { (void)ticks; }
void snd_music_enable(uint8_t on) { (void)on; }
#define RTCLOK frames16()
static uint16_t frames16(void)
{
    uint8_t lo, hi;
    do { hi = *(volatile uint8_t *)0x13; lo = *(volatile uint8_t *)0x14; } while (hi != *(volatile uint8_t *)0x13);
    return ((uint16_t)hi << 8) | lo;
}
int main(void)
{
    uint16_t i, k;
    uint16_t t0, t1;
#ifdef MATHCHECK
    { extern void math_check(void); math_check(); }
#endif
    t0 = RTCLOK;
    game_init();
    t1 = RTCLOK;
    a8_log("sk: init frames=%u\n", (uint16_t)(t1 - t0));
    game_step(0, B_A);
    t0 = RTCLOK;
    game_step(0, B_A);
    t1 = RTCLOK;
    a8_log("sk: start frames=%u state=%u\n", (uint16_t)(t1 - t0), G.state);
    for (k = 0; k < 6; ++k) {
        uint16_t held = k & 1 ? B_UP : B_LEFT;
        t0 = RTCLOK;
        for (i = 0; i < 50; ++i) game_step(held, i == 10 ? B_A : 0);
        t1 = RTCLOK;
        a8_log("sk: 50 steps frames=%u room=%s nact=%u x=%d z=%d\n", (uint16_t)(t1 - t0), R.room->id, R.nact,
               R.player >= 0 ? R.act[R.player].body->px : 0, R.player >= 0 ? R.act[R.player].body->pz : 0);
    }
    a8_log("sk: ready\n");
    for (k = 0;; ++k) {
        uint16_t held = (k >> 6) & 1 ? B_UP : B_LEFT;
        game_step(held, (k & 31) == 0 ? B_A : 0);
    }
}
