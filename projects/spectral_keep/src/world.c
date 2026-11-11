/*
 * Tiny kinematic physics for one room (Physics.cs): axis-separated moves
 * against solid boxes, gravity, pushing and lift-riding. The original's
 * world.c in 16-bit Q11 (see keep.h).
 *
 * The room's stone and brick blocks are a grid rather than bodies, so a
 * move only looks at the few cells its swept box touches.
 */
#include "keep.h"

#define EPS        2                /* Box.Overlaps eps 0.001 (Q12 4) */
#define EPS_GROUND 1                /* 0.0005 (2) */
#define GRAVITY    18               /* 22 u/s^2 (36) */
#define MAXFALL    573              /* 14 u/s (1147) */

#pragma bss-name (push, "LOWRAM")          /* port: $0C00-$17FF (not zeroed: w_reset clears) */
Body w_body[MAXBODIES];
#pragma bss-name (pop)
Body w_gridbody;
uint8_t w_nbody;
uint8_t grid[MAXH][MAXW][MAXW];          /* port: not static: gather.s reads them */
uint8_t gw, gd;

void w_reset(uint8_t w, uint8_t d)
{
    uint8_t i, *g = &grid[0][0][0];
    uint16_t k;
    for (i = 0; i < MAXBODIES; i++) w_body[i].used = 0;
    w_nbody = 0;
    for (k = 0; k < sizeof(grid); k++) g[k] = 0;
    gw = w;
    gd = d;
    w_gridbody.used = 1;
    w_gridbody.solid = 1;
    w_gridbody.owner = -1;
}

void w_set_block(uint8_t x, uint8_t y, uint8_t z) { grid[y][z][x] = 1; }

uint8_t w_block(int8_t x, int8_t y, int8_t z)
{
    if (x < 0 || x >= gw || z < 0 || z >= gd || y < 0 || y >= MAXH) return 0;
    return grid[y][z][x];
}

static Body *alloc_body(void)
{
    uint8_t i;
    for (i = 0; i < MAXBODIES; i++)
        if (!w_body[i].used) {
            Body *b = &w_body[i];
            b->px = b->py = b->pz = 0;
            b->sx = b->sy = b->sz = FX;
            b->vx = b->vy = b->vz = 0;
            b->used = 1;
            b->solid = 1;
            b->dynamic = b->pushable = b->player = b->npcbar = b->grounded = b->harm = 0;
            b->collides = 1;
            b->ground = 0;
            b->owner = -1;
            if (i >= w_nbody) w_nbody = i + 1;
            return b;
        }
    return &w_body[MAXBODIES - 1];        /* never in practice: rooms are small */
}

Body *w_add(fix px, fix py, fix pz, fix sx, fix sy, fix sz)
{
    Body *b = alloc_body();
    b->px = px; b->py = py; b->pz = pz;
    b->sx = sx; b->sy = sy; b->sz = sz;
    return b;
}

Body *w_add_static(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    /* port: x0 + w / 2, not (x0 + x1) / 2, which overflows 16 bits (sizes are even) */
    return w_add(x0 + ((x1 - x0) >> 1), y0, z0 + ((z1 - z0) >> 1), x1 - x0, y1 - y0, z1 - z0);
}

void w_remove(Body *b)
{
    uint8_t i;
    b->used = 0;
    for (i = 0; i < w_nbody; i++)
        if (w_body[i].ground == b) w_body[i].ground = 0;
}

/* port: inline in the loops below (a call per body was a tenth of the time) */
#define BODY_BOX(b, o) do { \
    (o)->x0 = (b)->px - ((b)->sx >> 1); (o)->x1 = (o)->x0 + (b)->sx; \
    (o)->y0 = (b)->py;                  (o)->y1 = (b)->py + (b)->sy; \
    (o)->z0 = (b)->pz - ((b)->sz >> 1); (o)->z1 = (o)->z0 + (b)->sz; \
} while (0)

