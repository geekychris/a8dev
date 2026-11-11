/*
 * world.c - ScAIry Fun's simulation (Atari edition), one step per 1/25 s:
 * the car, the autopilot, traffic, the hazards, damage and the score, and
 * the route -> arrival -> software update -> next route loop.
 *
 * The original's world.c, simplified to fit (its 32-bit C would be ~38K of
 * 6502 code; this is about a third of that). Kept as the original has them:
 * the car's handling (steering, the bends' pull, aquaplaning, what lies
 * beside the road), the autopilot's plan, steering and speed, and its blind
 * spots (old lane lines, floods, overpass shadows, low sun, cones, debris,
 * ambulances), the bait it gets right (deer, cyclist, tractor, merging
 * truck, goats, pedestrians), damage, takeovers judged as saves or
 * needless, the multiplier and the arrival bonus. Simplified: the traffic
 * (10 objects, not 40; oncoming cars stop for the car rather than queueing
 * at a detour), lane changes (overtake and keep right; the perfect
 * autopilot of the title's demo doesn't change lane for cones or
 * ambulances), and the AI's picture of the world (no detection list: its
 * speed looks at the objects directly).
 */
#include <string.h>
#include "sc.h"

#pragma static-locals (on)

#define CAR_LEN    36
#define CAR_WID    15
#define F(u)       ((int16_t)(u) * 16)              /* units -> fine */

World w;

uint8_t obj_type[MAX_OBJS], obj_flags[MAX_OBJS], obj_colour[MAX_OBJS];
int8_t obj_hz[MAX_OBJS], obj_lane[MAX_OBJS];
uint16_t obj_z[MAX_OBJS];
uint8_t obj_zf[MAX_OBJS];
int16_t obj_d[MAX_OBJS], obj_v[MAX_OBJS], obj_vd[MAX_OBJS], obj_vt[MAX_OBJS];
int16_t obj_timer[MAX_OBJS], obj_cool[MAX_OBJS];

const char *const msg_text[MSG_COUNT] = {
    "", "AUTOPILOT ENGAGED", "AUTOPILOT DISENGAGED", "I HAD THAT.",
    "GOOD CATCH.", "RECALCULATING...", "THAT IS A ROAD. PROBABLY.",
    "OBSTACLE DETECTED!", "KEEP YOUR HANDS ON THE WHEEL", "OOPS.",
    "UPDATE INSTALLED. I AM BETTER NOW.", "AUTOPILOT UNAVAILABLE",
    "WELCOME TO SCAIRY FUN", "WATER DETECTED. PROCEEDING ANYWAY.",
    "YOU HAVE ARRIVED.", "CAMERA BLINDED", "I AM VERY PATIENT.",
    "TICKET: FAILED TO YIELD", "TOW TRUCK ON ITS WAY", "POLICY CANCELLED"
};

const char *const ev_name[EV_COUNT] = {
    "none", "ap_on", "ap_off", "unavailable", "save", "needless", "scrape",
    "cone", "debris", "rearend", "crash", "splash", "roll", "ticket",
    "respawn", "phantom", "blinded", "aquaplane", "arrived", "game_over",
    "mult_up", "siren", "route_start", "hit_animal", "say", "win"
};

static const char *const hz_names[HZ_COUNT] = {
    "none", "flood", "ghost", "phantom", "cones", "glare", "siren", "debris",
    "deer", "cyclist", "tractor", "merge", "goats", "peds"
};

/* half length, half width, units */
static const uint8_t obj_len[OBJ_COUNT] = { 0, 36, 90, 30, 14, 16, 4, 10, 50, 10, 5 };
static const uint8_t obj_wid[OBJ_COUNT] = { 0, 15, 20, 16, 5, 6, 4, 10, 17, 4, 5 };

const char *hz_name(uint8_t type) { return type < HZ_COUNT ? hz_names[type] : "?"; }

/* this step's view of each object from the car (objects_update keeps them) */
static int16_t rz[MAX_OBJS];             /* units ahead of the car */
static uint8_t cur;                      /* the object being looked at */
static const RouteInfo *r;
static int16_t cs;                       /* the car's segment */

static int16_t iabs(int16_t x) { return x < 0 ? -x : x; }

/* |obj_d[i] - d| < u units */
static uint8_t near_d(uint8_t i, int16_t d, uint8_t u) { return iabs(obj_d[i] - d) < F(u); }

uint16_t sc_rand(void)
{
    uint16_t x = (uint16_t)w.rng;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    w.rng = x ? x : 1;
    return x;
}

static uint8_t rnd(uint8_t n) { return (uint8_t)(sc_rand() % n); }

/* z + v (fine) */
static void advance(uint16_t *z, uint8_t *zf, int16_t v)
{
    int16_t t = (int16_t)*zf + (v & 15);
    *z += (v >> 4) + (t >> 4);
    *zf = (uint8_t)(t & 15);
}

/* v towards t: up by `up`, down by `dn` a step */
static int16_t approach(int16_t v, int16_t t, uint8_t up, uint8_t dn)
{
    if (v < t) { v += up; if (v > t) v = t; }
    else if (v > t) { v -= dn; if (v < t) v = t; }
    return v;
}

static void push(uint8_t type, int16_t x, int16_t y)
{
    if (w.nev < MAX_EVENTS) {
        w.ev[w.nev].type = type;
        w.ev[w.nev].a = x;
        w.ev[w.nev].b = y;
        w.nev++;
    }
}

uint8_t world_pop_event(Event *e)
{
    if (!w.nev) return 0;
    *e = w.ev[0];
    memmove(&w.ev[0], &w.ev[1], (w.nev - 1) * sizeof(Event));
    w.nev--;
    return 1;
}

void world_say(uint8_t msg)
{
    w.msg = msg;
    w.msg_timer = 75;
    push(EV_SAY, msg, 0);
}

