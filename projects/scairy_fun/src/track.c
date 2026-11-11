/*
 * track.c - the original's four routes (amiga_mcp/examples/scairy_fun/
 * track.c, the same sections and hazards), built into 8 m segments held as
 * struct-of-arrays in 16 bits.
 */
#include "sc.h"

const RouteInfo route_info[NUM_ROUTES] = {
    /* name            destination      AI     nl no limit traffic */
    { "LEVEE ROAD",    "THE MARINA",    "1.0",  1, 1,  80,  10 },
    { "RUSH HOUR",     "THE OFFICE",    "2.0",  3, 0, 110,  34 },
    { "MOUNTAIN PASS", "THE SKI LODGE", "3.0",  1, 1,  70,   8 },
    { "FLOODED TOWN",  "HOME",          "4.0",  1, 1,  50,  12 },
};

int16_t seg_cx[MAX_SEGS], seg_y[MAX_SEGS];
int8_t seg_dir[MAX_SEGS];
uint8_t seg_side[MAX_SEGS], seg_flags[MAX_SEGS], seg_closed[MAX_SEGS], seg_ghost[MAX_SEGS];

typedef struct {
    uint8_t len;
    int8_t turn, hill;
    uint8_t sides, flags;                /* left | right << 4 */
} Sec;

#define S(l, r) ((l) | ((r) << 4))
#define L SIDE_LEVEE
#define D SIDE_DROP
#define G SIDE_GRASS
#define W SIDE_WALL
#define C SIDE_CLIFF
#define M SIDE_MEDIAN
#define T SIDE_TOWN
#define K SIDE_CANAL
#define RL SF_RAIL_L
#define RR SF_RAIL_R

static const Sec levee[] = {
    {  40,   0,   0, S(L, D), 0 },
    {  24,  36,   0, S(L, D), 0 },
    {  24, -36,   0, S(L, D), 0 },
    {  50,   0,   0, S(L, D), 0 },
    {  30, -40,   0, S(L, D), 0 },
    {  30,  40,   0, S(L, D), 0 },
    {  40,   0,  60, S(L, D), 0 },
    {  40,  30, -60, S(L, D), 0 },
    {  40, -30,   0, S(L, D), 0 },
    {  60,   0,   0, S(L, D), 0 },
    {  60,   0,   0, S(L, G), 0 },
    {   0 }
};

static const Sec freeway[] = {
    {  50,   0,   0, S(M, G), 0 },
    {  40,  30,  40, S(M, D), RR },
    {  40, -30, -40, S(M, D), RR },
    {  60,   0,   0, S(M, G), 0 },
    {  50, -24,   0, S(M, G), 0 },
    {  50,  24,   0, S(M, G), 0 },
    {  60,   0,  50, S(M, D), RR },
    {  40,   0, -50, S(M, G), 0 },
    {  80,   0,   0, S(M, G), 0 },
    {   0 }
};

static const Sec mountain[] = {
    {  30,   0,   0, S(W, C), RR },
    {  30,  60,  80, S(W, C), RR },
    {  30, -60,  80, S(W, C), RR },
    {  24, -50,  60, S(W, C), RR },
    {  24,  50,  40, S(W, C), RR },
    {  40,   0,  60, S(W, C), RR },
    {  30,  56,  60, S(W, C), RR },
    {  30, -56,  40, S(W, C), 0 },
    {  30, -60,  40, S(W, C), RR },
    {  30,  60,  20, S(W, C), RR },
    {  40,   0,   0, S(W, C), RR },
    {  60,   0, -40, S(G, G), 0 },
    {   0 }
};

static const Sec town[] = {
    {  40,   0,   0, S(T, T), 0 },
    {  30,  30,   0, S(K, T), RL },
    {  30, -30,   0, S(K, T), 0 },
    {  50,   0,   0, S(K, T), 0 },
    {  30, -40,   0, S(T, T), 0 },
    {  30,  40,   0, S(T, T), 0 },
    {  60,   0,   0, S(K, T), 0 },
    {  40,  24,   0, S(T, T), 0 },
    {  40, -24,   0, S(T, T), 0 },
    {  60,   0,   0, S(T, T), 0 },
    {   0 }
};

static const Sec *const route_secs[NUM_ROUTES] = { levee, freeway, mountain, town };

static const Hazard levee_haz[] = {
    { HZ_CYCLIST,  50,  0, 0 },
    { HZ_FLOOD,   108, 12, -1 },
    { HZ_DEER,    140,  0, 0 },
    { HZ_GHOST,   152, 26, 150 },
    { HZ_TRACTOR, 236, 20, 0 },
    { HZ_FLOOD,   352, 10, -1 },
    { HZ_NONE }
};

static const Hazard freeway_haz[] = {
    { HZ_MERGE,    40,  0, 2 },
    { HZ_PHANTOM, 100,  2, 0 },
    { HZ_CONES,   170, 36, 2 },
    { HZ_SIREN,   236,  0, 0 },
    { HZ_PHANTOM, 300,  2, 0 },
    { HZ_DEBRIS,  360,  0, -1 },
    { HZ_NONE }
};

static const Hazard mountain_haz[] = {
    { HZ_GLARE,    88, 22, 0 },
    { HZ_DEBRIS,  150,  0, -1 },
    { HZ_GOATS,   185,  0, 0 },
    { HZ_GHOST,   206, 24, 140 },
    { HZ_GLARE,   266, 22, 0 },
    { HZ_DEBRIS,  330,  0, -1 },
    { HZ_NONE }
};