void body_box(const Body *b, Box *o)
{
    o->x0 = b->px - (b->sx >> 1); o->x1 = o->x0 + b->sx;
    o->y0 = b->py;                o->y1 = b->py + b->sy;
    o->z0 = b->pz - (b->sz >> 1); o->z1 = o->z0 + b->sz;
}

uint8_t box_overlap(const Box *a, const Box *b, fix eps)
{
    return a->x0 < b->x1 - eps && a->x1 > b->x0 + eps &&
           a->y0 < b->y1 - eps && a->y1 > b->y0 + eps &&
           a->z0 < b->z1 - eps && a->z1 > b->z0 + eps;
}

/* box_shrink(b, 80): scale about the centre to 80% */
void box_shrink80(Box *b)
{
    /* port: 0.4 w by shifts (w/4 + w/8 + w/64 + w/128 = 0.398 w), not a divide */
    uint16_t w;
    fix cx, cy, cz, hx, hy, hz;
    w = (uint16_t)(b->x1 - b->x0); hx = (fix)((w >> 2) + (w >> 3) + (w >> 6) + (w >> 7)); cx = b->x0 + (fix)(w >> 1);
    w = (uint16_t)(b->y1 - b->y0); hy = (fix)((w >> 2) + (w >> 3) + (w >> 6) + (w >> 7)); cy = b->y0 + (fix)(w >> 1);
    w = (uint16_t)(b->z1 - b->z0); hz = (fix)((w >> 2) + (w >> 3) + (w >> 6) + (w >> 7)); cz = b->z0 + (fix)(w >> 1);
    b->x0 = cx - hx; b->x1 = cx + hx;
    b->y0 = cy - hy; b->y1 = cy + hy;
    b->z0 = cz - hz; b->z1 = cz + hz;
}

void box_grow(Box *b, fix g)
{
    b->x0 -= g; b->y0 -= g; b->z0 -= g;
    b->x1 += g; b->y1 += g; b->z1 += g;
}

static int8_t ffloor(fix v) { return v >= 0 ? (int8_t)(v >> FXS) : -(int8_t)((-v + FX - 1) >> FXS); }

/* every solid thing whose box roughly meets `r`: bodies, then grid cells */
#define MAXCAND 48
#pragma bss-name (push, "LOWRAM")
Box   cand_box[MAXCAND];                     /* port: not static: gather.s fills them */
Body *cand_body[MAXCAND];
#pragma bss-name (pop)

#ifdef SCANCHECK
#include "a8log.h"
#endif
/* port: gather is in asm (gather.s); this is the C it follows (make
   GATHERCHECK=1 runs both and logs any difference) */
#ifdef GATHERCHECK
#include "a8log.h"
extern uint8_t __fastcall__ gather_asm(const Box *r, const Body *self, uint8_t skip_npcbar);
static uint8_t gather_c(const Box *r, const Body *self, uint8_t skip_npcbar);
static Box chk_box[MAXCAND];
static Body *chk_body[MAXCAND];
static uint8_t gather(const Box *r, const Body *self, uint8_t skip_npcbar)
{
    uint8_t n = gather_c(r, self, skip_npcbar), i, m;
    for (i = 0; i < n; i++) { chk_box[i] = cand_box[i]; chk_body[i] = cand_body[i]; }
    m = gather_asm(r, self, skip_npcbar);
    if (m != n) {
        a8_log("GATHERCHECK n %u %u r %d %d %d %d %d %d\n", n, m, r->x0, r->y0, r->z0, r->x1, r->y1, r->z1);
        if (m) a8_log("GATHERCHECK asm %d %d %d %d %d %d body %u\n", cand_box[0].x0, cand_box[0].y0, cand_box[0].z0,
                      cand_box[0].x1, cand_box[0].y1, cand_box[0].z1, cand_body[0] == &w_gridbody ? 99 : (unsigned)(cand_body[0] - w_body));
        a8_log("GATHERCHECK grid %u ff %d %d %d %d %d %d gw %u gd %u\n", grid[2][6][3], ffloor(r->x0), ffloor(r->x1),
               ffloor(r->y0), ffloor(r->y1), ffloor(r->z0), ffloor(r->z1), gw, gd);
    }
    else
        for (i = 0; i < n; i++)
            if (chk_body[i] != cand_body[i] || chk_box[i].x0 != cand_box[i].x0 || chk_box[i].x1 != cand_box[i].x1 ||
                chk_box[i].y0 != cand_box[i].y0 || chk_box[i].y1 != cand_box[i].y1 ||
                chk_box[i].z0 != cand_box[i].z0 || chk_box[i].z1 != cand_box[i].z1) {
                a8_log("GATHERCHECK cand %u\n", i);
                break;
            }
    return m;
}
#else
#define gather_c gather_unused
#define gather gather_asm
extern uint8_t __fastcall__ gather_asm(const Box *r, const Body *self, uint8_t skip_npcbar);
#endif

