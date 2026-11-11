/*
 * Uranus Lander - autopilot.
 *
 * The ZX Spectrum port's autopilot (spectrum_dev/projects/uranus_lander),
 * which is the Atari ST port's autopilot.py in fixed point: OPTION (or Z)
 * switches it on and off while playing, and the test's bot is this. Same rules: aim for the best
 * multiplier pad (then the nearest), a wanted sideways speed towards its
 * centre reached by tilting, a descent speed limit that shrinks with
 * altitude, hover with clearance until over the pad, upright for touchdown.
 * Speeds here are Q8 pixels per frame (the 16.16 velocities >> 8).
 */
#include "game.h"
#include "autopilot.h"

static const LandingPad *target;
static WORD target_level = -1;
static uint8_t hold_left, hold_right, hold_thrust, tick;
static WORD t_cx, t_half, t_y;          /* the target pad: centre, half width - 3, y */

#define clampw(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define absw(v) ((v) < 0 ? -(v) : (v))

/* best multiplier, then nearest */
static uint8_t choose_pad(const GameState *g, WORD x)
{
    uint8_t i, best = 0;
    WORD best_m = -1, best_d = 0;
    for (i = 0; i < (uint8_t)g->num_pads; i++) {
        WORD m = g->pads[i].multiplier;
        WORD d = absw(g->pads[i].x + g->pads[i].width / 2 - x);
        if (m > best_m || (m == best_m && d < best_d)) {
            best = i;
            best_m = m;
            best_d = d;
        }
    }
    return best;
}

void autopilot_reset(void)
{
    target_level = -1;
    hold_left = hold_right = hold_thrust = 0;
    tick = 0;
}

/* (the integer and Q8 parts of the 16.16 values are read as bytes, and the
   products fit 16 bits: cc65's 32-bit arithmetic made this ~20,000 cycles) */
#define HI16(v) (*(int16_t *)((uint8_t *)&(v) + 2))      /* v >> 16 */
#define MID16(v) (*(int16_t *)((uint8_t *)&(v) + 1))     /* v >> 8 */

void autopilot_fly(InputState *in)
{
    static WORD x, y, vx, vy, angle, cx, err, ground, alt, vx_want, dv, angle_want, vy_want, keep;
    static uint8_t over_pad;
    x = HI16(gs.ship.x);
    y = HI16(gs.ship.y);
    vx = MID16(gs.ship.vx);                 /* Q8 */
    vy = MID16(gs.ship.vy);
    angle = gs.ship.angle < 128 ? gs.ship.angle : gs.ship.angle - 256;   /* signed, 0 = up */

    if (target_level != gs.level) {
        target = &gs.pads[choose_pad(&gs, x)];
        target_level = gs.level;
        t_cx = target->x + target->width / 2;      /* (kept: no pointer a frame) */
        t_half = target->width / 2 - 3;
        t_y = target->y;
    }
    /* like the original bot, decide every second frame and hold in between */
    if ((tick++ & 1) == 0) {
        cx = t_cx;
        err = cx - x;
        err = clampw(err, -40, 40);
        dv = cx - x;
        over_pad = absw(dv) < t_half;
        ground = x < 0 ? 0 : x > TERRAIN_W - 1 ? TERRAIN_W - 1 : x;
        ground = gs.terrain_y[ground];
        if (t_y < ground) ground = t_y;
        alt = ground - (y + 6);             /* the ship's bottom is y + 6 */

        vx_want = (err * 205) >> 5;                           /* err * 0.025, |0.9| */
        vx_want = clampw(vx_want, -230, 230);
        dv = vx_want - vx;
        if (alt < 18 && over_pad) {
            angle_want = 0;                 /* upright for touchdown */
        } else {                            /* (dv clamped first: the same result) */
            angle_want = clampw(dv, -400, 400);
            angle_want = (angle_want * 40 + 128) >> 8;
            angle_want = clampw(angle_want, -24, 24);
        }

        vy_want = 64 + alt * 3;                               /* 0.25 + alt * 0.012, 0.25..1.4 */
        vy_want = clampw(vy_want, 64, 358);
        if (!over_pad) {
            keep = (alt - 35) * 5;                            /* (alt - 35) * 0.02: ~35 px clearance */
            if (keep < vy_want) vy_want = keep;
        }
        hold_thrust = vy > vy_want;
        if (absw(dv) > 38 && absw(angle) > 4 && vy > -102 && alt > 20)
            hold_thrust = 1;                /* use tilted thrust to move sideways */
        if (vy < -154)
            hold_thrust = 0;

        hold_left = angle > angle_want + 1;
        hold_right = angle < angle_want - 1;
    }
    in->left = hold_left;
    in->right = hold_right;
    in->thrust = hold_thrust;
    in->up = 0;
}
