/*
 * sc.h - ScAIry Fun (Atari edition): the simulation.
 *
 * The original's world.c and track.c (amiga_mcp/examples/scairy_fun) in 16
 * bits: the same routes, rules, hazards and autopilot, with
 *   - z (along the road) as an unsigned count of world units (16 a metre,
 *     128 a segment; every route fits under 65536) plus a 4-bit fraction,
 *     instead of a 32-bit "fine" value;
 *   - lateral offsets d, speeds v and the plan as int16 fine units
 *     (world << 4), as in the original;
 *   - the objects as struct-of-arrays, 10 of them (the original has 40);
 *   - the AI's picture of the world (detection list, the top-down view)
 *     folded into its speed choice.
 * Compiled for the host too (tools/simtest.c), so it can be checked there.
 */
#ifndef SC_H
#define SC_H

#include <stdint.h>

#define FBITS       4
#define SEG_BITS    7
#define SEG_LEN     128                  /* units = 8 m */
#define MAX_SEGS    520
#define LANE_W      56                   /* 3.5 m */
#define SHOULDER    24
#define RUNOUT      40

enum { SIDE_GRASS, SIDE_LEVEE, SIDE_DROP, SIDE_WALL, SIDE_CLIFF, SIDE_MEDIAN,
       SIDE_TOWN, SIDE_CANAL, SIDE_COUNT };

#define SF_FLOOD     0x01
#define SF_GHOST     0x02
#define SF_OVERPASS  0x04
#define SF_RAIL_L    0x08
#define SF_RAIL_R    0x10
#define SF_GLARE     0x20
#define SF_FINISH    0x40
#define SF_CONES     0x80

/* segments, struct-of-arrays */
extern int16_t seg_cx[MAX_SEGS];         /* centre line x at the segment start, units */
extern int16_t seg_y[MAX_SEGS];
extern int8_t seg_dir[MAX_SEGS];         /* cx change to the next segment */
extern uint8_t seg_side[MAX_SEGS];       /* left | right << 4 */
extern uint8_t seg_flags[MAX_SEGS];
extern uint8_t seg_closed[MAX_SEGS];
extern uint8_t seg_ghost[MAX_SEGS];      /* SF_GHOST: old line offset from the right edge, units */

enum { HZ_NONE, HZ_FLOOD, HZ_GHOST, HZ_PHANTOM, HZ_CONES, HZ_GLARE, HZ_SIREN, HZ_DEBRIS,
       HZ_DEER, HZ_CYCLIST, HZ_TRACTOR, HZ_MERGE, HZ_GOATS, HZ_PEDS, HZ_COUNT };
#define HZ_FAILS(t) ((t) >= HZ_FLOOD && (t) <= HZ_DEBRIS)

typedef struct {
    uint8_t type;
    int16_t seg, len, param;
} Hazard;

typedef struct {
    const char *name, *dest, *ai_name;
    int8_t nl, no;                       /* lanes our way, oncoming lanes */
    uint8_t limit_kmh, traffic;
} RouteInfo;

#define NUM_ROUTES 4
#define MAX_HAZ    10
extern const RouteInfo route_info[NUM_ROUTES];

enum { OBJ_NONE, OBJ_CAR, OBJ_TRUCK, OBJ_TRACTOR, OBJ_CYCLIST, OBJ_DEER,
       OBJ_CONE, OBJ_DEBRIS, OBJ_AMBULANCE, OBJ_GOAT, OBJ_PED, OBJ_COUNT };

#define OF_ONCOMING  0x01
#define OF_KNOCKED   0x02
#define OF_TAILGATE  0x04
#define OF_PASSING   0x08

#define MAX_OBJS 10
extern uint8_t obj_type[MAX_OBJS], obj_flags[MAX_OBJS], obj_colour[MAX_OBJS];
extern int8_t obj_hz[MAX_OBJS], obj_lane[MAX_OBJS];
extern uint16_t obj_z[MAX_OBJS];         /* units */
extern uint8_t obj_zf[MAX_OBJS];         /* fine fraction 0..15 */
extern int16_t obj_d[MAX_OBJS], obj_v[MAX_OBJS], obj_vd[MAX_OBJS], obj_vt[MAX_OBJS];
extern int16_t obj_timer[MAX_OBJS], obj_cool[MAX_OBJS];

#define MAX_KMH    140
/* km/h <-> fine units a step (x 256/90): in 16 bits (x 91/32, x 45/128) */
#define KMH(v)     ((int16_t)(((int16_t)(v) * 45) >> 7))
#define FROM_KMH(k) ((int16_t)(((int16_t)(k) * 91) >> 5))

enum { GS_TITLE, GS_INTRO, GS_PLAY, GS_ARRIVED, GS_UPDATE, GS_GAMEOVER, GS_WIN };
enum { CS_DRIVING, CS_AQUAPLANE, CS_SPLASH, CS_ROLL };