static uint8_t gather_c(const Box *r, const Body *self, uint8_t skip_npcbar)
{
    uint8_t i, n = 0;
    int8_t x, y, z, x0, x1, y0, y1, z0, z1;
    fix rx0 = r->x0, rx1 = r->x1, rz0 = r->z0, rz1 = r->z1;
    Body *o = w_body;
    for (i = 0; i < w_nbody && n < MAXCAND; i++, o++) {
        Box *c;
        fix bx0, bz0;
        if (!o->used || o == self || !o->solid || (o->npcbar && skip_npcbar)) continue;
        /* monsters are deadly to touch, so they must be able to touch you
         * (in the Unity game they block each other and only ghosts kill) */
        if (self && ((self->player && o->harm) || (self->harm && o->player))) continue;
        /* port: x and z first, cheaply: most bodies are elsewhere */
        bx0 = o->px - (o->sx >> 1);
        if (bx0 > rx1 || bx0 + o->sx < rx0) continue;
        bz0 = o->pz - (o->sz >> 1);
        if (bz0 > rz1 || bz0 + o->sz < rz0) continue;
        c = &cand_box[n];
        BODY_BOX(o, c);
        if (c->y0 > r->y1 || c->y1 < r->y0) continue;
        cand_body[n++] = o;
    }
    x0 = ffloor(r->x0); x1 = ffloor(r->x1);
    y0 = ffloor(r->y0); y1 = ffloor(r->y1);
    z0 = ffloor(r->z0); z1 = ffloor(r->z1);
    if (x0 < 0) x0 = 0;
    if (x1 > gw - 1) x1 = gw - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > MAXH - 1) y1 = MAXH - 1;
    if (z0 < 0) z0 = 0;
    if (z1 > gd - 1) z1 = gd - 1;
    for (y = y0; y <= y1; y++)
        for (z = z0; z <= z1; z++)
            for (x = x0; x <= x1; x++)
                if (grid[y][z][x] && n < MAXCAND) {
                    Box *c = &cand_box[n];
                    c->x0 = (fix)x << FXS; c->x1 = c->x0 + FX;
                    c->y0 = (fix)y << FXS; c->y1 = c->y0 + FX;
                    c->z0 = (fix)z << FXS; c->z1 = c->z0 + FX;
                    cand_body[n++] = &w_gridbody;
                }
    return n;
}

static fix *pos(Body *b, uint8_t axis) { return axis == 0 ? &b->px : axis == 1 ? &b->py : &b->pz; }
static fix lo(const Box *b, uint8_t axis) { return axis == 0 ? b->x0 : axis == 1 ? b->y0 : b->z0; }
static fix hi(const Box *b, uint8_t axis) { return axis == 0 ? b->x1 : axis == 1 ? b->y1 : b->z1; }

