/* Rolling Steel (Atari 130XE) - the game around the marble (the original's
   game.c, GameDirector.cs): one clock that carries over between the
   courses, falls that cost three seconds and put you back behind where you
   last had the deck, medals, best times, the title's flyover of the course,
   the attract mode's autopilot, high scores. Steps are 1/50 s, as the
   original's; the main loop runs as many as frames went by. */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a8hw.h"
#include "rs.h"

#pragma static-locals (on)

extern uint8_t spr_x[5], spr_y[5], spr_h[5], spr_lo[5], spr_hi[5];
void spr_init(void);
void spr_commit(void);

#define SECS(s)       ((uint16_t)((s) * 50))
#define PENALTY       SECS(3)
#define DYING_HOLD    68                /* 1.35 s */
#define CLEAR_HOLD    170               /* 3.4 s */
#define SCREEN_Y0     48                /* the course's first scan line (disp.s) */
#define SCREEN_LINES  177
#define DEMO_AFTER    SECS(25)
#define KEY_B         21

enum { ST_TITLE, ST_PLAY, ST_CLEAR, ST_OVER, ST_WON, ST_ENTRY };
enum { MEDAL_NONE, MEDAL_BRONZE, MEDAL_SILVER, MEDAL_GOLD };
static const char *const medal_name[4] = { "", "BRONZE", "SILVER", "GOLD" };
static const uint16_t medal_bonus[4] = { 0, 100, 300, 500 };

static uint8_t state, level, start_level, sel, demo, autopilot, loaded = 0xFF;
static uint16_t st_t, idle;
static int16_t time_left;
static uint16_t course_time, last_time;
static uint8_t falls, dying, finished, last_medal, last_best;
static uint16_t dying_t;
static uint16_t best[NCOURSE];
static int32_t score;
static uint8_t demo_wp, warn_t;
static int16_t cam_x, cam_y;            /* the camera's centre in the picture, 1/16 pixel and line */
static uint16_t cine_t;                 /* the title's flyover: path index, Q8 */
static const char *banner;
static uint8_t flash_t, flash_col;      /* a wipeout's flash: the HUD's bar, the original's colours */
static uint8_t banner_t;
uint16_t steps, frames, loops;          /* for the test: steps run, frames gone by, frames drawn */
uint8_t probe[8];

/* ------------------------------------------------------------ high scores */
typedef struct { char name[4]; int32_t score; } Hs;
static Hs hs[5] = { { "RSX", 12000L }, { "ICE", 8000L }, { "MRB", 5000L }, { "ACD", 3000L }, { "STL", 1000L } };
static uint8_t ed_place, ed_pos;
static char ed_name[4];

/* ------------------------------------------------------------ the HUD */
static void hud_clear(void) { memset(hud, 0, 80); }
static void hud_str(uint8_t x, const char *s)
{
    uint8_t c;
    while ((c = (uint8_t)*s++) != 0 && x < 80) hud[x++] = (uint8_t)(c < 0x20 ? c + 0x40 : c < 0x60 ? c - 0x20 : c);
}
extern uint8_t decbuf[5];
void __fastcall__ dec5(uint16_t v);
static uint8_t hud_num(uint8_t x, uint16_t v, uint8_t w)  /* right-aligned in w, returns the end */
{
    uint8_t i = 0;
    dec5(v);
    while (i < 4 && !decbuf[i]) ++i;
    if (5 - i < w) i = 5 - w;
    while (i < 5) hud[x++] = decbuf[i++];
    return x;
}
uint8_t __fastcall__ hud_time(uint8_t x, uint16_t t);    /* maths.s: steps -> SSS.T at hud[x] */
static void hud_center(uint8_t row, const char *s)
{
    uint8_t n = (uint8_t)strlen(s);
    memset(hud + row * 40, 0, 40);
    hud_str((uint8_t)(row * 40 + (40 - n) / 2), s);
}

