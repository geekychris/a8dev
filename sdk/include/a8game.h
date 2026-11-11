/*
 * a8game.h - the a8dev game layer: frame timer, input, random numbers,
 * POKEY sound and music, 8.8 fixed point. Linked from build/a8sdk.lib, so a
 * game only carries what it calls. Graphics: a8gfx.h; sprites: a8pmg.h;
 * 130XE banks: a8xe.h; 16.16 maths: fix16.h; log: a8log.h.
 */
#ifndef A8GAME_H
#define A8GAME_H
#include <stdint.h>
#include "a8hw.h"

/* ------------------------------------------------------------ frame timer */

/* The OS vertical-blank interrupt counts RTCLOK2 (50 Hz PAL, 60 Hz NTSC). */
extern void frame_init(void);              /* reset counters, stop attract mode */
/* wait for the next vertical blank; returns how many frames passed since the
   last call (1 = on time). Also runs sound_tick() once per frame passed. */
extern uint8_t frame_wait(void);
extern uint16_t frame_count;               /* frames since frame_init (waits) */
extern uint16_t frame_overruns;            /* frames the game took too long for */
#define frame_is_pal() ((PAL & 0x0E) == 0)

/* ------------------------------------------------------------ input */

#define IN_LEFT    0x01
#define IN_RIGHT   0x02
#define IN_UP      0x04
#define IN_DOWN    0x08
#define IN_FIRE    0x10
#define IN_START   0x20                    /* console START (or RETURN) */
#define IN_SELECT  0x40
#define IN_OPTION  0x80

/* joystick 0 + console keys + cursor-free keys: W/A/S/D or arrows (- = * +
   with or without CTRL) move, SPACE fires, RETURN = START */
extern uint8_t input_read(void);
extern uint8_t input_pressed(void);        /* bits that went down since the previous read */
extern uint8_t input_last;
extern uint8_t joy_read(uint8_t port);     /* IN_LEFT..IN_FIRE for stick 0/1 */
extern uint8_t console_read(void);         /* IN_START | IN_SELECT | IN_OPTION held */
/* keyboard: the OS key code of the last key (KEY_* below), or KEY_NONE.
   key_get() also takes it (so the next call sees a new press only). */
#define KEY_NONE   0xFF
extern uint8_t key_peek(void);
extern uint8_t key_get(void);
extern char key_ascii(uint8_t code);       /* 'A'..'Z', '0'..'9', ' ', '\n' or 0 */
#define KEY_RETURN 12
#define KEY_SPACE  33
#define KEY_ESC    28
#define KEY_A 63
#define KEY_D 58
#define KEY_S 62
#define KEY_W 46
#define KEY_Q 47
#define KEY_P 10
#define KEY_Z 23
#define KEY_X 22
#define KEY_Y 43
#define KEY_N 35
#define KEY_1 31
#define KEY_2 30

/* ------------------------------------------------------------ random */

extern void rnd_seed(uint16_t s);          /* 0 is replaced by 1 */
extern uint16_t rnd16(void);               /* xorshift: deterministic, not POKEY RANDOM */
extern uint16_t rnd_range(uint16_t n);     /* 0..n-1 */

/* ------------------------------------------------------------ sound (POKEY) */

/* channels 0..3. AUDC = distortion (high nibble) | volume (0..15):
   SND_PURE = 0xA0 square wave, SND_NOISE = 0x80 white noise, 0xC0 buzz. */
#define SND_PURE   0xA0
#define SND_NOISE  0x80
#define SND_BUZZ   0xC0
extern void sound_init(void);              /* AUDCTL 0 (64 kHz clock), all silent */
extern void snd_tone(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol);
extern void snd_note(uint8_t ch, uint8_t note, uint8_t vol);   /* note = MIDI 48..107 (C3..B7) */
extern void snd_off(uint8_t ch);
/* an effect: tone that decays `decay` volume steps a frame, pitch +sweep a frame */
extern void snd_sfx(uint8_t ch, uint8_t freq, uint8_t dist, uint8_t vol, uint8_t decay, int8_t sweep);
extern const uint8_t snd_note_freq[60];    /* AUDF for MIDI 48..107 at 64 kHz (PAL) */

/* music: a list of notes per channel; note = MIDI (0 = rest), len in frames,
   a {0,0} entry loops back to the start. Ticked by frame_wait(). */
typedef struct { uint8_t note; uint8_t len; } a8_note;
extern void music_play(uint8_t ch, const a8_note *track, uint8_t vol);
extern void music_stop(void);
extern void sound_tick(void);              /* once per frame (frame_wait calls it) */

/* ------------------------------------------------------------ 8.8 fixed point */

typedef int16_t fix8;                      /* 8.8: 256 = 1.0 */
#define FIX(v)       ((fix8)((v) * 256))
#define FIX_INT(f)   ((int16_t)((f) >> 8))
#define FIX_FRAC(f)  ((uint8_t)(f))
extern const int16_t fix_sin_tab[256];
#define fix_sin(a)   (fix_sin_tab[(uint8_t)(a)])
#define fix_cos(a)   (fix_sin_tab[(uint8_t)((a) + 64)])
extern fix8 fix_mul(fix8 a, fix8 b);
extern fix8 fix_div(fix8 a, fix8 b);       /* b != 0 */
extern uint8_t fix_atan2(int16_t dy, int16_t dx);  /* 0..255, 0 = +x, 64 = +y */
extern uint16_t isqrt16(uint16_t v);

#endif