static void extend(Box *b, uint8_t axis, fix d)
{
    if (axis == 0) { if (d > 0) b->x1 += d; else b->x0 += d; }
    else if (axis == 1) { if (d > 0) b->y1 += d; else b->y0 += d; }
    else { if (d > 0) b->z1 += d; else b->z0 += d; }
}

/* port: a Box is six fix in a row (x0 y0 z0 x1 y1 z1) and a Body's position
   three (px py pz): the axis indexes them (no pos / lo / hi / extend calls) */
#define BLO(bx, ax) (((const fix *)(bx))[ax])
#define BHI(bx, ax) (((const fix *)(bx))[(ax) + 3])
#define OVL(a, b, e) ((a)->x0 < (b)->x1 - (e) && (a)->x1 > (b)->x0 + (e) && (a)->z0 < (b)->z1 - (e) && \
                      (a)->z1 > (b)->z0 + (e) && (a)->y0 < (b)->y1 - (e) && (a)->y1 > (b)->y0 + (e))

extern Box cs_start, cs_swept;
extern uint8_t cs_axis;
extern fix cs_d;
extern Body *cs_hit;
extern fix __fastcall__ cand_scan(uint8_t n);

static fix move_axis(Body *b, uint8_t axis, fix d, uint8_t canpush, uint8_t depth)
{
    Box start, swept, ob;
    fix allowed, limit, slo, shi;
    Body *hit = 0;
    uint8_t i, n;
    Box *c;
    if (d == 0) return 0;
    if (!b->collides) { (&b->px)[axis] += d; return d; }
    BODY_BOX(b, &start);
    swept = start;
    if (d > 0) ((fix *)&swept)[axis + 3] += d; else ((fix *)&swept)[axis] += d;
    slo = BLO(&start, axis);
    shi = BHI(&start, axis);
    /* shove anything pushable in the way first */
    if (canpush && axis != 1 && depth < 3) {
        Body *o = w_body;
        for (i = 0; i < w_nbody; i++, o++) {     /* port: a pointer, not w_body[i] (a multiply by 30) */
            fix gap;
            if (!o->used || o == b || !o->pushable || !o->solid) continue;
            BODY_BOX(o, &ob);
            if (!OVL(&swept, &ob, EPS) || OVL(&start, &ob, EPS)) continue;
            if (ob.y0 >= start.y1 - 102 || ob.y1 <= start.y0 + 102) continue;   /* above or below us (205) */
            gap = d > 0 ? BLO(&ob, axis) - shi : BHI(&ob, axis) - slo;
            move_axis(o, axis, d - gap, 1, depth + 1);
        }
    }
    n = gather(&swept, b, b->player);
#if !defined(GATHERCHECK) && !defined(SCANCHECK)
    /* port: the scan in asm (gather.s: cand_scan); make SCANCHECK=1 checks it against the C */
    cs_start = start;
    cs_swept = swept;
    cs_axis = axis;
    cs_d = d;
    allowed = cand_scan(n);
    hit = cs_hit;
#else
    allowed = d;
    for (i = 0, c = cand_box; i < n; i++, c++) {
        if (!OVL(&swept, c, EPS)) continue;
        if (OVL(&start, c, EPS)) continue;   /* already inside (spawned overlapping): let it escape */
        if (d > 0) {
            limit = BLO(c, axis) - shi;
            if (limit < allowed) { allowed = limit; hit = cand_body[i]; }
        } else {
            limit = BHI(c, axis) - slo;
            if (limit > allowed) { allowed = limit; hit = cand_body[i]; }
        }
    }
    {
        fix a2;
        cs_start = start; cs_swept = swept; cs_axis = axis; cs_d = d;
        a2 = cand_scan(n);
        if (a2 != allowed || cs_hit != hit) a8_log("SCANCHECK scan %d %d\n", allowed, a2);
    }
#endif
    if (d > 0) { if (allowed < 0) allowed = 0; }
    else if (allowed > 0) allowed = 0;
    (&b->px)[axis] += allowed;
    if (axis == 1 && hit && d < 0) b->ground = hit;
    return allowed;
}

