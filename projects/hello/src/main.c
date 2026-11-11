/*
 * hello - the a8dev first program: text, a player/missile sprite, a POKEY
 * sound and the harness log.
 *
 * GRAPHICS 0 style text screen (sdk gfx_init), a ball (player 0) that
 * bounces, a beep at every bounce, joystick moves a second sprite, FIRE
 * plays a note. Logs "hello: ready", "hello: bounce n=N x=X" and, after 4
 * bounces, A8:DONE (./a8 test hello passes on it).
 */
#include "a8game.h"
#include "a8gfx.h"
#include "a8pmg.h"
#include "a8log.h"

static const uint8_t ball[8] = { 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C };
static const uint8_t ship[8] = { 0x18, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x24, 0x24 };

/* globals so the debugger and tests can read them (emu_read_mem("bx")) */
uint8_t bx = 60, by = 80, sx = 120, sy = 180;
uint8_t bounces;

int main(void)
{
    uint8_t in, pressed;
    int8_t dx = 1, dy = 1;

    gfx_init(GFX_TEXT, 0);
    gfx_color(2, A8_COLOR(9, 2));          /* dark blue background */
    gfx_color(1, 0x0E);                    /* white text */
    gfx_border(A8_COLOR(9, 0));
    text_print_at(2, 1, "HELLO FROM A8DEV");
    text_print_at(2, 3, "Atari 8-bit, cc65, Atari800 harness");
    text_print_at(2, 21, "Joystick moves the ship, FIRE beeps");

    pmg_init();
    pmg_color(0, A8_COLOR(2, 10));         /* orange ball */
    pmg_color(1, A8_COLOR(12, 12));        /* green ship */
    pmg_set(1, sy, ship, 8);

    sound_init();
    rnd_seed(1234);
    frame_init();
    a8_log("hello: ready pal=%u\n", frame_is_pal() ? 1 : 0);

    for (;;) {
        frame_wait();
        bx += dx;
        by += dy;
        if (bx <= PMG_X0 || bx >= PMG_X0 + 160 - 8) {
            dx = -dx;
            ++bounces;
            snd_sfx(0, 60, SND_PURE, 12, 2, 1);
            a8_log("hello: bounce n=%u x=%u\n", bounces, bx);
            if (bounces == 4) a8_done();
        }
        if (by <= PMG_Y0 || by >= PMG_Y0 + 192 - 8) dy = -dy;
        pmg_x(0, bx);
        pmg_set(0, by, ball, 8);

        in = input_read();
        pressed = input_pressed();
        if ((in & IN_LEFT) && sx > PMG_X0) --sx;
        if ((in & IN_RIGHT) && sx < PMG_X0 + 160 - 8) ++sx;
        pmg_x(1, sx);
        if (pressed & IN_FIRE) {
            snd_note(1, 72, 10);           /* C5 */
            a8_log("hello: fire x=%u\n", sx);
        }
        if (pressed & (IN_LEFT | IN_RIGHT)) a8_log("hello: move x=%u\n", sx);
        if (!(in & IN_FIRE) && !(pressed & IN_FIRE)) snd_off(1);
        if (pressed & IN_START) a8_log("hello: start\n");
    }
}