/* ---------- damage ---------- */
static void hurt(uint8_t n, uint8_t ev)
{
    w.incident = 1;
    if (w.state != GS_PLAY) n = 0;
    w.cover = w.cover > n ? w.cover - n : 0;
    push(ev, n, w.cover);
    if (w.cover == 0 && w.state == GS_PLAY) {
        w.state = GS_GAMEOVER;
        w.state_timer = 50;
        w.ai.on = 0;
        push(EV_GAME_OVER, 0, w.route);
        world_say(MSG_CANCELLED);
    }
}

static void oops(uint8_t n, uint8_t ev)
{
    hurt(n, ev);
    world_say(MSG_OOPS);
}

/* ---------- objects ---------- */
static int8_t spawn(uint8_t type, uint16_t z, int16_t du, int16_t v, uint8_t flags)
{
    static uint8_t i;
    for (i = 0; i < MAX_OBJS; i++) {
        if (obj_type[i] != OBJ_NONE) continue;
        obj_type[i] = type;
        obj_flags[i] = flags;
        obj_hz[i] = -1;
        obj_z[i] = z;
        obj_zf[i] = 0;
        obj_d[i] = F(du);
        obj_v[i] = obj_vt[i] = v;
        obj_vd[i] = obj_timer[i] = obj_cool[i] = 0;
        obj_lane[i] = du < 0 ? -1 : lane_of(F(du));
        obj_colour[i] = (uint8_t)(sc_rand() & 3);
        rz[i] = (int16_t)(z - w.car.z);
        return (int8_t)i;
    }
    return -1;
}

/* is lane free of objects (not `skip`) from s0 to s1 segments around the car? */
static uint8_t lane_clear(int8_t lane, int8_t s0, int8_t s1, int8_t skip)
{
    static uint8_t i;
    for (i = 0; i < MAX_OBJS; i++)
        if ((int8_t)i != skip && obj_type[i] && obj_lane[i] == lane &&
            rz[i] > s0 * SEG_LEN && rz[i] < s1 * SEG_LEN) return 0;
    return 1;
}

static int16_t lane_d(int8_t lane) { return F(lane_centre(lane)); }

static int8_t car_lane(void) { return w.car.d >= 0 ? lane_of(w.car.d) : 0; }

/* to pass something small in the lane: the shoulder if there's room */
static int16_t swerve_to(uint8_t o)
{
    int16_t right = (obj_d[o] >> FBITS) + obj_wid[obj_type[o]] + CAR_WID + 12;
    if (right + CAR_WID <= road_right() + SHOULDER) return F(right);
    return obj_d[o] - F(obj_wid[obj_type[o]] + CAR_WID + 12);
}

uint8_t cone_at(int16_t seg, int16_t *d)
{
    static int16_t start, i;
    static int8_t lane;
    if (seg < 0 || seg >= w.nseg || !(seg_flags[seg] & SF_CONES)) return 0;
    for (lane = 0; lane < 7 && !(seg_closed[seg] & (1 << lane)); lane++)
        ;
    for (start = seg; start > 0 && (seg_flags[start - 1] & SF_CONES); start--)
        ;
    i = seg - start;
    if (i < 3) {
        *d = lane * LANE_W + ((i + 1) * LANE_W) / 4;
        return 1;
    }
    if (i % 3 == 0) {
        *d = lane_centre(lane);
        return 1;
    }
    return 0;
}

static void spawn_hazard(uint8_t i)
{
    static const Hazard *h;
    static uint16_t z;
    static int16_t rr;
    static int8_t k, o;
    h = &w.haz[i];
    z = (uint16_t)h->seg << SEG_BITS;
    rr = road_right();
    o = -1;
    switch (h->type) {
    case HZ_CYCLIST:
        o = spawn(OBJ_CYCLIST, z, rr - 10, FROM_KMH(20), 0);
        break;
    case HZ_DEER:
        o = spawn(OBJ_DEER, z, rr + 50, 0, 0);
        break;
    case HZ_GOATS:
    case HZ_PEDS:
        for (k = 0; k < 3; k++) {
            o = spawn(h->type == HZ_GOATS ? OBJ_GOAT : OBJ_PED, z + k * (SEG_LEN / 3), rr + 40 + k * 22, 0, 0);
            if (o >= 0) obj_hz[o] = (int8_t)i;
        }
        o = -1;
        break;
    case HZ_TRACTOR:
        o = spawn(OBJ_TRACTOR, z, lane_centre(r->nl - 1), FROM_KMH(25), 0);
        break;
    case HZ_MERGE:
        o = spawn(OBJ_TRUCK, z, rr + 34, FROM_KMH(60), 0);
        if (o >= 0) obj_lane[o] = r->nl;
        break;
    case HZ_PHANTOM:
        o = spawn(OBJ_TRUCK, w.car.z - 4 * SEG_LEN, lane_centre(car_lane()), w.car.v + FROM_KMH(15), OF_TAILGATE);
        break;
    case HZ_SIREN:
        o = spawn(OBJ_AMBULANCE, w.car.z - 12 * SEG_LEN, lane_centre(car_lane()), w.car.v + FROM_KMH(40), 0);
        w.siren = 1;
        push(EV_SIREN, h->seg, 0);
        break;
    case HZ_DEBRIS:
        o = spawn(OBJ_DEBRIS, z, lane_centre(h->param >= 0 ? (int8_t)h->param : car_lane()), 0, 0);
        break;
    }
    if (o >= 0) obj_hz[o] = (int8_t)i;
}

static void resolve_hazard(uint8_t i)
{
    w.haz_state[i] = 2;
    if (w.pending_save != (int8_t)i) return;
    if (!w.incident && w.state == GS_PLAY) {
        w.saves++;
        w.total_saves++;
        w.score += 500;
        push(EV_SAVE, w.haz[i].type, w.saves);
        world_say(MSG_GOOD_CATCH);
    }
    w.pending_save = -1;
}

