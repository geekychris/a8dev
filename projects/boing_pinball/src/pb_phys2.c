/*
 * pb_phys2.c - the physics' cold parts (port: split from pb_phys.c so the
 * hot parts fit bank 0; these run from bank 2): the ramps, the world's
 * set-up, adding, plunging, kicking and nudging balls. The original's
 * functions, as in pb_phys.c.
 */
#include <string.h>
#include "pbz.h"

static void unit(int16_t dx, int16_t dy, int16_t d, int16_t *nx, int16_t *ny)
{
    *nx = div_q14(dx, d);
    *ny = div_q14(dy, d);
}

#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static int16_t hypot16(int16_t x, int16_t y)
{
    return (int16_t)isqrt32((uint32_t)(mul16(x, x) + mul16(y, y)));
}

static void place(Ball *b, int16_t x, int16_t y)
{
    b->x = x; b->y = y; b->xf = b->yf = 128;
}

static void emit(World *w, int8_t type, int16_t x, int16_t y, int8_t id, int8_t up, int32_t val, int8_t ball)
{
    Event *e;
    if (w->nev >= PB_MAX_EV) return;
    e = &w->ev[w->nev++];
    e->type = type; e->id = id; e->kind = 0; e->up = up;
    e->x = x >> 4; e->y = y >> 4; e->val = val; e->ball = ball;
}

/* ================================================================ the ramps */

static void ramp_at(const Ramp *r, int16_t s, int16_t *x, int16_t *y, int16_t *z,
                    int16_t *slope, int16_t *dx, int16_t *dy)
{
    int16_t L = r->prof[r->n - 1], t, seglen;
    const int16_t *a, *b;
    int8_t i = 1;
    s = clamp16(s, 0, L);
    while (i < r->n - 1 && r->prof[i] < s) i++;
    seglen = r->prof[i] - r->prof[i - 1];
    if (seglen <= 0) seglen = 16;
    t = div_q14(s - r->prof[i - 1], seglen);
    a = r->pts[i - 1]; b = r->pts[i];
    *x = a[0] * 16 + mulq14((b[0] - a[0]) * 16, t);
    *y = a[1] * 16 + mulq14((b[1] - a[1]) * 16, t);
    *z = a[2] + mulq14(b[2] - a[2], t);
    if (slope) *slope = div_q14((b[2] - a[2]) * 16, seglen);
    if (dx) *dx = div_q14((b[0] - a[0]) * 16, seglen);
    if (dy) *dy = div_q14((b[1] - a[1]) * 16, seglen);
}


void pb_world_init(World *w, Table *t)
{
    int8_t i;
    memset(w, 0, sizeof *w);
    w->t = t;
    w->nflip = t->nflip;
    for (i = 0; i < t->nflip; i++) { w->flip[i].d = t->flip[i]; w->flip[i].ang = t->flip[i].rest; w->flip[i].tip_ang = 0x7FFF; }
    for (i = 0; i < t->nseg; i++) if (t->seg[i].kind == SK_DROP) t->seg[i].up = 1;
    w->next_id = 1;
    w->flipper_power = w->active_targets = 1;
    w->substeps = 1;
}

Ball *pb_add_ball(World *w, int16_t x, int16_t y, int16_t vx, int16_t vy, int8_t state)
{
    Ball *b;
    if (w->nballs >= PB_MAX_BALLS) return 0;
    b = &w->balls[w->nballs++];
    memset(b, 0, sizeof *b);
    b->id = w->next_id++;
    place(b, x, y);
    b->vx = vx; b->vy = vy; b->state = state; b->autol = -1;
    return b;
}

void pb_remove_drained(World *w)
{
    int8_t i, n = 0;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state != BS_DRAINED) w->balls[n++] = w->balls[i];
    w->nballs = n;
}


/* A ball on a ramp: one-dimensional motion along the rail, pulled by the slope. */
void pb_ramp_step(World *w, Ball *b)
{
    const Ramp *r = &w->t->ramp[b->ramp];
    int16_t x, y, z, slope, dx, dy, L = r->prof[r->n - 1];
    ramp_at(r, b->rs, &x, &y, &z, &slope, 0, 0);
    /* rv += (-3200 slope - 120 sign(rv)) h: in V, 512 slope and 19 (port: 25 Hz) */
    b->rv -= (int16_t)(slope >> 5) + (b->rv > 0 ? 19 : b->rv < 0 ? -19 : 0);
    b->rs += (int16_t)(mul16(b->rv, 4) / 25);   /* V for 1/25 s, in P */
    ramp_at(r, b->rs, &x, &y, &z, 0, &dx, &dy);
    place(b, x, y); b->z = z;
    b->vx = mulq14(dx, b->rv); b->vy = mulq14(dy, b->rv);
    if (b->rs <= 0 && b->rv <= 0) {     /* didn't make it: rolls back out of the entrance */
        b->state = BS_PLAY; b->z = 0;
        place(b, r->pts[0][0] * 16, (r->pts[0][1] - 4) * 16);
        b->vx = 0; b->vy = b->rv < V_MM(-120) ? b->rv : V_MM(-120);
        emit(w, EV_RAMPFAIL, b->x, b->y, b->ramp, 0, 0, b->id);
    } else if (b->rs >= L) {
        int16_t v = -(ABS16(b->rv) >> 1);
        b->state = BS_PLAY; b->z = 0;
        place(b, r->exit_x, r->exit_y);
        b->vx = 0; b->vy = r->exit_vy < v ? r->exit_vy : v;
        emit(w, EV_RAMP, b->x, b->y, b->ramp, 0, 0, b->id);
    }
}