static void probe_ground(Body *b)
{
    Box probe;
    uint8_t i, n;
    if (!b->collides) return;
    body_box(b, &probe);
    probe.y1 = probe.y0;
    probe.y0 -= 41;                       /* 0.02 (82) */
    n = gather(&probe, b, 1);
    for (i = 0; i < n; i++)
        if (box_overlap(&probe, &cand_box[i], EPS_GROUND)) {
            b->grounded = 1;
            b->ground = cand_body[i];
            return;
        }
}

void w_move(Body *b, fix dx, fix dy, fix dz, uint8_t canpush, fix *moved)
{
    fix my;
    moved[0] = move_axis(b, 0, dx, canpush, 0);
    moved[2] = move_axis(b, 2, dz, canpush, 0);
    b->grounded = 0;
    b->ground = 0;
    my = move_axis(b, 1, dy, 0, 0);
    moved[1] = my;
    if (dy <= 0 && my > dy) b->grounded = 1;
    if (dy > 0 && my < dy && b->vy > 0) b->vy = 0;             /* bonked our head */
    if (!b->grounded && dy <= 0) probe_ground(b);
}

void w_step(Body *b, uint8_t canpush, fix *moved)
{
    b->vy -= GRAVITY;
    if (b->vy < -MAXFALL) b->vy = -MAXFALL;
    w_move(b, b->vx, b->vy, b->vz, canpush, moved);
    if (b->grounded && b->vy < 0) b->vy = 0;
}

void w_move_kinematic(Body *lift, fix dy)
{
    Body *riders[MAXBODIES];
    Box top, ob;
    fix moved[3];
    uint8_t i, n = 0;
    body_box(lift, &top);
    top.y0 = top.y1;
    top.y1 += 102;                        /* 0.05 (205) */
    for (i = 0; i < w_nbody; i++) {
        Body *o = &w_body[i];
        if (!o->used || o == lift || !o->dynamic || !o->collides) continue;
        BODY_BOX(o, &ob);
        if (o->ground == lift || box_overlap(&top, &ob, EPS_GROUND)) riders[n++] = o;
    }
    if (dy > 0)
        for (i = 0; i < n; i++) w_move(riders[i], 0, dy, 0, 0, moved);
    lift->py += dy;
    if (dy <= 0)
        for (i = 0; i < n; i++) {
            w_move(riders[i], 0, dy, 0, 0, moved);
            riders[i]->grounded = 1;
            riders[i]->ground = lift;
        }
}

uint8_t w_overlapping_solid(const Box *box, const Body *except)
{
    uint8_t i, n = gather(box, except, 0);
    for (i = 0; i < n; i++)
        if (box_overlap(box, &cand_box[i], EPS)) return 1;
    return 0;
}

fix w_ground_below(fix x, fix y, fix z)
{
    fix best = 0;
    int8_t cx = ffloor(x), cz = ffloor(z), cy;
    uint8_t i;
    for (cy = 0; cy < MAXH; cy++)
        if (w_block(cx, cy, cz) && ((fix)(cy + 1) << FXS) <= y + 205 && ((fix)(cy + 1) << FXS) > best)
            best = (fix)(cy + 1) << FXS;
    for (i = 0; i < w_nbody; i++) {
        Body *o = &w_body[i];
        fix bx0, bz0;
        if (!o->used || !o->solid || o->npcbar || (o->owner >= 0 && !o->pushable)) continue;
        bx0 = o->px - (o->sx >> 1);                  /* port: inline, x and z first */
        if (x <= bx0 || x >= bx0 + o->sx) continue;
        bz0 = o->pz - (o->sz >> 1);
        if (z <= bz0 || z >= bz0 + o->sz) continue;
        if (o->py + o->sy <= y + 205 && o->py + o->sy > best) best = o->py + o->sy;   /* 0.1 (410) */
    }
    return best;
}