static void hazards_update(void)
{
    static uint8_t i;
    static const Hazard *h;
    for (i = 0; i < w.nhaz; i++) {
        h = &w.haz[i];
        if (w.haz_state[i] == 0) {
            if (h->type == HZ_SIREN ? cs >= h->seg : h->seg - cs <= 32) {
                w.haz_state[i] = 1;
                spawn_hazard(i);
            }
        } else if (w.haz_state[i] == 1 && h->type != HZ_SIREN && cs > h->seg + h->len + 2) {
            resolve_hazard(i);
        }
    }
}

int8_t world_hazard_ahead(int16_t segs)
{
    static uint8_t i;
    static const Hazard *h;
    cs = seg_of(w.car.z);
    for (i = 0; i < w.nhaz; i++) {
        h = &w.haz[i];
        if (!HZ_FAILS(h->type) || w.haz_state[i] == 2) continue;
        if (h->type == HZ_SIREN) {
            if (w.haz_state[i] == 1) return (int8_t)i;
            continue;
        }
        if (h->seg - cs <= segs && cs <= h->seg + h->len + 1) return (int8_t)i;
    }
    return -1;
}

/* ---------- traffic ---------- */
static void traffic_spawn(void)
{
    static uint8_t want, n, i, type, pct;
    static int16_t seg, v;
    static int8_t lane, o;
    want = r->traffic / 3;                /* the original's density x 0.7, for 10 objects */
    if (want > 5) want = 5;
    n = 0;
    for (i = 0; i < MAX_OBJS; i++)
        if (obj_hz[i] < 0 && (obj_type[i] == OBJ_CAR || obj_type[i] == OBJ_TRUCK)) n++;
    if (n >= want || (sc_rand() & 7)) return;

    type = rnd(5) ? OBJ_CAR : OBJ_TRUCK;
    seg = cs + 30 + rnd(6);
    if (r->no && !rnd(3)) {               /* oncoming */
        lane = -1;
        pct = 85 + rnd(20);
    } else {
        lane = rnd(r->nl);
        pct = r->nl == 1 ? 92 + rnd(14) : type == OBJ_TRUCK ? 55 : 65 + rnd(25);
    }
    v = FROM_KMH(r->limit_kmh * pct / 100);
    if (lane < 0) v = -v;
    if (seg >= w.finish) return;
    if (lane >= 0 && (seg_closed[seg] & (1 << lane))) return;
    if (lane < 0 && (seg_flags[seg] & SF_CONES)) return;
    if (!lane_clear(lane, 27, 39, -1)) return;
    o = spawn(type, (uint16_t)seg << SEG_BITS, lane_centre(lane), v, v < 0 ? OF_ONCOMING : 0);
    if (o >= 0) obj_lane[o] = lane;
}

/* the speed behind whatever is ahead in the lane (the car counts too) */
static void vehicle_follow(uint8_t i)
{
    static int16_t best, lead_v, dz, gap, des, t;
    static uint8_t k;
    best = 5 * SEG_LEN;
    lead_v = 0;
    t = obj_vt[i];
    for (k = 0; k < MAX_OBJS; k++) {
        if (k == i || !obj_type[k] || obj_v[k] < 0 || obj_type[k] == OBJ_CYCLIST ||
            obj_type[k] == OBJ_DEBRIS || !near_d(k, obj_d[i], 40)) continue;
        dz = rz[k] - rz[i];
        if (dz > 0 && dz < best) { best = dz; lead_v = obj_v[k]; }
    }
    dz = -rz[i];
    if (w.car.state == CS_DRIVING && dz > 0 && dz < best &&
        (near_d(i, w.car.d, 40) || (w.car.d >= 0 && lane_of(w.car.d) == obj_lane[i]))) {
        best = dz;
        lead_v = w.car.v;
    }
    if (best < 5 * SEG_LEN) {
        gap = best - obj_len[obj_type[i]] - CAR_LEN;
        des = (obj_v[i] * 25) / 16 + 30;
        if (gap < des) {
            des = lead_v + (gap - des) * 2;
            if (des < 0) des = 0;
            if (des < t) t = des;
        }
    }
    obj_v[i] = approach(obj_v[i], t, 3, 10);
}

static void move_across(uint8_t i, int16_t target, uint8_t rate)
{
    obj_d[i] = approach(obj_d[i], target, rate, rate);
}

