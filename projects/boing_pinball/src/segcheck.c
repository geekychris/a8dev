/* segcheck.c - (make SEGCHECK=1) seg_fast (physasm.s) checked against the
   C it follows: hit_segment's interior case. Runs in main memory, called
   through banks.s. */
#ifdef SEGCHECK
#include <string.h>
#include "pbz.h"
#include "a8log.h"
#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))

static void bounce(Ball *b, int16_t nx, int16_t ny, int16_t vn, uint8_t e, uint8_t fric)
{
    int16_t tx = b->vx - mulq14(vn, nx), ty = b->vy - mulq14(vn, ny);
    int8_t slow = -vn < V_MM(60);
    int16_t ee = slow ? 0 : e, k = slow ? 256 : 256 - fric;
    int16_t en = mulq8(ee, vn);
    b->vx = mulq8(tx, k) - mulq14(en, nx);
    b->vy = mulq8(ty, k) - mulq14(en, ny);
}

int16_t c_seg_ref(Ball *b, const Seg *s)
{
    int16_t dx = b->x - s->ax, dy = b->y - s->ay, t, qx, qy, rr = PB_R + s->r, nx, ny, vn;
    int32_t tt = (mul16(dx, s->ux) + mul16(dy, s->uy)) >> 14;
    int16_t dist = (int16_t)((mul16(dy, s->ux) - mul16(dx, s->uy)) >> 14);
    int16_t ad = ABS16(dist);
    if (ad >= rr) return -1;
    if (dist >= 0) { nx = -s->uy; ny = s->ux; } else { nx = s->uy; ny = -s->ux; }
    vn = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
    if ((s->ow_x || s->ow_y) && (nx * s->ow_x + ny * s->ow_y <= 0 || vn >= 0)) return -1;
    t = (int16_t)tt;
    qx = s->ax + mulq14(s->ux, t);
    qy = s->ay + mulq14(s->uy, t);
    b->x = qx + mulq14(nx, rr); b->y = qy + mulq14(ny, rr); b->xf = b->yf = 128;
    if (vn < 0) bounce(b, nx, ny, vn, s->e, 8);
    return vn < 0 ? -vn : 0;
}

static uint8_t told;
void c_seg_mismatch(const Ball *a, const Ball *c, int16_t ra, int16_t rc)
{
    if (told++ > 3) return;
    a8_log("segcheck: r %d/%d x %d/%d y %d/%d vx %d/%d vy %d/%d\n", ra, rc, a->x, c->x, a->y, c->y, a->vx, c->vx,
           a->vy, c->vy);
}
#endif