/* ------------------------------------------------------------ sound */
static uint8_t hud_dirty = 1;           /* the HUD's text to lay out again */
static uint8_t sfx_ch = 1;
static void sfx(uint8_t freq, uint8_t dist, uint8_t vol, uint8_t decay, int8_t sweep)
{
    snd_sfx(sfx_ch, freq, dist, vol, decay, sweep);
    if (++sfx_ch > 3) sfx_ch = 1;
}
#define SFX_START()   sfx(80, SND_PURE, 10, 1, -3)
#define SFX_GOAL()    do { sfx(60, SND_PURE, 12, 1, -1); sfx(45, SND_PURE, 10, 1, -1); } while (0)
#define SFX_FALL()    sfx(30, SND_PURE, 10, 0, 4)
#define SFX_SHATTER() sfx(20, SND_NOISE, 14, 1, 0)
#define SFX_SIZZLE()  sfx(4, SND_NOISE, 12, 1, 0)
#define SFX_CHOMP()   sfx(120, SND_BUZZ, 13, 2, 6)
#define SFX_WARN()    sfx(40, SND_PURE, 8, 2, 0)
#define SFX_CLACK()   sfx(10, SND_PURE, 9, 3, 0)
#define SFX_THUD()    sfx(200, SND_BUZZ, 8, 2, 0)

static void rumble(void)
{
    uint16_t s;
    if (state != ST_PLAY || dying || finished || !b_gr[0]) { snd_off(0); return; }
    s = speed_of(b_vx[0], b_vz[0]) >> 7;    /* 0..8 */
    if (s < 1) { snd_off(0); return; }
    snd_tone(0, (uint8_t)(160 - (s << 3)), b_cls[0] == 2 ? SND_PURE : SND_NOISE, (uint8_t)(s > 6 ? 6 : s));
}

/* ------------------------------------------------------------ the route */
static uint16_t dist_to(const PathPt *p, uint16_t x, int16_t y, uint16_t z)
{
    int16_t dx = (int16_t)(p->x - x), dz = (int16_t)(p->z - z), dy = p->y - y;
    if (dx > 16000 || dx < -16000 || dz > 16000 || dz < -16000) return 0xFFFF;
    if (dy < 0) dy = -dy;
    return speed_of(dx, dz) + (dy >> 1);
}