static void objects_update(void)
{
    static uint8_t i, k, type;
    static int16_t dz, gap, t, to, s;
    for (i = 0; i < MAX_OBJS; i++) {
        cur = i;
        type = obj_type[i];
        if (!type) continue;
        if (obj_cool[i] > 0) obj_cool[i]--;
        dz = rz[i];
        gap = (dz < 0 ? -dz : dz) - obj_len[type] - CAR_LEN;

        if (obj_flags[i] & OF_ONCOMING) {
            /* stop for the car in the way; after a while, over to the kerb
               until it is past */
            if (obj_flags[i] & OF_KNOCKED) obj_v[i] = 0;
            else {
                to = lane_d(-1);
                if (obj_flags[i] & OF_PASSING) {
                    to = F(road_left() - 26);
                    if (dz < -2 * SEG_LEN) obj_flags[i] &= ~OF_PASSING;
                } else if (near_d(i, w.car.d, 34) && dz > 0 && gap < 4 * SEG_LEN) {
                    obj_v[i] = approach(obj_v[i], 0, 10, 10);
                    if (++obj_timer[i] > 50) obj_flags[i] |= OF_PASSING;
                } else
                    obj_v[i] = approach(obj_v[i], obj_vt[i], 4, 4);
                move_across(i, to, 16);
            }
        } else switch (type) {
        case OBJ_CAR:
        case OBJ_TRUCK:
            if (obj_flags[i] & OF_TAILGATE) {
                /* close behind, reacting to the car half a second late */
                t = w.car.hist[(uint8_t)(w.car.hist_pos - 12) & 31];
                gap = t + (gap - 48) * 2;
                if (gap > t + FROM_KMH(30)) gap = t + FROM_KMH(30);
                if (gap < 0) gap = 0;
                obj_v[i] = approach(obj_v[i], gap, 6, 12);
                if (w.car.d >= 0) obj_d[i] = lane_d(lane_of(w.car.d));
                if (obj_hz[i] >= 0 && w.haz_state[obj_hz[i]] == 2) {
                    obj_flags[i] &= ~OF_TAILGATE;
                    obj_vt[i] = FROM_KMH(r->limit_kmh);
                    obj_lane[i] = 0;
                }
                break;
            }
            if (obj_lane[i] == r->nl) {    /* the merging truck, on the slip road */
                if (dz > 0 && dz < 8 * SEG_LEN) obj_lane[i] = (int8_t)w.haz[obj_hz[i]].param;
                break;
            }
            s = seg_of(obj_z[i]) + 6;      /* round a closed lane */
            if (obj_lane[i] > 0 && s < w.nseg && (seg_closed[s] & (1 << obj_lane[i])) &&
                lane_clear(obj_lane[i] - 1, -2 + (int8_t)(rz[i] >> 7), 3 + (int8_t)(rz[i] >> 7), (int8_t)i))
                obj_lane[i]--;
            vehicle_follow(i);
            to = lane_d(obj_lane[i]);
            for (k = 0; k < MAX_OBJS; k++) {   /* round a cyclist or debris */
                t = rz[k] - rz[i];
                if (!obj_type[k] || t <= -SEG_LEN || t >= 6 * SEG_LEN || !near_d(k, to, 34)) continue;
                if (obj_type[k] == OBJ_CYCLIST) to = obj_d[k] - F(34);
                else if (obj_type[k] == OBJ_DEBRIS) to = swerve_to(k);
            }
            move_across(i, to, 20);
            break;

        case OBJ_AMBULANCE:
            if (!(obj_flags[i] & OF_PASSING)) {
                /* catch up, then sit behind the car until it moves over */
                if (gap > 120) obj_v[i] = w.car.v + FROM_KMH(40);
                else {
                    obj_v[i] = w.car.v;
                    obj_timer[i]++;
                }
                if (!near_d(i, w.car.d, 45) || w.car.state != CS_DRIVING) obj_flags[i] |= OF_PASSING;
                else if (obj_timer[i] > 75) {
                    obj_flags[i] |= OF_PASSING;
                    hurt(10, EV_TICKET);
                    world_say(MSG_TICKET);
                }
            } else {
                obj_v[i] = near_d(i, w.car.d, 34) && dz < 0 ? w.car.v : w.car.v + FROM_KMH(50);
                if (near_d(i, w.car.d, 46) && dz < 2 * SEG_LEN)
                    obj_lane[i] = r->nl > 1 ? (car_lane() > 0 ? car_lane() - 1 : 1) : -1;
                move_across(i, lane_d(obj_lane[i]), 24);
                if (dz > 3 * SEG_LEN && obj_hz[i] >= 0 && w.haz_state[obj_hz[i]] == 1) {
                    w.siren = 0;
                    resolve_hazard((uint8_t)obj_hz[i]);
                }
            }
            break;

        case OBJ_TRACTOR:
            if (obj_hz[i] >= 0 && seg_of(obj_z[i]) > w.haz[obj_hz[i]].seg + w.haz[obj_hz[i]].len) {
                obj_d[i] += 20;             /* turns off into a field */
                obj_v[i] = FROM_KMH(10);
                if ((obj_d[i] >> FBITS) > road_right() + 220) obj_type[i] = OBJ_NONE;
            } else
                vehicle_follow(i);
            break;

        case OBJ_DEER:
        case OBJ_GOAT:
        case OBJ_PED:
            /* cross when the car is close */
            if (!obj_vd[i] && !obj_timer[i] && dz > 0 && dz < (type == OBJ_PED ? 6 : 5) * SEG_LEN) {
                obj_timer[i] = 1;
                obj_vd[i] = type == OBJ_PED ? -18 : -30;
            }
            obj_d[i] += obj_vd[i];
            break;
        }
        if (!obj_type[i]) continue;
        advance(&obj_z[i], &obj_zf[i], obj_v[i]);
        dz = rz[i] = (int16_t)(obj_z[i] - w.car.z);
        if (dz < -20 * SEG_LEN || dz > 48 * SEG_LEN || seg_of(obj_z[i]) >= w.nseg - 1 || obj_z[i] > 0xF000) {
            if (type == OBJ_AMBULANCE) {
                w.siren = 0;
                if (obj_hz[i] >= 0 && w.haz_state[obj_hz[i]] == 1) resolve_hazard((uint8_t)obj_hz[i]);
            }
            obj_type[i] = OBJ_NONE;
        }
    }
}