static const Hazard town_haz[] = {
    { HZ_PEDS,     30,  0, 0 },
    { HZ_FLOOD,    84, 14, -1 },
    { HZ_CONES,   150, 30, 0 },
    { HZ_SIREN,   205,  0, 0 },
    { HZ_FLOOD,   252, 12, -1 },
    { HZ_PHANTOM, 300,  2, 0 },
    { HZ_PEDS,    330,  0, 0 },
    { HZ_NONE }
};

static const Hazard *const route_haz[NUM_ROUTES] = { levee_haz, freeway_haz, mountain_haz, town_haz };

void route_build(uint8_t route)
{
    static const Sec *s;
    static const Hazard *h;
    static int16_t cx, y, dir, dir0, y0, n, i, k;

    cx = y = dir = 0;
    n = 0;
    w.route = route;
    for (s = route_secs[route]; s->len && n < MAX_SEGS - RUNOUT - 1; s++) {
        dir0 = dir;
        y0 = y;
        for (i = 0; i < s->len && n < MAX_SEGS - RUNOUT - 1; i++, n++) {
            dir = dir0 + (s->turn * (i + 1)) / s->len;
            seg_cx[n] = cx;
            seg_y[n] = y;
            seg_dir[n] = (int8_t)dir;
            seg_side[n] = s->sides;
            seg_flags[n] = s->flags;
            seg_closed[n] = 0;
            seg_ghost[n] = 0;
            cx += dir;
            y = y0 + (s->hill * (i + 1)) / s->len;
        }
    }
    w.finish = n;
    for (i = 0; i < RUNOUT + 1; i++, n++) {
        seg_cx[n] = cx;
        seg_y[n] = y;
        seg_dir[n] = (int8_t)dir;
        seg_side[n] = seg_side[n - 1];
        seg_flags[n] = i < 2 ? SF_FINISH : 0;
        seg_closed[n] = 0;
        seg_ghost[n] = 0;
        cx += dir;
    }
    w.nseg = n;

    w.nhaz = 0;
    for (h = route_haz[route]; h->type != HZ_NONE && w.nhaz < MAX_HAZ; h++) {
        w.haz[w.nhaz] = *h;
        w.haz_state[w.nhaz] = 0;
        w.nhaz++;
        for (k = 0; k < h->len && h->seg + k < n; k++) {
            i = h->seg + k;
            switch (h->type) {
            case HZ_FLOOD:   seg_flags[i] |= SF_FLOOD; break;
            case HZ_PHANTOM: seg_flags[i] |= SF_OVERPASS; break;
            case HZ_GLARE:   seg_flags[i] |= SF_GLARE; break;
            case HZ_CONES:   seg_flags[i] |= SF_CONES; seg_closed[i] |= 1 << h->param; break;
            case HZ_GHOST:
                seg_flags[i] |= SF_GHOST;
                seg_ghost[i] = (uint8_t)(k * 3 >= h->len * 2 ? h->param
                                         : (h->param * k * 3) / (h->len * 2));
                break;
            }
        }
    }
}

int16_t seg_of(uint16_t z)
{
    return (int16_t)(z >> SEG_BITS);
}

static int16_t clamp_seg(int16_t i)
{
    if (i < 0) return 0;
    if (i > w.nseg - 2) return w.nseg - 2;
    return i;
}

/* the road's centre at z: the segment's start plus its slope */
int16_t road_cx(uint16_t z, uint8_t zf)
{
    int16_t j = clamp_seg(seg_of(z));
    int16_t f = (int16_t)(z - ((uint16_t)j << SEG_BITS));
    (void)zf;
    return seg_cx[j] + (int16_t)((seg_dir[j] * f) >> SEG_BITS);
}

int16_t road_y(uint16_t z)
{
    int16_t j = clamp_seg(seg_of(z));
    int16_t f = (int16_t)(z - ((uint16_t)j << SEG_BITS));
    return seg_y[j] + (((seg_y[j + 1] - seg_y[j]) * f) >> SEG_BITS);   /* (|dy| < 16: 16 bits) */
}

int16_t road_curve(uint16_t z)
{
    int16_t j = clamp_seg(seg_of(z));
    return seg_dir[j + 1] - seg_dir[j];
}

int16_t ghost_at(uint16_t z)
{
    int16_t j = clamp_seg(seg_of(z));
    int16_t f = (int16_t)(z - ((uint16_t)j << SEG_BITS));
    if (!(seg_flags[j] & SF_GHOST)) return 0;
    return seg_ghost[j] + (int16_t)(((int16_t)(seg_ghost[j + 1] - seg_ghost[j]) * f) >> SEG_BITS);
}

int16_t road_left(void)
{
    const RouteInfo *r = &route_info[w.route];
    return r->no ? -(int16_t)r->no * LANE_W : -8;
}

int16_t road_right(void)
{
    return (int16_t)route_info[w.route].nl * LANE_W;
}

/* lane * LANE_W + LANE_W / 2, for lanes -4..3 (a multiply by 56 in cc65) */
static const int16_t lane_c[8] = { -196, -140, -84, -28, 28, 84, 140, 196 };
int16_t lane_centre(int8_t lane)
{
    return lane_c[(uint8_t)(lane + 4) & 7];
}

int8_t lane_of(int16_t d)
{
    int16_t u = d >> FBITS;
    int8_t n = route_info[w.route].nl;
    if (u < 0) return -1;
    /* (u / LANE_W, by comparisons: at most three lanes) */
    if (u < LANE_W || n == 1) return 0;
    if (u < 2 * LANE_W || n == 2) return 1;
    return 2;
}