void pb_kick_saucer(World *w, Ball *b, int16_t jitter)
{
    int8_t right = w->t->saucer_x > PB_MID;            /* kicks out towards the middle */
    b->state = BS_PLAY;
    place(b, w->t->saucer_x + (right ? P_MM(-4) : P_MM(4)), w->t->saucer_y - P_MM(4));
    b->vx = V_MM(520) + jitter; b->vy = V_MM(-820);
    if (right) b->vx = -b->vx;
    b->nocatch = PB_MS(500);
    emit(w, EV_KICK, b->x, b->y, 0, 0, 0, b->id);
}

void pb_plunge(World *w, Ball *b, int16_t pull)
{
    pull = clamp16(pull, 0, 1000);
    b->state = BS_PLAY;
    b->vx = 0; b->vy = V_MM(700) + (int16_t)(mul16(pull, 172) / 10);   /* 700 + 4300 pull mm/s */
    place(b, b->x, w->t->plunger_rest + P_MM(1));
    emit(w, EV_PLUNGE, b->x, b->y, 0, 0, pull, b->id);
}

void pb_nudge(World *w, int16_t dx, int16_t dy)
{
    int8_t i;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_PLAY) { w->balls[i].vx += dx; w->balls[i].vy += dy; }
}

/* move_ball's tables (pb_phys.c, physasm.s), made here once per table */
extern int16_t mb_sx0[PB_MAX_SEG], mb_sy0[PB_MAX_SEG], mb_sx1[PB_MAX_SEG], mb_sy1[PB_MAX_SEG];
extern Seg *mb_sp[PB_MAX_SEG];
extern int16_t mb_cx[PB_MAX_CIRC], mb_cy[PB_MAX_CIRC], mb_crr[PB_MAX_CIRC];
extern const Circ *mb_cp[PB_MAX_CIRC];
extern int16_t mb_nx0[PB_MAX_SENS], mb_ny0[PB_MAX_SENS], mb_nx1[PB_MAX_SENS], mb_ny1[PB_MAX_SENS];
extern const Sensor *mb_np[PB_MAX_SENS];
extern int16_t mb_fpx[PB_MAX_FLIP], mb_fpy[PB_MAX_FLIP], mb_freach[PB_MAX_FLIP];
extern Flipper *mb_fp[PB_MAX_FLIP];
extern uint8_t mb_nflip;
extern int16_t mb_saucer_x, mb_saucer_y, mb_saucer_r;
extern const uint16_t *mb_grid_at, *mb_grid;
extern Table *mb_t;

void pb_mb_prepare(World *w)
{
    Table *t = w->t;
    int8_t i;
    mb_t = t;
    for (i = 0; i < t->nseg; i++) {
        Seg *s = &t->seg[i];
        mb_sp[i] = s;
        mb_sx0[i] = s->bb[0]; mb_sy0[i] = s->bb[1]; mb_sx1[i] = s->bb[2]; mb_sy1[i] = s->bb[3];
    }
    for (i = 0; i < t->ncirc; i++) {
        const Circ *c = &t->circ[i];
        mb_cp[i] = c;
        mb_cx[i] = c->cx; mb_cy[i] = c->cy; mb_crr[i] = PB_R + c->r;
    }
    for (i = 0; i < t->nsens; i++) {
        const Sensor *n = &t->sens[i];
        mb_np[i] = n;
        mb_nx0[i] = n->bb[0]; mb_ny0[i] = n->bb[1]; mb_nx1[i] = n->bb[2]; mb_ny1[i] = n->bb[3];
    }
    mb_nflip = (uint8_t)w->nflip;
    for (i = 0; i < w->nflip; i++) {
        Flipper *f = &w->flip[i];
        mb_fp[i] = f;
        mb_fpx[i] = f->d.px; mb_fpy[i] = f->d.py; mb_freach[i] = f->d.len + f->d.r1 + PB_R;
    }
    mb_saucer_x = t->saucer_x; mb_saucer_y = t->saucer_y; mb_saucer_r = t->saucer_r;
    mb_grid_at = t->grid_at;
    mb_grid = t->grid;
}

int8_t pb_hit_balls(Ball *a, Ball *b)
{
    int16_t dx = b->x - a->x, dy = b->y - a->y, rr = 2 * PB_R, d, nx, ny, pen, rel;
    uint32_t d2;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return 0;
    d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
    if (d2 >= (uint32_t)mul16(rr, rr) || d2 == 0) return 0;
    d = (int16_t)isqrt32(d2);
    if (d < 1) return 0;
    unit(dx, dy, d, &nx, &ny);
    pen = (rr - d) / 2;
    place(a, a->x - mulq14(nx, pen), a->y - mulq14(ny, pen));
    place(b, b->x + mulq14(nx, pen), b->y + mulq14(ny, pen));
    rel = (int16_t)((mul16(b->vx - a->vx, nx) + mul16(b->vy - a->vy, ny)) >> 14);
    if (rel < 0) {
        int16_t j = mulq8(rel, 243);                     /* 0.95 */
        a->vx += mulq14(j, nx); a->vy += mulq14(j, ny);
        b->vx -= mulq14(j, nx); b->vy -= mulq14(j, ny);
    }
    return 1;
}
