/*
 * ld.h - Lander 3D's rules for the Atari: the original's (amiga_mcp
 * examples/lander_3d, ld_core.c, the browser's lander-3d-core.js) worlds,
 * levels, limits and scoring, its terrain idea (hills from value noise,
 * craters with raised rims, three flattened pads x1 x2 x5) and its
 * autopilot's plan, on 8-bit maths.
 *
 * port: 12.5 steps a second (the original: 100 with sub-steps); positions
 * and velocities 16.16 (metres, m/s), accelerations 8.8; the attitude as
 * a binary angle (65536 a turn) that the stick turns at a fixed rate (the
 * original's assisted mode: it holds the angle when you let go); a 31 x 31
 * grid of whole-metre heights (32 m cells; 16 m on the asteroid) made at
 * the level's start from its seed.
 */
#ifndef LD_H
#define LD_H
#include <stdint.h>

typedef int32_t fx;                     /* 16.16 */
#define FXI(v)   ((fx)(v) << 16)
#define LD_N     31                     /* grid points a side */
#define LD_S     32                     /* the grid's row stride (shifts, not multiplies) */
#define LD_HZ    25                     /* fuel's units (1/16 of 1/25 s at full throttle) */

enum { W_MOON, W_MARS, W_ASTEROID, W_COUNT };
enum { OUT_NONE, OUT_LANDED, OUT_OFFPAD, OUT_CRASHED, OUT_LOST };
enum { LDE_NONE, LDE_LANDED, LDE_OFFPAD, LDE_CRASHED, LDE_LOST, LDE_FUEL_OUT };

typedef struct {
    const char *name;
    int16_t g, amax;                    /* m/s2, 8.8 */
    uint8_t cell;                       /* metres between grid points */
    uint8_t hills, rough;               /* metres; octave falloff /256 */
    uint8_t craters, crater_max;        /* count, biggest radius (m) */
    uint8_t wind;                       /* m/s (Mars) */
    uint8_t curve;                      /* the asteroid's curvature: drop /m at 100 m */
    int16_t lim_vs, lim_hs;             /* m/s, 8.8 */
    uint8_t lim_tilt;                   /* degrees */
    uint8_t fuel;                       /* seconds of full throttle */
} LdWorld;

typedef struct { int16_t x, z; int8_t y; uint8_t r4, mult; } Pad;     /* r4: radius in 1/4 m */

typedef struct {
    uint8_t vs_ok, hs_ok, tilt_ok, pad;
    int16_t vs, hs;                     /* 8.8 m/s */
    uint8_t tilt;                       /* degrees */
    uint16_t soft, fuel_bonus, score;
} LdResult;

typedef struct {
    uint8_t world, level, outcome;
    uint16_t half;                      /* half the world's size (m) */
    uint8_t cell;
    uint8_t cshift;                     /* cell = 1 << cshift metres */
    int8_t h[LD_S * LD_N];              /* heights (m), row z then x */
    Pad pads[3];
    int16_t pitch, roll;                /* 65536 a turn */
    uint8_t throttle, rcs;
    uint16_t fuel, fuel_max;            /* 1/16 of a full-throttle step */
    int16_t windx, windz;               /* m/s2 8.8 */
    uint16_t t;                         /* steps */
    int8_t ap_target;
    uint8_t ap_down;
    LdResult res;
    uint8_t ev[8], nev;
} Level;

typedef struct {
    uint8_t throttle; int8_t pitch, roll;   /* 0..255, the stick: -1/0/1 */
    uint8_t aim; int16_t tp, tr;            /* the autopilot: the angles to turn to */
} LdInput;

extern Level L;
/* the craft (ld_phys.s): 16.16 position and velocity, and the 16-bit copies
   (index 0 x, 1 y up, 2 z) */
extern fx ld_p[3], ld_v[3];
extern int16_t ld_acc[3], ld_m[3], ld_v88[3];
extern uint8_t ld_frac[3];
void ld_integrate(void);
void ld_sync(void);
extern const LdWorld ld_worlds[W_COUNT];

void ld_new_level(uint8_t world, uint8_t level, uint32_t seed);
extern LdInput ld_in;                   /* the controls (the player's or the autopilot's) */
void ld_step(void);
void ld_autopilot(void);
int16_t ld_ground(int16_t xm, uint8_t xf, int16_t zm, uint8_t zf);   /* ground (1/16 m) at metres + 1/256 */
int16_t ld_ground_here(void);
int16_t ld_agl(void);                   /* the feet above the ground (1/16 m) */
int8_t ld_nearest_pad(void);
uint8_t ld_tilt_deg(void);
uint8_t ld_pop_event(void);

#endif
