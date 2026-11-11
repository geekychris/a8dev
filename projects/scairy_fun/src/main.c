/*
 * ScAIry Fun (Atari edition): the front end. (first cut: the road)
 */
#pragma static-locals(on)
#include <stdint.h>
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "sc.h"
#include "render.h"

extern void display_install(void);
extern void os_off(void);
extern uint8_t vbi_count;

static void logu(const char *k, uint16_t v) { a8_log_s(k); a8_log_u(v); }

int main(void)
{
    static uint8_t in, f;
    os_off();                             /* (the renderer runs under the OS ROM) */
    render_init();
    COLOR0 = 0x1A; COLOR1 = 0x0E; COLOR2 = 0x84; COLOR3 = 0x46; COLOR4 = 0;
    display_install();
    SDLSTL = (uint16_t)render_dl();
    SDMCTL = 0x22;
    sound_init();
    frame_init();
    w.ai_perfect = 1;
    world_init();
    world_start_game();
    a8_log_s("scairy: ready\n");
    for (;;) {
        f = frame_wait();
        in = 0;
        if (w.frame & 1 || 1) world_update(in);
        render_frame();
        if ((w.frame & 63) == 0) { logu("scairy: seg=", seg_of(w.car.z)); logu(" vbi=", vbi_count); a8_log_c('\n'); }
    }
}