static void collide(void)
{
    static uint8_t i, ln, type;
    static int16_t dz, dd, k, cd;
    if (w.car.state != CS_DRIVING && w.car.state != CS_AQUAPLANE) return;
    for (i = 0; i < MAX_OBJS; i++) {
        type = obj_type[i];
        if (!type || obj_cool[i] || (obj_flags[i] & OF_KNOCKED)) continue;
        dz = rz[i];
        dd = (obj_d[i] - w.car.d) >> FBITS;
        ln = obj_len[type];
        if (iabs(dz) >= CAR_LEN + ln || iabs(dd) >= CAR_WID + obj_wid[type]) continue;
        if (type == OBJ_DEBRIS) {
            obj_type[i] = OBJ_NONE;
            w.car.v = w.car.v * 3 / 4;
            oops(10, EV_DEBRIS);
        } else if (type == OBJ_DEER || type == OBJ_GOAT || type == OBJ_PED || type == OBJ_CYCLIST) {
            obj_cool[i] = 1000;
            obj_vd[i] = dd > 0 ? 60 : -60;
            obj_v[i] = 0;
            w.car.v /= 2;
            oops(20, EV_HIT_ANIMAL);
        } else {
            obj_cool[i] = 50;
            if (obj_v[i] < 0) {            /* head on: the other car a wreck at the roadside */
                w.car.v = obj_v[i] = 0;
                obj_flags[i] |= OF_KNOCKED;
                obj_d[i] -= F(48);
                oops(35, EV_CRASH);
            } else if (dz > 0) {
                w.car.v = obj_v[i] * 3 / 4;
                w.car.z = obj_z[i] - (CAR_LEN + ln + 2);
                oops(25, EV_CRASH);
            } else {
                obj_v[i] = w.car.v * 3 / 4;
                obj_z[i] = w.car.z - (CAR_LEN + ln + 2);
                obj_flags[i] &= ~OF_TAILGATE;
                oops(15, EV_REAREND);
            }
        }
    }
    /* cones in a closed lane */
    for (k = cs; k <= cs + 1; k++) {
        if (k >= w.nseg || (w.knocked[k >> 3] & (1 << (k & 7))) || !cone_at(k, &cd)) continue;
        if (iabs((int16_t)(((uint16_t)k << SEG_BITS) - w.car.z)) < CAR_LEN + 4 &&
            iabs((w.car.d >> FBITS) - cd) < CAR_WID + 4) {
            w.knocked[k >> 3] |= (uint8_t)(1 << (k & 7));
            w.car.v = w.car.v * 7 / 8;
            hurt(2, EV_CONE);
        }
    }
}

/* ---------- the autopilot ---------- */
uint8_t world_ap_allowed(void)
{
    if (w.car.state != CS_DRIVING || w.car.d < 0) return 0;
    if (w.state != GS_PLAY && w.state != GS_TITLE) return 0;
    return iabs((w.car.d >> FBITS) - lane_centre(lane_of(w.car.d))) <= LANE_W / 4;
}

void world_set_ap(uint8_t on)
{
    static int8_t h;
    if (on && !w.ai.on) {
        if (!world_ap_allowed()) {
            push(EV_UNAVAILABLE, 0, 0);
            world_say(MSG_UNAVAILABLE);
            return;
        }
        w.ai.on = 1;
        w.ai.lane = lane_of(w.car.d);
        w.ap_frames = 0;
        push(EV_AP_ON, 0, 0);
        world_say(MSG_AP_ON);
    } else if (!on && w.ai.on) {
        /* a takeover: a save if a hazard it would get wrong is ahead */
        w.ai.on = 0;
        w.takeovers++;
        w.mult = 1;
        w.ap_frames = 0;
        push(EV_AP_OFF, w.takeovers, 0);
        if (w.state != GS_PLAY) return;
        h = world_hazard_ahead(24);
        if (h >= 0) {
            w.pending_save = h;
            w.incident = 0;
            world_say(MSG_AP_OFF);
        } else {
            w.needless++;
            w.total_needless++;
            push(EV_NEEDLESS, w.needless, 0);
            world_say(MSG_HAD_THAT);
        }
    }
}

/* lane changes on the freeway: overtake something slow, then keep right */
static void ai_lanes(void)
{
    static uint8_t i;
    if (w.ai.lane_hold > 0) { w.ai.lane_hold--; return; }
    if (r->nl < 2 || w.ai.lane < 0) return;
    for (i = 0; i < MAX_OBJS; i++) {
        if (!obj_type[i] || obj_lane[i] != w.ai.lane || rz[i] <= 0 || rz[i] > 6 * SEG_LEN ||
            obj_type[i] > OBJ_TRACTOR) continue;
        if (obj_v[i] < w.ai.target_v - FROM_KMH(10) && w.ai.lane > 0 && lane_clear(w.ai.lane - 1, -4, 5, -1)) {
            w.ai.lane--;
            w.ai.lane_hold = 100;
            return;
        }
    }
    if (w.ai.lane < r->nl - 1 && lane_clear(w.ai.lane + 1, -3, 7, -1)) {
        for (i = 0; i < 16 && cs + i < w.nseg; i++)
            if (seg_closed[cs + i] & (1 << (w.ai.lane + 1))) return;
        w.ai.lane++;
        w.ai.lane_hold = 100;
    }
}

static uint8_t perfect(void) { return w.ai_perfect || w.state == GS_TITLE; }

