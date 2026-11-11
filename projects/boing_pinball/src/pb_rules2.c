/*
 * pb_rules2.c - the original's display, lamps and bot (the end of pb_rules.c:
 * port: split off, they run in main memory with the front end while the
 * rules themselves run from bank 1).
 */
#include <string.h>
#include "pbz.h"

#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))

/* What the dot-matrix display shows now. */
void pb_dmd(Game *g, Dmd *d)
{
    static char big[24], small[40];
    Msg *m = NULL;
    int8_t i;
    while (g->nmsg > 1 && g->msg[0].until < g->time) { memmove(&g->msg[0], &g->msg[1], sizeof(Msg) * (g->nmsg - 1)); g->nmsg--; }
    for (i = 0; i < g->nmsg; i++) if (g->msg[i].until >= g->time) m = &g->msg[i];
    d->flash = 0;
    if (g->state == GS_ATTRACT) { d->big = g->t->name; d->small = "PRESS FIRE"; return; }
    if (m) { d->big = m->big; d->small = m->small; d->flash = g->time - m->at < PB_MS(600); return; }
    pb_fmt(big, g->score);
    d->big = big; d->small = small;
    {
        char *p = small;
        const char *s;
        if (g->mission >= 0) {
            int32_t left = (g->mission_until - g->time + PB_HZ - 1) / PB_HZ;
            char num[12];
            for (s = g->t->mission[g->mission]; *s && *s != ' '; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, g->mission_n); for (s = num; *s; s++) *p++ = *s;
            *p++ = '/';
            pb_fmt(num, pb_missions[g->mission].need); for (s = num; *s; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, left < 0 ? 0 : left); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'S'; *p = 0;
        } else if (g->multiball) {
            char num[12];
            for (s = "MULTIBALL  JACKPOT "; *s; s++) *p++ = *s;
            pb_fmt(num, 50 + 10 * g->jackpot); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'K'; *p = 0;
        } else {
            int8_t lane = 0;
            for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
            for (s = "BALL "; *s; s++) *p++ = *s;
            *p++ = (char)('0' + g->ball_no % 10);
            if (lane) { for (s = "  SKILL SHOT "; *s; s++) *p++ = *s; *p++ = (char)('A' + g->skill_lane); }
            else if (g->bonus_x > 1) { for (s = "  BONUS "; *s; s++) *p++ = *s; *p++ = (char)('0' + g->bonus_x); *p++ = 'X'; }
            *p = 0;
        }
    }
}

/* Which lamps are lit (the browser page's rules); blink toggles at 2.5 Hz,
 * chase counts up in the attract mode. */
void pb_lamps(const Game *g, int8_t blink, int8_t chase, uint8_t on[L_COUNT])
{
    int8_t i, lane = 0, skill;
    memset(on, 0, L_COUNT);
    if (g->state == GS_ATTRACT || g->state == GS_OVER) {
        int8_t k = (int8_t)(chase % 5);    /* port: (i + chase) % 5 == 0, counting (no modulo a lamp) */
        for (i = 0; i < L_COUNT; i++) {
            on[i] = (uint8_t)(k == 0);
            if (++k == 5) k = 0;
        }
        return;
    }
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
    skill = g->skill_armed && (g->time - g->plunge_at < PB_MS(6000) || lane) ? g->skill_lane : -1;
    for (i = 0; i < 3; i++) on[L_LANE0 + i] = (uint8_t)(g->lanes[i] || (skill == i && blink));
    for (i = 0; i < 3; i++) on[L_LOCK0 + i] = (uint8_t)(g->multiball ? blink : (i < g->locks || (i == g->locks && blink && g->state == GS_PLAY)));
    on[L_JACKPOT] = (uint8_t)(g->multiball && blink);
    for (i = 0; i < 5; i++) on[L_M0 + i] = (uint8_t)((g->missions_done & (1 << i)) || (g->mission == i && blink));
    on[L_MISSION] = (uint8_t)(g->mission_lit && g->mission < 0 && blink);
    for (i = 0; i < 4; i++) on[L_X2 + i] = (uint8_t)(g->bonus_x >= i + 2);
    on[L_SAVE] = (uint8_t)(g->time < g->save_until);
    for (i = 0; i < g->t->nspin && i < 2; i++) on[L_SPIN0 + i] = (uint8_t)(g->w.spin[i].w > 2 * 256);
    for (i = 0; i < g->t->ndrop && i < 8; i++) on[L_DROP0 + i] = g->t->seg[g->t->drop_seg[i]].up;
    on[L_SUPER] = (uint8_t)(g->supernova > g->time && blink);
}

/* The attract mode's player (and the tests'): launches, holds a flipper up
 * while a ball comes down towards it and lets go a moment after. */
void pb_bot(const Game *g, PbInput *in, Bot *bot)
{
    int8_t i, k;
    int16_t *hold = bot->hold;
    uint32_t *rng = &bot->rng;
    memset(in, 0, sizeof *in);
    in->pull_to = -1;
    for (k = 0; k < 2; k++) if (hold[k] > 0) { hold[k]--; if (k) in->right = 1; else in->left = 1; }
    for (i = 0; i < g->w.nballs; i++) {
        const Ball *b = &g->w.balls[i];
        int16_t avy;
        if (b->state == BS_LANE) {
            if (++bot->launch > PB_MS(800)) { in->pull_to = 800 + (int16_t)((uint16_t)a3_rand(rng) % 200); bot->launch = 0; }
            continue;
        }
        if (b->state != BS_PLAY || b->vy >= V_MM(-60)) continue;     /* port: the cheap test first */
        avy = (int16_t)(mulu16((uint16_t)ABS16(b->vy), 52429) >> 18);   /* port: |vy| / 5, no division (exact) */
        for (k = 0; k < g->w.nflip; k++) {
            const Flipper *f = &g->w.flip[k];
            int16_t tx, ty, dx, dy;
            pb_flipper_tip(f, &tx, &ty);
            dx = b->x - ((f->d.px + tx) >> 1);
            dy = b->y - ((f->d.py + ty) >> 1);
            /* coming down within reach: flip, with a little randomness in the timing
               (port: in P and V; |vy| / 20 mm is |vy|_V / 5 P) */
            if (ABS16(dx) < ((f->d.len * 3) >> 2) + PB_R && dy > 0 && dy < P_MM(40) + avy) {
                if ((uint16_t)a3_rand(rng) % 4) hold[f->d.side] = PB_MS(250);
            }
        }
    }
    for (k = 0; k < 2; k++) if (hold[k] > 0) { if (k) in->right = 1; else in->left = 1; }
}
