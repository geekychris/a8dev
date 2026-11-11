/*
 * The a8dev arcade menu: the program the arcade disk boots into.
 *
 * Lists every game (build/gen/games.h, made from the projects' arcade.json
 * by ./a8 arcade), with the selected game's description and controls.
 * Joystick / W S / arrows move, FIRE / START / RETURN play. The disk's boot
 * loader (sdk/boot/boot.s, resident at $0700) loads the game's XEX from the
 * disk: directory entry i + 1 = game i. RESET comes back here.
 *
 * Log: "arcade: menu ready sel=N count=N", "arcade: sel=N", "arcade: run n=N".
 */
#include <string.h>
#include "a8game.h"
#include "a8gfx.h"
#include "a8log.h"
#include "games.h"

#define LIST_Y   4
#define LIST_ROWS 12
#define RUN_ENTRY ((void __fastcall__ (*)(uint8_t))0x0709)

static uint8_t sel, top;

static void clear_row(uint8_t y) { memset(gfx_screen + (uint16_t)y * 40, 0, 40); }

static void print_inv(uint8_t x, uint8_t y, const char *s, uint8_t inv)
{
    uint8_t *p = gfx_screen + (uint16_t)y * 40 + x;
    while (*s) *p++ = a8_screen_code(*s++) | (inv ? 0x80 : 0);
}

/* word-wrap s into rows y.. of width w (at most n rows) */
static void wrap(uint8_t y, const char *s, uint8_t w, uint8_t n)
{
    static char line[41];
    uint8_t len, cut;
    while (*s && n--) {
        len = (uint8_t)strlen(s);
        if (len <= w) cut = len;
        else {
            cut = w;
            while (cut && s[cut] != ' ') --cut;
            if (!cut) cut = w;
        }
        memcpy(line, s, cut);
        line[cut] = 0;
        clear_row(y);
        text_print_at(1, y++, line);
        s += cut;
        while (*s == ' ') ++s;
    }
}

static void draw(void)
{
    uint8_t i, g;
    if (sel < top) top = sel;
    if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
    for (i = 0; i < LIST_ROWS; ++i) {
        g = top + i;
        clear_row(LIST_Y + i);
        if (g < GAME_COUNT) {
            print_inv(2, LIST_Y + i, "                                    ", g == sel);
            print_inv(3, LIST_Y + i, game_title[g], g == sel);
        }
    }
    for (i = 17; i < 23; ++i) clear_row(i);
    if (GAME_COUNT) {
        wrap(17, game_desc[sel], 38, 4);
        text_print_at(1, 22, game_controls[sel]);
    }
}

int main(void)
{
    uint8_t in, pressed, rep = 0;

    gfx_init(GFX_TEXT, 0);
    gfx_color(2, A8_COLOR(0, 0));
    gfx_color(1, 0x0E);
    gfx_border(A8_COLOR(3, 2));
    text_print_at(14, 1, "A8 ARCADE");
    text_print_at(2, 2, "------------------------------------");
    text_print_at(1, 16, "--------------------------------------");
    if (!GAME_COUNT) text_print_at(3, LIST_Y, "no games (add arcade.json files)");
    frame_init();
    draw();
    a8_log("arcade: menu ready sel=%u count=%u\n", sel, GAME_COUNT);

    for (;;) {
        frame_wait();
        in = input_read();
        pressed = input_pressed();
        if (in & (IN_UP | IN_DOWN)) {
            if ((pressed & (IN_UP | IN_DOWN)) || ++rep > 12) {
                if (!(pressed & (IN_UP | IN_DOWN))) rep = 9;     /* held: repeat every 4 frames */
                else rep = 0;
                if ((in & IN_UP) && sel > 0) --sel;
                else if ((in & IN_DOWN) && sel + 1 < GAME_COUNT) ++sel;
                else continue;
                draw();
                a8_log("arcade: sel=%u\n", sel);
            }
        }
        if ((pressed & (IN_FIRE | IN_START)) && GAME_COUNT) {
            a8_log("arcade: run n=%u\n", sel);
            gfx_off();
            RUN_ENTRY(sel + 1);
        }
    }
}
