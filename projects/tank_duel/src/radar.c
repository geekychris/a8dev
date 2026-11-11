/*
 * Tank Duel: the radar and the gun sight, in players and missiles (the
 * original draws them into its picture; port: here they cost the picture
 * nothing).
 *
 * The radar turns with the tank (ahead is up): 32 scan lines high, 16
 * pixels wide (players 2 and 3, double width: 8 bits of 2 pixels, at the
 * same place): 500 units a bit across, 80 a line. Player 2 is its frame and the tank's mark,
 * player 3 the enemies' blips, missile 3 (player 3's colour, flashing) the
 * mortar rounds' landing points. The sight: missile 0 above the view's centre.
 */
#include <string.h>
#include "a8game.h"
#include "td.h"
#include "view.h"

extern int16_t r_dx, r_dz, r_cx, r_cz, r_cs, r_cc;   /* fast.s */
void rot2(void);

#define PM(n)    ((uint8_t *)(0x9C00 + ((n) << 8)))
#define MIS      ((uint8_t *)0x9B00)
#define RTOP     30                      /* the radar's first scan line */
#define RH       32
#define RX       180                     /* its left (HPOS) */
#define SIGHT_Y  (24 + V_HORIZON * 2 - 10)   /* the sight's top scan line */

void radar_init(void)
{
    uint8_t y, *p = PM(2) + RTOP;
    memset(MIS, 0, 0x500);
    PMBASE = 0x98;
    SDMCTL |= 0x1C;                     /* players and missiles, single-line (a3d_init sets the playfield's) */
    GRACTL = 3;
    GPRIOR = 0x01;
    (&SIZEP0)[2] = (&SIZEP0)[3] = 1;    /* double width */
    SIZEM = 0x40;                       /* missile 3 double */
    (&HPOSP0)[2] = (&HPOSP0)[3] = RX;
    (&PCOLR0)[2] = 0xB4;                /* the frame: dark green */
    (&PCOLR0)[3] = 0xBE;                /* the blips: light green */
    PCOLR0 = 0x0E;                      /* the sight: missile 0 takes player 0's colour */
    (&HPOSM0)[0] = 48 + 80;
    p[0] = p[RH - 1] = 0xFF;
    for (y = 1; y < RH - 1; ++y) p[y] = 0x81;
    p[RH / 2] |= 0x18;                  /* the tank, in the middle */
    for (y = 0; y < 8; ++y) MIS[SIGHT_Y + y] = (uint8_t)(y == 3 || y == 4 ? 0x03 : 0x02);
}

void radar_off(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i) { (&HPOSP0)[i] = 0; (&HPOSM0)[i] = 0; }
}

void radar_on(void)
{
    (&HPOSP0)[2] = (&HPOSP0)[3] = RX;
    (&HPOSM0)[0] = 48 + 80;
}

/* blips: each enemy relative to the tank, turned so ahead is up */
void radar_update(void)
{
    static uint8_t *b;
    const Tank *p = &G.tk[0], *t;
    int16_t s = td_sin8(p->yaw), c = td_cos8(p->yaw), dx, dz, rx, rz;
    uint8_t k, col, row;
    b = PM(3) + RTOP;
    memset(b, 0, RH);
    memset(MIS + RTOP, 0, RH);
    for (k = 1, t = G.tk + 1; k < G.ntanks; ++k, ++t) {
        if (!t->alive) continue;
        if (t->type == TK_BULWARK && (G.tick & 2)) continue;   /* bulwarks flash */
        dx = UNITS(t->x - p->x);
        dz = UNITS(t->z - p->z);
        r_dx = dx; r_dz = dz; r_cs = s; r_cc = c;
        rot2();
        rx = r_cx;                          /* across */
        rz = r_cz;                          /* ahead */
        rx = (rx + 2000) / 500;             /* 8 bits across: 0..7 */
        rz = 16 - rz / 80;
        if (rx < 1 || rx > 6 || rz < 1 || rz > 30) continue;
        col = (uint8_t)rx;
        row = (uint8_t)rz;
        b[row] |= (uint8_t)(0x80 >> col);
    }
    for (k = 0; k < TD_MAX_MORTARS; ++k) {
        const Mortar *m = &G.mo[k];
        if (!m->alive || !(G.tick & 1)) continue;
        dx = UNITS(m->tx - p->x);
        dz = UNITS(m->tz - p->z);
        r_dx = dx; r_dz = dz; r_cs = s; r_cc = c;
        rot2();
        rx = (r_cx + 2000) / 500;
        rz = 16 - r_cz / 80;
        if (rx < 1 || rx > 6 || rz < 1 || rz > 30) continue;
        MIS[RTOP + rz] |= 0xC0;
        (&HPOSM0)[3] = (uint8_t)(RX + rx * 2);
    }
}