#define IN_LEFT    0x01
#define IN_RIGHT   0x02
#define IN_UP      0x04
#define IN_DOWN    0x08
#define IN_FIRE    0x10
#define IN_START   0x20

enum { MSG_NONE, MSG_AP_ON, MSG_AP_OFF, MSG_HAD_THAT, MSG_GOOD_CATCH,
       MSG_RECALC, MSG_ROAD_PROBABLY, MSG_OBSTACLE, MSG_HANDS, MSG_OOPS,
       MSG_UPDATED, MSG_UNAVAILABLE, MSG_WELCOME, MSG_WATER_ANYWAY,
       MSG_ARRIVED, MSG_BLINDED, MSG_PATIENT, MSG_TICKET, MSG_TOW,
       MSG_CANCELLED, MSG_COUNT };
extern const char *const msg_text[MSG_COUNT];

enum { EV_NONE, EV_AP_ON, EV_AP_OFF, EV_UNAVAILABLE, EV_SAVE, EV_NEEDLESS,
       EV_SCRAPE, EV_CONE, EV_DEBRIS, EV_REAREND, EV_CRASH, EV_SPLASH,
       EV_ROLL, EV_TICKET, EV_RESPAWN, EV_PHANTOM, EV_BLINDED, EV_AQUAPLANE,
       EV_ARRIVED, EV_GAME_OVER, EV_MULT_UP, EV_SIREN, EV_ROUTE_START,
       EV_HIT_ANIMAL, EV_SAY, EV_WIN, EV_COUNT };
extern const char *const ev_name[EV_COUNT];
#define MAX_EVENTS 8
typedef struct { uint8_t type; int16_t a, b; } Event;

/* the car and the autopilot */
typedef struct {
    uint8_t state;
    uint16_t z;                          /* units */
    uint8_t zf;
    int16_t d, v, s, lat;
    int16_t timer, cool;
    int16_t hist[32];
    uint8_t hist_pos;
} Car;

typedef struct {
    uint8_t on;
    int8_t lane;
    int16_t plan[16];                    /* planned d (fine) at each segment ahead */
    int16_t plan_from;
    int16_t phantom_timer, phantom_seg;
    uint8_t blinded;
    int16_t blind_x;                     /* units: absolute x where it went blind */
    uint16_t blind_z;
    int8_t blind_dir;
    int16_t target_v;
    int16_t lane_hold;
} Ai;

typedef struct {
    uint8_t route;
    int16_t nseg, finish;
    Hazard haz[MAX_HAZ];
    uint8_t nhaz;
    uint8_t haz_state[MAX_HAZ], haz_said[MAX_HAZ];

    uint8_t state;
    uint16_t frame;
    int16_t state_timer;
    Car car;
    Ai ai;
    uint32_t rng;

    int32_t score;
    int16_t cover, mult;
    uint16_t ap_frames;
    uint32_t dist_ap, dist_all;          /* this route, fine units / 16 (units) */
    uint8_t dist_apf, dist_allf;
    int16_t saves, needless, takeovers;
    uint8_t ap_rem;
    uint8_t knocked[MAX_SEGS / 8 + 1];
    int8_t pending_save;
    uint8_t incident;
    int32_t route_bonus;
    int16_t total_saves, total_needless;

    uint8_t msg;
    int16_t msg_timer;
    uint8_t glare;                       /* 0..16 */
    uint8_t siren;
    Event ev[MAX_EVENTS];
    uint8_t nev;
    uint8_t last_in;
    uint8_t ai_perfect;                  /* the title's demo, and tests */
} World;

extern World w;

/* track.c */
void route_build(uint8_t route);
int16_t seg_of(uint16_t z);
int16_t road_cx(uint16_t z, uint8_t zf);  /* units */
int16_t road_y(uint16_t z);
int16_t road_curve(uint16_t z);
int16_t road_left(void);
int16_t road_right(void);
int16_t lane_centre(int8_t lane);         /* units */
int8_t lane_of(int16_t d);                /* fine d -> our lane */
int16_t ghost_at(uint16_t z);             /* units */

/* world.c */
uint16_t sc_rand(void);
void world_init(void);
void world_start_game(void);
void world_start_route(uint8_t route);
void world_update(uint8_t input);
uint8_t world_pop_event(Event *e);
void world_say(uint8_t msg);
int8_t world_hazard_ahead(int16_t segs);
void world_set_ap(uint8_t on);
uint8_t world_ap_allowed(void);
int16_t world_ap_pct(void);
uint8_t cone_at(int16_t seg, int16_t *d);
uint8_t aqua_kmh(void);
uint8_t world_scene(uint8_t hz_type);    /* (tests) just before a hazard */
const char *hz_name(uint8_t type);

#endif
