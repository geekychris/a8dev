/*
 * od.h - Orbit Docking's rules for the Atari, from the Amiga original's
 * od.h / od_core.c. A station spins about its long axis; its docking port
 * is a slot at the end of its hub, on that axis, turning with it. You fly
 * a small craft that always faces the station: thrusters move it across
 * (x, y) and along (z) the axis, and roll it. There is no friction in
 * space: a push lasts until you push back. The thrusters across are the
 * craft's own: right is right in the (rolled) view.
 *
 * To dock: come in along the axis, rolled to match the slot and turning as
 * fast as it turns, slowly. At contact the craft must be within 14 of the
 * axis, closing at under 1.6 a tick, drifting across at under 0.8, rolled
 * within 8 degrees of the slot and turning within 7 degrees a second of
 * it. Anything else is a crash.
 *
 * The station at the origin, its axis along z, the port facing +z. One
 * tick is 40 ms (every second PAL frame).
 *
 * port: 16 bits where the original has 16.16: positions in 1/16 units
 * (with a 1/256 remainder), speeds in 1/256 units a tick; angles as the
 * original (16 bits and 8 more of fraction, in 32); fuel in half units;
 * the debris' swing by an angle that steps each tick.
 */
#ifndef OD_GAME_H
#define OD_GAME_H
#include <stdint.h>

#define OD_LEVELS    6
#define OD_LIVES     3
#define OD_PORT_Z    60
#define OD_HUB_R     60
#define OD_NOSE      20
#define OD_START_Z   900
#define OD_MAX_DEBRIS 3
#define OD_ACC       10                 /* 0.04 a tick^2 (0.039), 1/256 units */
#define OD_ROLL_ACC  (6 << 8)
#define OD_TOL_AXIS  14
#define OD_TOL_CLOSE 410                /* 1.6, 1/256 units a tick */
#define OD_TOL_DRIFT 205                /* 0.8 */
#define OD_TOL_ROLL  1456               /* 8 degrees */
#define OD_TOL_SPIN  (20 << 8)
#define OD_BETWEEN   75

#define OD_IN_LEFT   1
#define OD_IN_RIGHT  2
#define OD_IN_UP     4
#define OD_IN_DOWN   8
#define OD_IN_FWD    16
#define OD_IN_BACK   32
#define OD_IN_ROLL_L 64
#define OD_IN_ROLL_R 128

enum { ODE_NONE, ODE_DOCK, ODE_CRASH, ODE_DEBRIS, ODE_FUEL_OUT, ODE_LEVEL, ODE_OVER, ODE_WIN, ODE_LOST, ODE_COUNT };
#define OD_MAX_EVENTS 8

typedef struct {
    uint32_t seed, rng;
    uint8_t level, lives, over, won, between;
    int16_t x, y, z;                    /* the craft, 1/16 units */
    uint8_t xf, yf, zf;                 /* and 1/256 units more (0..15) */
    int16_t vx, vy, vz;                 /* 1/256 units a tick */
    int32_t roll, roll_rate;            /* a8: 16-bit angle << 8 */
    int32_t st_angle, st_spin;
    uint16_t fuel, fuel_max;            /* half units */
    uint8_t ndebris;
    int16_t d_z[OD_MAX_DEBRIS], d_y[OD_MAX_DEBRIS], d_amp[OD_MAX_DEBRIS];
    uint32_t d_ang[OD_MAX_DEBRIS], d_step[OD_MAX_DEBRIS];   /* the swing's angle, 16.16 of a 16-bit turn */
    uint8_t crashes, docks;
    uint16_t tick, level_ticks;
    uint16_t score;
    uint8_t last_fail;                  /* 1 off axis, 2 too fast, 3 drifting, 4 rolled wrong, 5 spin, 6 the hub's side, 7 debris */
    uint8_t ev[OD_MAX_EVENTS], eva[OD_MAX_EVENTS], nev;
    uint16_t evb[OD_MAX_EVENTS];
} ODGame;

extern ODGame G;
extern const char *const od_fail_names[8];

void od_new_game(uint32_t seed, uint8_t level);
void od_load_level(uint8_t level);
void od_tick(uint8_t in);
uint8_t od_pop_event(uint8_t *a, uint16_t *b);
uint8_t od_bot(void);
int16_t od_roll_error(void);            /* the craft's roll against the slot, -16384..16383 */
int16_t od_debris_x(uint8_t k);         /* units */
int32_t od_lat2(void);                  /* the craft's distance from the axis squared, 1/16 units */
int16_t od_sin(uint16_t a);             /* 16-bit angle -> Q14 */
#define od_cos(a) od_sin((uint16_t)((a) + 0x4000))

#endif