static void ai_think(void)
{
    static int16_t k, d, lim, pd, gap, band, later, t;
    static uint16_t z;
    static uint8_t perf, i, hit, type;
    static const Hazard *h;
    perf = perfect();

    /* the low sun */
    if (!perf && w.glare >= 12) {
        if (!w.ai.blinded) {
            w.ai.blinded = 1;
            w.ai.blind_z = w.car.z;
            w.ai.blind_x = road_cx(w.car.z, 0) + (w.car.d >> FBITS);
            w.ai.blind_dir = seg_dir[cs];
            if (w.ai.on) { push(EV_BLINDED, cs, 0); world_say(MSG_BLINDED); }
        }
    } else if (w.ai.blinded && w.glare < 6) {
        w.ai.blinded = 0;
        if (w.ai.on) world_say(MSG_RECALC);
    }

    if (w.ai.on) ai_lanes();

    /* the plan: where it means to be at each of the next 16 segments (the
       old lane lines' offset added: it follows them) */
    w.ai.plan_from = cs;
    pd = lane_d(w.ai.lane);
    for (k = 0; k < 16; k++) {
        z = (uint16_t)(cs + k) << SEG_BITS;
        if (w.ai.blinded) {
            d = w.ai.blind_x + (int16_t)(((int32_t)w.ai.blind_dir * (int16_t)(z - w.ai.blind_z)) >> SEG_BITS)
                - road_cx(z, 0);
            if (d > 1000) d = 1000;
            if (d < -1000) d = -1000;
            d = F(d);
        } else {
            d = pd;
            if (!perf && (seg_flags[cs + k] & SF_GHOST)) d += F(ghost_at(z));
        }
        w.ai.plan[k] = d;
    }
    /* give a cyclist room */
    for (i = 0; i < MAX_OBJS; i++) {
        if (obj_type[i] != OBJ_CYCLIST || rz[i] < -2 * SEG_LEN || rz[i] > 8 * SEG_LEN || w.ai.blinded) continue;
        t = (rz[i] >> SEG_BITS);
        for (k = t - 6; k <= t + 2; k++)
            if (k >= 0 && k < 16 && iabs(w.ai.plan[k] - obj_d[i]) < F(34))
                w.ai.plan[k] = obj_d[i] - F(34);
    }

    /* the overpass shadow: something huge in the lane, it decides */
    if (!perf && w.ai.on && w.ai.phantom_timer == 0) {
        for (k = cs + 1; k <= cs + 6 && k < w.nseg; k++)
            if ((seg_flags[k] & SF_OVERPASS) && !(seg_flags[k - 1] & SF_OVERPASS) && k != w.ai.phantom_seg) {
                w.ai.phantom_seg = k;
                w.ai.phantom_timer = 30;
                push(EV_PHANTOM, k, 0);
                world_say(MSG_OBSTACLE);
                break;
            }
    }
    if (w.ai.phantom_timer > 0 && --w.ai.phantom_timer == 0 && w.ai.on) world_say(MSG_RECALC);

    /* speed: the limit, slower in water (if it knew), and what's in its path
       (debris is too small to matter, it thinks; the low sun hides it all) */
    lim = FROM_KMH(r->limit_kmh);
    if (perf)
        for (k = 0; k < 9 && cs + k < w.nseg; k++)
            if (seg_flags[cs + k] & SF_FLOOD) {
                lim = FROM_KMH(aqua_kmh() * 7 / 10);
                break;
            }
    if (!w.ai.blinded)
        for (i = 0; i < MAX_OBJS; i++) {
            type = obj_type[i];
            if (!type || (type == OBJ_DEBRIS && !perf) || rz[i] <= 0 || rz[i] > 30 * SEG_LEN) continue;
            t = rz[i] >> SEG_BITS;
            pd = w.ai.plan[t > 15 ? 15 : t];
            band = F(CAR_WID + obj_wid[type] + 6);
            hit = iabs(obj_d[i] - pd) < band;
            if (!hit && obj_vd[i]) {           /* about to cross it */
                later = obj_d[i] + obj_vd[i] * 45;
                hit = (obj_d[i] > pd + band && later < pd + band) || (obj_d[i] < pd - band && later > pd - band);
            }
            if (!hit) continue;
            gap = rz[i] - obj_len[type] - CAR_LEN;
            if (obj_v[i] <= 0) {
                if (gap < (w.car.v >> 4) * (w.car.v >> 4) + 110) lim = 0;
            } else {
                d = (w.car.v * 25) / 16 + 30;
                if (gap < d) {
                    t = obj_v[i] + (gap - d) * 2;
                    if (t < 0) t = 0;
                    if (t < lim) lim = t;
                }
            }
        }
    if (w.ai.phantom_timer > 0) lim = 0;
    if (cs >= w.finish) lim = w.nseg - 6 > cs ? FROM_KMH(30) : 0;
    w.ai.target_v = lim;

    /* comments */
    for (i = 0; i < w.nhaz; i++) {
        h = &w.haz[i];
        if (w.haz_said[i] || !w.ai.on || w.state != GS_PLAY) continue;
        if ((h->type == HZ_FLOOD && w.route > 0 && cs >= h->seg - 3 && cs < h->seg) ||
            (h->type == HZ_GHOST && ghost_at(w.car.z + 4 * SEG_LEN) > LANE_W / 2) ||
            (h->type == HZ_TRACTOR && cs > h->seg + 6 && cs < h->seg + h->len && w.car.v < FROM_KMH(30))) {
            w.haz_said[i] = 1;
            world_say(h->type == HZ_FLOOD ? MSG_WATER_ANYWAY : h->type == HZ_GHOST ? MSG_ROAD_PROBABLY : MSG_PATIENT);
        }
    }
}

/* ---------- the car ---------- */
uint8_t aqua_kmh(void)
{
    return w.route == 3 ? 30 : 50;          /* the town's streets are deeper */
}

static void place(uint16_t z)
{
    w.car.z = z;
    w.car.zf = 0;
    w.car.d = lane_d(r->nl - 1);
    w.car.s = w.car.lat = 0;
    w.car.state = CS_DRIVING;
    w.car.cool = 0;
    w.ai.lane = r->nl - 1;
    w.ai.blinded = 0;
    w.mult = 1;
    w.ap_frames = 0;
}

static void respawn(void)
{
    static int16_t s;
    static uint8_t i;
    s = cs - 4;
    while (s > 2 && (seg_flags[s] & (SF_FLOOD | SF_GHOST | SF_CONES | SF_GLARE))) s--;
    for (i = 0; i < w.nhaz; i++)
        if (w.haz[i].seg >= s && w.haz[i].seg < s + 10) s = w.haz[i].seg - 10;
    if (s < 2) s = 2;
    place((uint16_t)s << SEG_BITS);
    w.car.v = 0;
    w.ai.on = 0;
    for (i = 0; i < MAX_OBJS; i++)
        if (obj_type[i] && iabs((int16_t)(obj_z[i] - w.car.z)) < 4 * SEG_LEN &&
            obj_type[i] != OBJ_DEBRIS && obj_type[i] != OBJ_AMBULANCE) obj_type[i] = OBJ_NONE;
    push(EV_RESPAWN, s, 0);
}

