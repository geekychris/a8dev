/*
 * __NAME__ - made from sdk/template (./a8 new).
 *
 * Main-loop skeleton: 50 Hz loop on the vertical blank (frame_wait), a text
 * screen, a player/missile sprite moved with the joystick (or W/A/S/D),
 * FIRE (or SPACE) beeps, and a8_log lines the harness and test.py wait for.
 */
#include "a8game.h"
#include "a8gfx.h"
#include "a8pmg.h"
#include "a8log.h"

static const uint8_t shape[8] = { 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };

int main(void)
{
    uint8_t x = 120, y = 120, in, pressed;

    gfx_init(GFX_TEXT, 0);
    gfx_color(2, A8_COLOR(0, 0));
    gfx_color(1, 0x0C);
    text_print_at(1, 1, "__NAME__");
    text_print_at(1, 22, "Joystick/WASD move, FIRE/SPACE beep");

    pmg_init();
    pmg_color(0, A8_COLOR(12, 10));
    pmg_x(0, x);
    pmg_set(0, y, shape, 8);
    sound_init();
    frame_init();
    a8_log("__NAME__: ready\n");

    for (;;) {
        frame_wait();
        in = input_read();
        pressed = input_pressed();
        if (in & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN)) {
            if ((in & IN_LEFT) && x > PMG_X0) --x;
            if ((in & IN_RIGHT) && x < PMG_X0 + 152) ++x;
            if ((in & IN_UP) && y > PMG_Y0) --y;
            if ((in & IN_DOWN) && y < PMG_Y0 + 184) ++y;
            pmg_x(0, x);
            pmg_set(0, y, shape, 8);
            a8_log("__NAME__: move x=%u y=%u\n", x, y);
        }
        if (pressed & IN_FIRE) {
            snd_sfx(0, 40, SND_PURE, 12, 1, 0);
            a8_log("__NAME__: fire frame=%u\n", frame_count);
        }
        if (pressed & IN_START) a8_log("__NAME__: start\n");
    }
    return 0;
}