static uint8_t nearest_pt(uint16_t x, int16_t y, uint16_t z, uint8_t safe_only)
{
    uint8_t i, best = 0xFF;
    uint16_t d, bd = 0xFFFF;
    for (i = 0; i < MH->npath; ++i) {
        if (safe_only && !(path[i].y & 1)) continue;
        d = dist_to(&path[i], x, y, z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

#ifndef A8_NO_LOG
static uint8_t progress(void)            /* 0..100 along the route */
{
    uint8_t i = nearest_pt(b_x[0], b_y[0], b_z[0], 0);
    return (uint8_t)((uint16_t)i * 100 / (MH->npath - 1));
}
#endif

/* the original's DemoInput: at the next waypoint at a cruise of 8.5 u/s,
   with the stick (digital: the error's sign) */
uint8_t __fastcall__ acid_at(uint16_t x, int16_t y, uint16_t z);   /* maths.s: in an acid pool? */

static void demo_input(int8_t *jx, int8_t *jz)
{
    int16_t dx, dz, ex, ez;
    uint8_t acidn;
    uint16_t l;
    /* port: with acid within a second of it, the autopilot creeps (2.5 u/s)
       and keeps to the route (a waypoint is reached at a unit, not two):
       on the Atari's 2.5D deck it rolled into course 6's last pools */
    acidn = acid_at(b_x[0] + b_vx[0] * 3, b_y[0], b_z[0] + b_vz[0] * 3) ||
           acid_at(b_x[0] + b_vx[0] + (b_vx[0] >> 1), b_y[0], b_z[0] + b_vz[0] + (b_vz[0] >> 1));
    while (demo_wp < MH->npath - 1) {
        dx = (int16_t)(path[demo_wp].x - b_x[0]);
        dz = (int16_t)(path[demo_wp].z - b_z[0]);
        if (speed_of(dx, dz) < (acidn ? 256 : 512)) ++demo_wp; else break;
    }
    dx = (int16_t)(path[demo_wp].x - b_x[0]);
    dz = (int16_t)(path[demo_wp].z - b_z[0]);
    l = speed_of(dx, dz);
    *jx = *jz = 0;
    if (l < 8) return;
    while (l > 255) { dx >>= 1; dz >>= 1; l >>= 1; }
    {   /* the way, Q7, at 8.5 u/s (near acid 2.5) */
        int16_t k = acidn ? 26 : 87;
        ex = ((((dx << 7) / (int16_t)l) * k) >> 4) - b_vx[0];
        ez = ((((dz << 7) / (int16_t)l) * k) >> 4) - b_vz[0];
    }
    if (ex > 40) *jx = 1; else if (ex < -40) *jx = -1;
    if (ez > 40) *jz = 1; else if (ez < -40) *jz = -1;
}

/* ------------------------------------------------------------ the camera */
static int16_t pic_x(uint16_t gx) { return (int16_t)((uint16_t)(gx + MH->px0) >> 6); }

/* the marble in the picture (worked out once a frame, for the camera and
   the sprites): pixel, line */
static int16_t m_px, m_line;
static void marble_pic(void)
{
    m_px = pic_x(b_x[0]);
    m_line = pic_line(b_y[0], b_z[0]);
}

static void cam_snap(void)
{
    marble_pic();
    cam_x = m_px << 4;
    cam_y = m_line << 4;
}

/* follow the marble with the original's lag (1 - 500/4096 a step, two
   steps a frame here) and its lead (0.35 s of the flat velocity) */
static void follow(void)
{
    int16_t tx, ty;
    marble_pic();
    if (state == ST_TITLE) {
        const PathPt *p = &path[(uint8_t)(cine_t >> 8)];
        tx = pic_x(p->x) << 4;
        ty = pic_line(p->y, p->z) << 4;
        cam_x += (tx - cam_x) >> 4;
        cam_y += (ty - cam_y) >> 4;
        return;
    }
    if (state != ST_PLAY || dying) return;
    tx = (m_px << 4) + (b_vx[0] >> 2);
    ty = (m_line << 4) - (b_vz[0] >> 2);
    cam_x += (tx - cam_x) >> 2;
    cam_y += (ty - cam_y) >> 2;
}

static void view_set(void)
{
    int16_t vx = (cam_x >> 4) - 80, vy = (cam_y >> 4) - 88;
    int16_t mx = (int16_t)(c_w - 41) * 4, my = (int16_t)(c_rows - 24) * 8;
    if (vx > mx) vx = mx;
    if (vx < 12) vx = 12;
    if (vy > my) vy = my;
    if (vy < 0) vy = 0;
    __asm__("sei");
    view_x = (uint16_t)vx;
    view_y = (uint16_t)vy;
    __asm__("cli");
}

/* ------------------------------------------------------------ sprites */
static const uint8_t spr_marble[8] = { 0x60, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0x60 };
static const uint8_t spr_shine[8]  = { 0x00, 0x40, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t spr_steel[11] = { 0x30, 0x78, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0x78, 0x30 };
static const uint8_t spr_blob[8]   = { 0x30, 0x78, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0x78 };
static const uint8_t spr_shadow[3] = { 0x06, 0x0F, 0x06 };

/* sprite n's shape (h lines, w clocks) centred on picture pixel px, line ln */
static uint8_t put_h, put_w;
static void put(uint8_t n, int16_t px, int16_t ln, const uint8_t *shape)
{
    int16_t x = px - (int16_t)view_x + 48 - (put_w >> 1);
    int16_t y = ln - (int16_t)view_y + SCREEN_Y0 - (put_h >> 1);
    if (x < 40 || x > 210 || y < SCREEN_Y0 || y > SCREEN_Y0 + SCREEN_LINES - put_h) return;
    spr_x[n] = (uint8_t)x;
    spr_y[n] = (uint8_t)y;
    spr_h[n] = put_h;
    spr_lo[n] = (uint8_t)(uint16_t)shape;
    spr_hi[n] = (uint8_t)((uint16_t)shape >> 8);
}

static void sprites(void)                /* positions against the view the next frame shows */
{
    uint8_t i, n = 2;
    spr_h[0] = spr_h[1] = spr_h[2] = spr_h[3] = spr_h[4] = 0;
    if (state != ST_TITLE && b_live[0]) {
        put_h = 8;
        put_w = 4;
        put(0, m_px, m_line, spr_shine);
        put(1, m_px, m_line, spr_marble);
        /* the shadow: on the deck under it (a line per 8/210 unit of drop) */
        if (b_gr[0] || (ground(b_x[0], b_z[0]) && g_h < b_y[0])) {
            int16_t drop = b_gr[0] ? 128 : b_y[0] - g_h;
            put_h = 3;
            put(4, m_px, m_line + (int16_t)(((uint16_t)drop * 105) >> 12), spr_shadow);
        }
    }
    for (i = 1; i <= nen && n < 4; ++i) {
        int16_t px = pic_x(b_x[i]) - (int16_t)view_x;
        if (px < -8 || px > 168) continue;          /* off the screen: no need of its line */
        put_w = 6;
        if (en_kind[i]) put_h = 8; else put_h = 11;
        put(n, pic_x(b_x[i]), pic_line(b_y[i], b_z[i]), en_kind[i] ? spr_blob : spr_steel);
        if (spr_h[n]) ((uint8_t *)0x2C0)[n++] = en_kind[i] ? 0xC8 : 0x04;   /* blobs green, steel dark */
    }
}

/* ------------------------------------------------------------ the states */
static void load(uint8_t ci)
{
    if (loaded == ci) return;
    SDMCTL = 0;
    if (!course_load(ci)) {
        hud_clear();
        hud_str(0, "ROLLING STEEL NEEDS A 130XE");
        SDMCTL = 0x22;
        a8_log("rs: no banks\n");
        a8_fail("needs a 130XE");
        for (;;) ;
    }
    disp_course();
    phys_reset();
    loaded = ci;
    SDMCTL = 0x3E;                       /* the display, players and missiles, single line */
}

static void enter(uint8_t s)
{
    state = s;
    st_t = 0;
    hud_dirty = 1;
    a8_log("rs: state=%u level=%u falls=%u clock=%u score=%ld\n", s, level + 1, falls,
           time_left > 0 ? (uint16_t)time_left / 5 : 0, score);
}

static void start_course(uint8_t ci, uint8_t reset_clock)
{
    load(ci);
    level = ci;
    if (reset_clock) time_left = 0;
    time_left += MH->time;
    course_time = 0;
    dying = finished = 0;
    demo_wp = 0;
    phys_place(MH->sx, MH->sy, MH->sz);
    phys_reset();
    cam_snap();
    a8_log("rs: level %u %s clock=%u\n", ci + 1, course_src[ci].name, (uint16_t)time_left / 5);
}

static void to_title(void)
{
    if (demo) sel = (uint8_t)((level + 1) % NCOURSE);
    demo = autopilot = 0;
    idle = 0;
    snd_off(0);
    load(sel);
    cine_t = 0;
    cam_x = pic_x(path[0].x) << 4;
    cam_y = pic_line(path[0].y, path[0].z) << 4;
    enter(ST_TITLE);
}

static void start_run(uint8_t from, uint8_t is_demo)
{
    demo = is_demo;
    autopilot = is_demo;
    falls = 0;
    score = 0;
    start_level = from;
    start_course(from, 1);
    enter(ST_PLAY);
    SFX_START();
}

static uint8_t medal_for(uint16_t t)
{
    if (MH->gold && t <= MH->gold) return MEDAL_GOLD;
    if (MH->silver && t <= MH->silver) return MEDAL_SILVER;
    if (MH->bronze && t <= MH->bronze) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

static void show_banner(const char *s, uint8_t t) { banner = s; banner_t = t; hud_dirty = 1; }

static void kill(const char *why)
{
    if (dying || finished) return;
    a8_log("rs: death %s course=%u\n", why, level + 1);
    ++falls;
    time_left -= PENALTY;
    if (time_left < 0) time_left = 0;
    b_live[0] = 0;
    dying = 1;
    dying_t = 0;
    show_banner(why, 68);
    flash_t = 14;
    flash_col = why[0] == 'D' ? 0xC6 : why[0] == 'E' ? 0xB6 : 0x34;   /* acid green, eaten, a fall: red */
    SFX_SHATTER();
}

static void respawn(void)
{
    static uint8_t last_i = 0xFF, again;
    uint8_t i = nearest_pt(lg_x, lg_y, lg_z, 1), k;
    dying = 0;
    if (i != 0xFF)
        for (k = i; k > 0; --k)
            if (path[k - 1].y & 1) { i = k - 1; break; }
    /* port: the autopilot can't get past everything (course 6's chaser
       guards the gap between two pools): put back at the same place (within
       two route points) a second time, it goes on from the third safe route point after */
    if (autopilot && i != 0xFF && last_i != 0xFF && i <= last_i + 2 && i + 2 >= last_i && ++again >= 2) {
        for (k = i + 1, again = 0; k < MH->npath && again < 3; ++k)
            if (path[k].y & 1) { i = k; ++again; }
        again = 0;
    } else if (i > last_i + 2 || i + 2 < last_i)
        again = 0;
    last_i = i;
    if (i == 0xFF) phys_place(MH->sx, MH->sy, MH->sz);
    else phys_place(path[i].x, path[i].y + 102, path[i].z);
    phys_enemies_home();
    a8_log("rs: respawn at %u %u %d (route %u)\n", b_x[0], b_z[0], b_y[0], i);
    show_banner("-3 SECONDS", 75);
    cam_snap();
    demo_wp = nearest_pt(b_x[0], b_y[0], b_z[0], 0);
}

static void game_over(uint8_t won)
{
    int8_t r;
    uint8_t i;
    score += time_left > 0 ? (uint16_t)time_left / 5 : 0;
    snd_off(0);
    enter(won ? ST_WON : ST_OVER);
    a8_log("rs: gameover score=%ld won=%u demo=%u\n", score, won, demo);
    r = -1;
    for (i = 0; i < 5; ++i) if (score > hs[i].score) { r = (int8_t)i; break; }
    if (!demo && r >= 0) {
        ed_place = (uint8_t)r;
        ed_pos = 0;
        memcpy(ed_name, "AAA", 4);
        state = ST_ENTRY;
    }
}

static void reach_goal(void)
{
    if (finished || dying) return;
    finished = 1;
    last_time = course_time;
    last_medal = medal_for(course_time);
    last_best = 0;
    if (!demo && (!best[level] || course_time < best[level])) { best[level] = course_time; last_best = 1; }
    score += 1000L * (level + 1) + medal_bonus[last_medal];
    b_vx[0] = b_vz[0] = b_vy[0] = 0;
    a8_log("rs: goal course=%u time=%u medal=%u best=%u score=%ld\n", level + 1, course_time / 5, last_medal,
           last_best, score);
    SFX_GOAL();
    snd_off(0);
    enter(ST_CLEAR);
}

static void play_step(int8_t jx, int8_t jz)
{
    uint8_t e;
    if (dying) {
        phys_step(0, 0);
        if ((dying_t += 2) >= DYING_HOLD) {
            if (time_left <= 0) { show_banner("OUT OF TIME", 200); game_over(0); return; }
            if (demo && falls >= 5) { to_title(); return; }     /* the autopilot's had enough */
            respawn();
        }
        return;
    }
    if (autopilot) { demo_input(&jx, &jz); probe[4] = (uint8_t)jx; probe[5] = (uint8_t)jz; probe[6] = demo_wp; }
    phys_step(jx, jz);
    time_left -= 2;
    course_time += 2;
    if (time_left <= SECS(10) && time_left > 0 && (warn_t += 2) >= 50) { warn_t = 0; SFX_WARN(); }
    if (time_left <= 0) {
        time_left = 0;
        b_live[0] = 0;
        show_banner("OUT OF TIME", 200);
        game_over(0);
        return;
    }
    e = ph_event;
    if (e & PE_CLACK) SFX_CLACK();
    if (e & PE_LAND) SFX_THUD();
    if (e & PE_DISSOLVED) { SFX_SIZZLE(); kill("DISSOLVED"); return; }
    if (e & PE_EATEN) { SFX_CHOMP(); kill("EATEN"); return; }
    if (e & PE_FELL) { SFX_FALL(); kill("FELL OFF"); return; }
    if (e & PE_GOAL) { reach_goal(); return; }
    if (course_time % 250 == 0)
        a8_log("rs: run course=%u t=%u progress=%u falls=%u\n", level + 1, course_time / 50, progress(), falls);
}

static void step(uint8_t held, uint8_t edge, uint8_t key)
{
    int8_t jx = 0, jz = 0;
    st_t += 2;
    if (banner_t) { banner_t = banner_t > 2 ? banner_t - 2 : 0; if (!banner_t) hud_dirty = 1; }
    if (demo && (edge || key != KEY_NONE)) { to_title(); return; }
    switch (state) {
    case ST_TITLE:
        if (edge || key != KEY_NONE) idle = 0; else idle += 2;
        if ((edge & IN_DOWN) && sel < NCOURSE - 1) { ++sel; to_title(); return; }
        if ((edge & IN_UP) && sel > 0) { --sel; to_title(); return; }
        if (edge & IN_SELECT) { sel = (uint8_t)((sel + 1) % NCOURSE); to_title(); return; }
        if (edge & (IN_FIRE | IN_START)) { start_run(sel, 0); return; }
        if (idle > DEMO_AFTER) { a8_log("rs: demo starts course=%u\n", sel + 1); start_run(sel, 1); return; }
        cine_t += (uint16_t)MH->npath * 59 / 128;          /* 0.045 of the course a second */
        if ((cine_t >> 8) >= MH->npath) cine_t = 0;
        phys_step(0, 0);
        break;
    case ST_PLAY:
        if (key == KEY_B && !demo) { autopilot ^= 1; demo_wp = nearest_pt(b_x[0], b_y[0], b_z[0], 0); a8_log("rs: autopilot=%u\n", autopilot); hud_dirty = 1; }
        if (held & IN_LEFT) jx = -1;
        if (held & IN_RIGHT) jx = 1;
        if (held & IN_UP) jz = 1;
        if (held & IN_DOWN) jz = -1;
        play_step(jx, jz);
        break;
    case ST_CLEAR:
        phys_step(0, 0);
        if (st_t >= CLEAR_HOLD) {
            if (demo) { to_title(); return; }
            if (level + 1 >= NCOURSE) { show_banner("ALL CLEAR", 250); game_over(1); return; }
            start_course(level + 1, 0);
            enter(ST_PLAY);
            SFX_START();
        }
        break;
    case ST_OVER:
    case ST_WON:
        phys_step(0, 0);
        if (st_t > SECS(1) && (edge & (IN_FIRE | IN_START))) { to_title(); return; }
        if (st_t > SECS(demo ? 4 : 20)) { to_title(); return; }
        break;
    }
}

static void entry_input(uint8_t edge, uint8_t key)
{
    char c = key != KEY_NONE ? key_ascii(key) : 0;
    if (c >= 'A' && c <= 'Z') { ed_name[ed_pos] = c; if (ed_pos < 2) ed_pos++; }
    if (edge & IN_UP) ed_name[ed_pos] = ed_name[ed_pos] == 'Z' ? 'A' : (char)(ed_name[ed_pos] + 1);
    if (edge & IN_DOWN) ed_name[ed_pos] = ed_name[ed_pos] == 'A' ? 'Z' : (char)(ed_name[ed_pos] - 1);
    if ((edge & IN_RIGHT) && ed_pos < 2) ed_pos++;
    if ((edge & IN_LEFT) && ed_pos > 0) ed_pos--;
    if ((edge & (IN_FIRE | IN_START)) || c == '\n') {
        uint8_t i;
        for (i = 4; i > ed_place; --i) hs[i] = hs[i - 1];
        memcpy(hs[ed_place].name, ed_name, 4);
        hs[ed_place].score = score;
        a8_log("rs: hiscore name=%s score=%ld place=%u\n", ed_name, score, ed_place);
        to_title();
    }
}

/* ------------------------------------------------------------ the HUD's text */
static uint8_t blink;
/* the numbers that change while you play: the clock (flashing in its
   last 10 seconds), your falls, the course's time */
static void hud_tick(void)
{
    uint8_t x;
    hud_time(31, time_left > 0 ? (uint16_t)time_left : 0);
    if (time_left <= SECS(10) && (blink & 8)) for (x = 31; x < 36; ++x) hud[x] |= 0x80;
    if (state == ST_PLAY && !banner_t && !demo) {
        hud_num(46, falls, 1);
        hud_time(55, course_time);
    }
}

static void draw_hud(void)
{
    uint8_t x;
    hud_clear();
    switch (state) {
    case ST_TITLE:
        hud_str(0, "ROLLING STEEL");
        hud_str(20, "HI");
        x = hud_num(23, (uint16_t)hs[0].score, 1);
        hud_str(x + 1, hs[0].name);
        hud_num(40, sel + 1, 1);
        hud_str(42, course_src[sel].name);
        if (best[sel]) { hud_str(56, "BEST"); hud_time(60, best[sel]); }
        else if (blink & 32) hud_str(56, "FIRE TO ROLL");
        else hud_str(56, "UP/DOWN COURSE");
        return;
    case ST_ENTRY:
        hud_str(0, "HIGH SCORE");
        hud_num(12, (uint16_t)score, 1);
        hud_str(40, "YOUR INITIALS:");
        hud_str(55, ed_name);
        if (blink & 16) hud[55 + ed_pos] |= 0x80;
        return;
    }
    hud_num(0, level + 1, 1);
    hud_str(2, course_src[level].name);
    hud_str(26, "TIME");
    hud_tick();
    if (state == ST_CLEAR) {
        hud_str(40, "COURSE CLEAR");
        hud_time(53, last_time);
        hud_str(60, medal_name[last_medal]);
        if (last_best) hud_str(67, "BEST!");
        return;
    }
    if (state == ST_OVER || state == ST_WON) {
        hud_center(1, state == ST_WON ? "ALL CLEAR" : "GAME OVER");
        hud_str(40, "SCORE");
        hud_num(46, (uint16_t)score, 1);
        return;
    }
    if (banner_t) { hud_center(1, banner); return; }
    if (demo) { hud_center(1, "DEMO - PRESS FIRE"); return; }
    hud_str(40, "FALLS");
    hud_str(49, "COURSE");
    if (best[level]) { hud_str(62, "BEST"); hud_time(66, best[level]); }
    if (autopilot) hud_str(76, "AUTO");
    hud_tick();
}

int main(void)
{
    uint8_t n, held, edge = 0, key = KEY_NONE, due = 0, s0, ran;
    os_off();
    CHBAS = 0x0C;
    COLOR1 = 0x0E;
    COLOR2 = 0x80;
    COLOR4 = 0x00;
    COLOR3 = 0x00;                      /* the shadow (the missiles as a fifth player) */
    PCOLR0 = 0x0E;                      /* the marble's shine */
    ((uint8_t *)0x2C0)[1] = 0x0A;                      /* the marble: chrome */
    ((uint8_t *)0x2C0)[2] = 0x08;                      /* chasers: steel */
    ((uint8_t *)0x2C0)[3] = 0x08;
    GPRIOR = 0x11;                      /* players over the course, the missiles one player */
    sound_init();
    disp_init();
    spr_init();
    to_title();
    frame_init();
    a8_log("rs: ready\n");
    for (;;) {
        n = frame_wait();
        frames += n;
        spr_commit();
        held = in_held;
        edge |= take_latch();           /* kept until a step takes them */
        if (key == KEY_NONE) key = key_get();
        ++blink;
        if (state == ST_ENTRY) { entry_input(edge, key); edge = 0; key = KEY_NONE; if ((blink & 7) == 0 || hud_dirty) { draw_hud(); hud_dirty = 0; } continue; }
        /* a step is 2/50 s: one every second frame, at most two at once */
        due += n;
        if (due > 4) due = 4;
        ran = 0;
        while (due >= 2) {
            due -= 2;
            ran = 1;
            s0 = state;
            step(held, edge, key);
            ++steps;
            edge = 0;
            key = KEY_NONE;
            if (state != s0) hud_dirty = 1;
            if (state == ST_ENTRY) break;
        }
        if (state == ST_ENTRY) continue;
        if (!ran && !hud_dirty) continue;              /* no step this frame: nothing moved */
        ++loops;
        if (flash_t) { --flash_t; COLOR2 = (flash_t & 2) ? flash_col : 0x80; }
        follow();
        view_set();
        sprites();
        rumble();
        if (state == ST_TITLE && (blink & 31) == 0) hud_dirty = 1;   /* its hint changes */
        if (hud_dirty) { draw_hud(); hud_dirty = 0; }
        else if ((blink & 3) == 0 && state != ST_TITLE) hud_tick();
        probe[0] = state;
        probe[1] = level;
        probe[2] = falls;
        probe[3] = b_gr[0];
    }
}