static void off_road(uint8_t side, int16_t depth, int16_t edge)
{
    static uint8_t t, rail;
    t = side ? seg_side[cs] >> 4 : seg_side[cs] & 15;
    rail = seg_flags[cs] & (side ? SF_RAIL_R : SF_RAIL_L);
    if (rail || t == SIDE_MEDIAN || t == SIDE_WALL) {
        w.car.d = F(edge + (side ? -2 : 2));
        w.car.s = 0;
        if (!w.car.cool) {
            w.car.v = w.car.v * 3 / 4;
            w.car.cool = 15;
            hurt(rail ? 5 : t == SIDE_MEDIAN ? 8 : 10, EV_SCRAPE);
        }
    } else if (t == SIDE_GRASS) {
        if (w.car.v > FROM_KMH(40)) w.car.v -= 6;
        if (depth > 160) w.car.d = F(edge + (side ? 160 : -160));
    } else if (t == SIDE_TOWN) {
        if (depth > 40) {
            w.car.d = F(edge + (side ? 38 : -38));
            if (!w.car.cool) {
                w.car.v /= 3;
                w.car.cool = 25;
                oops(25, EV_CRASH);
            }
        }
    } else if (depth > ((t == SIDE_LEVEE || t == SIDE_CANAL) ? 16 : 8)) {
        t = t == SIDE_LEVEE || t == SIDE_CANAL;
        w.car.state = t ? CS_SPLASH : CS_ROLL;
        w.car.timer = 50;
        w.car.lat = side ? 24 : -24;
        w.ai.on = 0;
        hurt(40, t ? EV_SPLASH : EV_ROLL);
        if (w.state == GS_PLAY) world_say(MSG_TOW);
    }
}

static void car_update(uint8_t in)
{
    static uint8_t edge, playing;
    static int16_t pull, u, aq;
    edge = in & ~w.last_in;
    playing = w.state == GS_PLAY;

    if (w.car.cool > 0) w.car.cool--;
    if (w.car.state == CS_SPLASH || w.car.state == CS_ROLL) {
        w.car.v = w.car.v * 15 / 16;
        advance(&w.car.z, &w.car.zf, w.car.v);
        w.car.d += w.car.lat;
        if (--w.car.timer <= 0) respawn();
        return;
    }

    if (playing) {
        if (edge & IN_FIRE) world_set_ap(!w.ai.on);
        else if (w.ai.on && (in & (IN_LEFT | IN_RIGHT | IN_DOWN))) world_set_ap(0);
    }

    if (w.ai.on || !playing) {
        /* the autopilot's steering: the bend, plus the way to its plan */
        u = seg_of(w.car.z + 2 * SEG_LEN) - w.ai.plan_from;
        pull = w.ai.plan[u < 0 ? 0 : u > 15 ? 15 : u];
        pull = (((w.ai.blinded ? 0 : road_curve(w.car.z)) * w.car.v) * 9) / 40 +
               ((pull - w.car.d) >> FBITS) * 4;
        w.car.s = pull > 256 ? 256 : pull < -256 ? -256 : pull;
        w.car.v = approach(w.car.v, w.ai.target_v, 3, w.ai.phantom_timer > 0 ? 18 : 8);
    } else {
        if (in & IN_LEFT) w.car.s = w.car.s > -216 ? w.car.s - 40 : -256;
        else if (in & IN_RIGHT) w.car.s = w.car.s < 216 ? w.car.s + 40 : 256;
        else w.car.s = approach(w.car.s, 0, 64, 64);
        if (in & IN_DOWN) w.car.v -= 12;
        else if (in & IN_UP) w.car.v += 4;
        else w.car.v -= 1;
        if (w.car.v < 0) w.car.v = 0;
        if (w.car.v > FROM_KMH(MAX_KMH)) w.car.v = FROM_KMH(MAX_KMH);
    }

    /* the bend's pull, k v^2 / 8000, in 16 bits: (v/4)^2 / 50 * k / 10 */
    u = w.car.v >> 2;
    pull = (((u * u) / 50) * road_curve(w.car.z)) / 10;
    aq = FROM_KMH(aqua_kmh());
    if ((seg_flags[cs] & SF_FLOOD) && w.car.v > aq) {
        if (w.car.state != CS_AQUAPLANE) push(EV_AQUAPLANE, KMH(w.car.v), 0);
        w.car.state = CS_AQUAPLANE;
        w.car.lat = -((w.car.v - aq) * 3) / 2;
        w.car.v -= 1;
    } else {
        w.car.state = CS_DRIVING;
        w.car.lat = (w.car.s * (w.car.v >> 3)) / 225 - pull;      /* s v / 1800 */
    }
    w.car.d += w.car.lat;
    advance(&w.car.z, &w.car.zf, w.car.v);

    u = w.car.d >> FBITS;
    if (u < road_left() - SHOULDER) off_road(0, road_left() - SHOULDER - u, road_left() - SHOULDER);
    else if (u > road_right() + SHOULDER) off_road(1, u - road_right() - SHOULDER, road_right() + SHOULDER);

    w.car.hist[w.car.hist_pos & 31] = w.car.v;
    w.car.hist_pos++;
}

/* ---------- route and game flow ---------- */
static void place_car(void)
{
    memset(&w.car, 0, sizeof(w.car));
    memset(&w.ai, 0, sizeof(w.ai));
    place(3 * SEG_LEN);
    w.car.v = FROM_KMH(r->limit_kmh * 7 / 10);
    w.ai.on = 1;
    w.ai.phantom_seg = -1;
}

