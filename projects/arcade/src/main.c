/*
 * The a8dev arcade menu: the program the arcade disk boots into.
 *
 * Title, the selected game's picture (80x48, 4 colours, ANTIC D, read from
 * the disk), the list of games, the description and controls
 * (build/gen/games.h, made from the projects' arcade.json by ./a8 arcade).
 * Joystick / W S / arrows move, FIRE / START / RETURN play. The disk's boot
 * loader (sdk/boot/boot.s, resident at $0700) loads the game's XEX:
 * directory entry i + 1 = game i. RESET comes back here.
 *
 * Log: "arcade: menu ready sel=N count=N", "arcade: sel=N",
 * "arcade: picture n=N ok=1", "arcade: run n=N".
 */
#include <string.h>
#include "a8game.h"
#include "a8gfx.h"
#include "a8log.h"
#include "games.h"

#define RUN_ENTRY   ((void __fastcall__ (*)(uint8_t))0x0709)
#define READ_SECTOR 0x070C
#define PIC_SECTOR  17          /* a8dev/atr.py */
#define PIC_SECTORS 8

#define PICTURE  ((uint8_t *)0xA010)   /* 48 lines x 40 bytes, ANTIC D */
#define TEXT     ((uint8_t *)0xA800)   /* 12 rows x 40: 0-1 title, 2-11 below */
#define PICBUF   ((uint8_t *)0xAC00)   /* 1024 bytes read from the disk */
#define DLIST    ((uint8_t *)0xBF00)

#define LIST_Y    2
#define LIST_ROWS 5
#define DESC_Y    8

extern uint8_t pic_colors[4];
extern void dli_on(void);
extern void dli_off(void);

static uint8_t sel, top, shown = 0xFF;

static void build_dlist(void)
{
    uint8_t *d = DLIST, i;
    *d++ = 0x70; *d++ = 0x70; *d++ = 0x70;
    *d++ = 0x42; *d++ = (uint8_t)(uint16_t)TEXT; *d++ = (uint8_t)((uint16_t)TEXT >> 8);
    *d++ = 0x02;
    *d++ = 0xF0;                                  /* 8 blank lines + DLI: picture colours */
    *d++ = 0x4D; *d++ = (uint8_t)(uint16_t)PICTURE; *d++ = (uint8_t)((uint16_t)PICTURE >> 8);
    for (i = 1; i < 48; ++i) *d++ = 0x0D;
    *d++ = 0xB0;                                  /* 4 blank lines + DLI: text colours */
    *d++ = 0x42; *d++ = (uint8_t)(uint16_t)(TEXT + 80); *d++ = (uint8_t)((uint16_t)(TEXT + 80) >> 8);
    for (i = 1; i < 10; ++i) *d++ = 0x02;
    *d++ = 0x41; *d++ = (uint8_t)(uint16_t)DLIST; *d = (uint8_t)((uint16_t)DLIST >> 8);
}

static uint8_t read_sector(uint16_t sector, uint8_t *buf)
{
    *(uint16_t *)0x30A = sector;
    *(uint16_t *)0x304 = (uint16_t)buf;
    __asm__("jsr %w", READ_SECTOR);
    return *(uint8_t *)0x303 == 1;
}

static void load_picture(uint8_t g)
{
    uint8_t i, ok = 1, *src, *dst;
    for (i = 0; i < PIC_SECTORS; ++i)
        ok &= read_sector(PIC_SECTOR + (uint16_t)g * PIC_SECTORS + i, PICBUF + (uint16_t)i * 128);
    memset(PICTURE, 0, 48 * 40);
    if (ok && PICBUF[1023] == 0xA8) {
        memcpy(pic_colors, PICBUF, 4);
        src = PICBUF + 4;
        dst = PICTURE + 10;
        for (i = 0; i < 48; ++i, src += 20, dst += 40) memcpy(dst, src, 20);
    } else {
        pic_colors[0] = 0;
        ok = 0;
    }
    shown = g;
    a8_log("arcade: picture n=%u ok=%u\n", g, ok);
}

static void clear_row(uint8_t y) { memset(TEXT + (uint16_t)y * 40, 0, 40); }

static void print_inv(uint8_t x, uint8_t y, const char *s, uint8_t inv)
{
    uint8_t *p = TEXT + (uint16_t)y * 40 + x;
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
    if (top) text_print_at(39, LIST_Y, "-");
    if (top + LIST_ROWS < GAME_COUNT) text_print_at(39, LIST_Y + LIST_ROWS - 1, "+");
    for (i = DESC_Y; i < 12; ++i) clear_row(i);
    if (GAME_COUNT) {
        wrap(DESC_Y, game_desc[sel], 38, 3);
        text_print_at(1, 11, game_controls[sel]);
    }
}

int main(void)
{
    uint8_t in, pressed, rep = 0, still = 0;

    SDMCTL = 0;
    memset(TEXT, 0, 12 * 40);
    build_dlist();
    gfx_screen = TEXT;                 /* text_print_at writes our text rows */
    COLOR1 = 0x0E;
    COLOR2 = A8_COLOR(0, 0);
    COLOR4 = A8_COLOR(3, 2);
    CHBAS = 0xE0;
    SDLSTL = (uint16_t)DLIST;
    dli_on();
    SDMCTL = 0x22;

    text_print_at(15, 0, "A8 ARCADE");
    text_print_at(4, 1, "joystick: choose   FIRE: play");
    if (!GAME_COUNT) text_print_at(3, LIST_Y, "no games (add arcade.json files)");
    frame_init();
    draw();
    if (GAME_COUNT) load_picture(sel);
    a8_log("arcade: menu ready sel=%u count=%u\n", sel, GAME_COUNT);

    for (;;) {
        frame_wait();
        in = input_read();
        pressed = input_pressed();
        if (in & (IN_UP | IN_DOWN)) {
            if ((pressed & (IN_UP | IN_DOWN)) || ++rep > 12) {
                rep = (pressed & (IN_UP | IN_DOWN)) ? 0 : 9;   /* held: repeat every 4 frames */
                if ((in & IN_UP) && sel > 0) --sel;
                else if ((in & IN_DOWN) && sel + 1 < GAME_COUNT) ++sel;
                else continue;
                draw();
                still = 0;
                a8_log("arcade: sel=%u\n", sel);
            }
        }
        /* load the picture once the selection has stayed for a few frames */
        if (shown != sel && GAME_COUNT && ++still >= 6) load_picture(sel);
        if ((pressed & (IN_FIRE | IN_START)) && GAME_COUNT) {
            a8_log("arcade: run n=%u\n", sel);
            dli_off();
            SDMCTL = 0;
            RUN_ENTRY(sel + 1);
        }
    }
}