void world_start_route(uint8_t route)
{
    route_build(route);
    r = &route_info[route];
    memset(obj_type, 0, sizeof(obj_type));
    memset(w.knocked, 0, sizeof(w.knocked));
    memset(w.haz_said, 0, sizeof(w.haz_said));
    place_car();
    w.ap_rem = 0;
    w.dist_ap = w.dist_all = 0;
    w.saves = w.needless = w.takeovers = 0;
    w.pending_save = -1;
    w.incident = 0;
    w.route_bonus = 0;
    w.glare = 0;
    w.siren = 0;
    w.state = GS_INTRO;
    w.state_timer = 100;
    push(EV_ROUTE_START, route, 0);
}

void world_start_game(void)
{
    w.score = 0;
    w.cover = 100;
    w.total_saves = w.total_needless = 0;
    world_start_route(0);
    world_say(MSG_HANDS);
}

void world_init(void)
{
    uint8_t keep = w.ai_perfect;
    memset(&w, 0, sizeof(w));
    w.ai_perfect = keep;
    w.rng = 12345;
    world_start_route(0);
    w.nev = 0;
    w.state = GS_TITLE;
    w.cover = 100;
}

int16_t world_ap_pct(void)
{
    uint16_t all = (uint16_t)(w.dist_all >> 8), ap = (uint16_t)(w.dist_ap >> 8);
    return all ? (int16_t)(((uint32_t)ap * 100) / all) : 100;
}

static void score_update(void)
{
    static int16_t m, s;
    if (w.state != GS_PLAY || w.car.state >= CS_SPLASH) return;
    w.dist_all += w.car.v;
    if (!w.ai.on) return;
    w.dist_ap += w.car.v;
    s = w.ap_rem + w.car.v * w.mult;
    w.score += s >> 8;
    w.ap_rem = (uint8_t)s;
    w.ap_frames++;
    m = 1 + w.ap_frames / 200;              /* x1 more every 8 s, to x8 */
    if (m > 8) m = 8;
    if (m > w.mult) {
        w.mult = m;
        push(EV_MULT_UP, m, 0);
    }
}

void world_update(uint8_t in)
{
    static uint8_t edge, raw, k, g;
    edge = in & ~w.last_in;
    raw = in;
    r = &route_info[w.route];
    cs = seg_of(w.car.z);
    if (cs > w.nseg - 2) cs = w.nseg - 2;

    w.frame++;
    if (w.msg_timer > 0 && --w.msg_timer == 0) w.msg = MSG_NONE;
    if (w.state_timer > 0) w.state_timer--;

    switch (w.state) {
    case GS_TITLE:
        if (edge & (IN_FIRE | IN_START)) { world_start_game(); w.last_in = in; return; }
        if (cs >= w.finish - 2) place_car();
        break;
    case GS_INTRO:
        if (w.state_timer == 0 || (edge & (IN_FIRE | IN_START))) {
            w.state = GS_PLAY;
            in &= ~IN_FIRE;
        }
        break;
    case GS_ARRIVED:
        if (w.state_timer == 0 || (w.state_timer < 210 && (edge & (IN_FIRE | IN_START)))) {
            if (w.route < NUM_ROUTES - 1) {
                w.state = GS_UPDATE;
                w.state_timer = 200;
                world_say(MSG_UPDATED);
            } else {
                w.state = GS_WIN;
                w.state_timer = 50;
                push(EV_WIN, 0, 0);
            }
        }
        break;
    case GS_UPDATE:
        if (w.state_timer == 0 || (w.state_timer < 160 && (edge & (IN_FIRE | IN_START))))
            world_start_route(w.route + 1);
        w.last_in = in;
        return;
    case GS_GAMEOVER:
    case GS_WIN:
        if (w.state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            world_init();
            w.last_in = in;
            return;
        }
        break;
    }

    /* the sun: glare builds up over the segments ahead */
    g = 0;
    for (k = 0; k < 4 && cs + k < w.nseg; k++)
        if (seg_flags[cs + k] & SF_GLARE) { g = 16; break; }
    if (w.glare < g) w.glare++;
    else if (w.glare > g) w.glare--;

    if (w.state == GS_PLAY || w.state == GS_TITLE) {
        hazards_update();
        traffic_spawn();
    }
    ai_think();
    car_update(w.state == GS_PLAY ? in : 0);
    for (k = 0; k < MAX_OBJS; k++) rz[k] = (int16_t)(obj_z[k] - w.car.z);
    objects_update();
    collide();
    score_update();

    if (w.state == GS_PLAY && cs >= w.finish && w.car.state == CS_DRIVING) {
        /* arrived: bonus = autopilot % x 50 + saves x 500, cover back */
        w.route_bonus = (int32_t)world_ap_pct() * 50 + (int32_t)w.saves * 500;
        w.score += w.route_bonus;
        w.cover = w.cover + 30 > 100 ? 100 : w.cover + 30;
        w.state = GS_ARRIVED;
        w.state_timer = 250;
        w.ai.on = 1;
        w.ai.blinded = 0;
        w.pending_save = -1;
        push(EV_ARRIVED, w.route, 0);
        world_say(MSG_ARRIVED);
    }
    w.last_in = raw;
}

/* (tests) the car just before the first hazard of a type on this route,
   or the finish (0) */
uint8_t world_scene(uint8_t t)
{
    static int16_t s;
    static uint8_t i;
    world_start_route(w.route);
    s = w.finish - 12;
    if (t) {
        for (i = 0; i < w.nhaz && w.haz[i].type != t; i++)
            ;
        if (i == w.nhaz) return 0;
        s = t == HZ_SIREN ? w.haz[i].seg - 1 : w.haz[i].seg - 22;
    }
    if (s < 3) s = 3;
    w.car.z = (uint16_t)s << SEG_BITS;
    for (i = 0; i < w.nhaz; i++)
        if (w.haz[i].seg + w.haz[i].len + 2 < s) w.haz_state[i] = 2;
    w.state = GS_PLAY;
    w.nev = 0;
    return 1;
}
