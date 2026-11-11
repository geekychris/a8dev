/*
 * a8_3d - bench and demo for the a3d renderer.
 *
 * First it times each part of the renderer (a8_log "bench: ..." lines in
 * CPU cycles, measured with VCOUNT: 2 scan lines = 228 cycles of wall time,
 * DMA included), then runs a demo: flat-shaded cubes spinning in front of a
 * sky and a floor, double buffered, with the frame rate in the text row.
 * Joystick: left/right turn the camera, up/down move it, FIRE toggles
 * wireframe. The log reports fps every 100 frames.
 */
#include <string.h>
#include "a8game.h"
#include "a8log.h"
#include "a3d.h"

static const int8_t cube_v[8 * 3] = {
    -40, -40, -40,   40, -40, -40,   40, 40, -40,   -40, 40, -40,
    -40, -40,  40,   40, -40,  40,   40, 40,  40,   -40, 40,  40,
};
static const uint8_t cube_f[6][4] = {
    { 3, 2, 1, 0 }, { 6, 7, 4, 5 }, { 2, 6, 5, 1 },
    { 7, 3, 0, 4 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 },
};
static const uint8_t face_shade[6] = { 6, 2, 4, 3, 5, 1 };

static uint16_t t0;
#define CYC(t) ((unsigned long)(t) * A3D_TICK_CYCLES)

static void bench_start(void)
{
    frame_wait();
    t0 = a3d_ticks();
}

static void bench_end(const char *what, uint8_t n)
{
    uint16_t t = a3d_since(t0);
    a8_log("bench: %s cycles=%lu\n", what, CYC(t) / n);
}

/* ---------------------------------------------------------------- checks */
extern int16_t a3d_vcx, a3d_vcy;
static uint8_t *back_line(uint8_t y)
{
    extern uint8_t a3d_ylo[96], a3d_yhi[96];
    return (uint8_t *)(((uint16_t)(a3d_yhi[y] | (a3d_back ? 0x10 : 0)) << 8) | a3d_ylo[y]);
}

static void check(const char *what, uint8_t ok)
{
    a8_log("check: %s ok=%u\n", what, ok);
}

/* a3d_prism's fields for an 80-unit cube at depth z turned by (c, s) */
static void pr_cube(int16_t z, int8_t c, int8_t s)
{
    uint8_t k;
    a3d_pr_x = 0; a3d_pr_z = z; a3d_pr_c = c; a3d_pr_s = s;
    a3d_pr_hw = 40; a3d_pr_z0 = -40; a3d_pr_z1 = 40;
    a3d_pr_y0 = -40; a3d_pr_y1 = 40;
    a3d_pr_vis = 0; a3d_pr_far = 0;
    for (k = 0; k < 5; ++k) a3d_pr_shade[k] = 0;
}

static void checks(void)
{
    int16_t a, b, p;
    uint8_t ok, y, *l;
    static const uint8_t idx[4] = { 0, 1, 2, 3 };
    static const int8_t v1[3] = { 40, -40, 0 };

    ok = 1;
    for (a = -128; a < 128; a += 5)
        for (b = -128; b < 128; b += 3)
            if (a3d_mul8s((int8_t)a, (int8_t)b) != a * b) ok = 0;
    if (a3d_mul8s(-128, -128) != 16384 || a3d_mul8s(127, -128) != -16256) ok = 0;
    check("mul8s", ok);

    ok = a3d_mulf(1000, 512) == 2000 && a3d_mulf(-300, 256) == -300 &&
         a3d_mulf(-1000, -768) == 3000 && a3d_mulf(20000, 4096) == 16383;
    check("mulf", ok);

    ok = a3d_muldiv(1000, 30, 7) == 4285 && a3d_muldiv(-1000, 30, 7) == -4285 &&
         a3d_muldiv(1000, -30, -7) == 4285 && a3d_muldiv(30000, 30000, 30000) == 30000 &&
         a3d_muldiv(30000, 30000, 2) == 32767 && a3d_muldiv(-30000, 30000, 3) == -32767 &&
         a3d_muldiv(12345, 1, 1) == 12345 && a3d_muldiv(255, 255, 255) == 255;
    check("muldiv", ok);

    /* a rectangle x 5..12, y 2..5 in colour 3 */
    a3d_clear(0);
    a3d_colour(3);
    a3d_px[0] = 5;  a3d_py[0] = 2;
    a3d_px[1] = 13; a3d_py[1] = 2;
    a3d_px[2] = 13; a3d_py[2] = 6;
    a3d_px[3] = 5;  a3d_py[3] = 6;
    a3d_poly(4);
    ok = 1;
    for (y = 0; y < 8; ++y) {
        l = back_line(y);
        if (y >= 2 && y < 6) {
            if (l[0] != 0 || l[1] != 0x3F || l[2] != 0xFF || l[3] != 0xC0 || l[4] != 0) ok = 0;
        } else if (l[1] || l[2] || l[3]) ok = 0;
    }
    check("fill_rect", ok);

    /* the same anticlockwise draws nothing */
    a3d_clear(0);
    a3d_px[0] = 5;  a3d_py[0] = 2;
    a3d_px[1] = 5;  a3d_py[1] = 6;
    a3d_px[2] = 13; a3d_py[2] = 6;
    a3d_px[3] = 13; a3d_py[3] = 2;
    a3d_poly(4);
    ok = 1;
    for (y = 0; y < 8; ++y) {
        l = back_line(y);
        if (l[1] || l[2] || l[3]) ok = 0;
    }
    check("backface", ok);

    /* clipped: a triangle far outside on the left and top still fills its corner */
    a3d_clear(0);
    a3d_px[0] = -500; a3d_py[0] = -500;
    a3d_px[1] = 40;   a3d_py[1] = 10;
    a3d_px[2] = -500; a3d_py[2] = 300;
    a3d_poly(3);
    l = back_line(10);
    a8_log("check: clipdump %x %x %x %x\n", l[0], l[9], l[11], back_line(0)[0]);
    check("clip", l[0] == 0xFF && l[11] == 0 && back_line(0)[0] == 0xFF);

    /* an edge from far above and left (skipped to line 0) into the screen:
       the walker took its fast path on the wrong corner and crashed */
    a3d_clear(0);
    a3d_px[0] = 40;     a3d_py[0] = 39;
    a3d_px[1] = -13711; a3d_py[1] = 436;
    a3d_px[2] = -13711; a3d_py[2] = -374;
    a3d_px[3] = 40;     a3d_py[3] = 37;
    a3d_poly(4);
    /* (it reaches the screen only near its right corners: x 0..6 on line 36) */
    ok = back_line(36)[0] == 0xFF && !back_line(20)[0] && !back_line(0)[0];
    for (y = 0; y < 40; ++y)
        if (back_line(y)[11]) ok = 0;
    check("clip_skip", ok);

    /* dirty rectangle: clearing it leaves nothing */
    a3d_clear_dirty(0);
    ok = 1;
    for (y = 0; y < a3d_h; ++y) {
        l = back_line(y);
        for (p = 0; p < 40; ++p) if (l[p]) ok = 0;
    }
    check("clear_dirty", ok);

    /* transform + projection: identity at z 256, vertex (40, -40, 0) */
    a3d_mat_ident(&a3d_om);
    a3d_ox = 0; a3d_oy = 0; a3d_oz = 256; a3d_oshift = 0;
    a3d_xform(0, 1, v1);
    a8_log("check: xform v=%d,%d,%d s=%d,%d clip=%u\n", a3d_vx[0], a3d_vy[0], a3d_vz[0],
           a3d_sx[0], a3d_sy[0], a3d_vclip[0]);
    a3d_oz = 4;
    a3d_xform(0, 1, v1);
    check("near", a3d_vclip[0] == 1);
    (void)idx;

    /* a3d_prism: an 80-unit cube straight ahead at 200: drawn, its near
       side (z0: the back) in shade 6 (colour 3) and nothing else seen */
    a3d_clear(0);
    pr_cube(200, 127, 0);
    a3d_pr_shade[0] = 6;
    ok = a3d_prism() == 0;
    l = back_line(a3d_vcy);
    check("prism_draw", ok && l[20] == 0xFF && l[19] == 0xFF && l[0] == 0);
    /* nearer than the near plane: 1, nothing drawn */
    a3d_clear(0);
    pr_cube(30, 127, 0);
    ok = a3d_prism() == 1;
    for (y = 0; y < 40; ++y) if (back_line(y)[20]) ok = 0;
    check("prism_near", ok);
    /* turned 45 degrees: two sides seen (back and left: shades 6 and 2,
       colour 3 right of the middle, colour 1 left of it) */
    a3d_clear(0);
    pr_cube(200, 90, 90);
    a3d_pr_shade[0] = 6; a3d_pr_shade[2] = 2;
    a3d_prism();
    l = back_line(a3d_vcy);
    check("prism_turned", l[17] == 0x55 && l[22] == 0xFF);
    /* the top, below the eye */
    a3d_clear(0);
    pr_cube(200, 127, 0);
    a3d_pr_y0 = -100; a3d_pr_y1 = -20;
    a3d_pr_shade[4] = 2;
    a3d_prism();
    p = 0;
    for (y = 0; y < a3d_h; ++y) if (back_line(y)[20] == 0x55) ++p;
    check("prism_top", p > 1);
    /* a pyramid and a wall quad draw at the middle */
    a3d_clear(0);
    pr_cube(200, 127, 0);
    a3d_pr_y1 = 60;
    for (y = 0; y < 5; ++y) a3d_pr_shade[y] = 6;
    ok = a3d_pyramid() == 0;
    ok = ok && back_line(a3d_vcy + 4)[20];
    a3d_clear(0);
    a3d_pr_ax = 60; a3d_pr_az = 200; a3d_pr_bx = -60; a3d_pr_bz = 200;   /* (right to left: either side) */
    a3d_pr_y0 = -30; a3d_pr_y1 = 30;
    a3d_pr_shade[0] = 6;
    p = a3d_wallquad();
    ok = ok && p == 0 && back_line(a3d_vcy)[20];
    check("prism_pyr_wall", ok);
    a3d_clear(0);
}

static void bench(void)
{
    uint8_t i;
    a3d_mat m;

    bench_start();
    for (i = 0; i < 10; ++i) a3d_clear(A3D_C1);
    bench_end("clear96", 10);

    bench_start();
    for (i = 0; i < 10; ++i) a3d_clear_rows(10, 50, A3D_C2);
    bench_end("clear40rows", 10);

    /* a 40 x 30 pixel quad on screen (10 bytes x 30 lines) */
    a3d_colour(3);
    bench_start();
    for (i = 0; i < 50; ++i) {
        a3d_px[0] = 20; a3d_py[0] = 10;
        a3d_px[1] = 60; a3d_py[1] = 10;
        a3d_px[2] = 60; a3d_py[2] = 40;
        a3d_px[3] = 20; a3d_py[3] = 40;
        a3d_fill(4);
    }
    bench_end("fill40x30", 50);

    /* a triangle, 60 wide and 40 high, through a3d_poly (bbox + no clip) */
    a3d_shade_set(3);
    bench_start();
    for (i = 0; i < 50; ++i) {
        a3d_px[0] = 80; a3d_py[0] = 20;
        a3d_px[1] = 110; a3d_py[1] = 60;
        a3d_px[2] = 50; a3d_py[2] = 60;
        a3d_poly(3);
    }
    bench_end("poly_tri60x40", 50);

    /* the same clipped at the top and the sides */
    bench_start();
    for (i = 0; i < 10; ++i) {
        a3d_px[0] = 80; a3d_py[0] = -40;
        a3d_px[1] = 200; a3d_py[1] = 60;
        a3d_px[2] = -40; a3d_py[2] = 60;
        a3d_poly(3);
    }
    bench_end("poly_clipped", 10);

    bench_start();
    for (i = 0; i < 50; ++i) a3d_line(10, 5, 150, 70, 2);
    bench_end("line140", 50);

    bench_start();
    for (i = 0; i < 50; ++i) a3d_line(30, 2, 60, 90, 1);
    bench_end("line_steep88", 50);

    /* transform + project the cube's 8 vertices */
    a3d_mat_ypr(&a3d_om, 20, 30, 0);
    a3d_ox = 0; a3d_oy = 0; a3d_oz = 300; a3d_oshift = 0;
    bench_start();
    for (i = 0; i < 50; ++i) a3d_xform(0, 8, cube_v);
    bench_end("xform8", 50);

    bench_start();
    for (i = 0; i < 20; ++i) a3d_mat_ypr(&m, i, 30, 10);
    bench_end("mat_ypr", 20);

    bench_start();
    for (i = 0; i < 20; ++i) a3d_mat_mul(&m, &a3d_om, &m);
    bench_end("mat_mul", 20);

    a3d_xform(0, 8, cube_v);
    bench_start();
    for (i = 0; i < 20; ++i) {
        uint8_t f;
        for (f = 0; f < 6; ++f) {
            a3d_shade_set(face_shade[f]);
            a3d_face(4, cube_f[f]);
        }
    }
    bench_end("cube_faces", 20);

    /* the same size of box upright, turned, through a3d_prism */
    bench_start();
    for (i = 0; i < 20; ++i) {
        pr_cube(300, 110, 64);
        a3d_pr_shade[0] = 6; a3d_pr_shade[1] = 2; a3d_pr_shade[2] = 4; a3d_pr_shade[3] = 3; a3d_pr_shade[4] = 5;
        a3d_prism();
    }
    bench_end("prism_box", 20);
    bench_start();
    for (i = 0; i < 20; ++i) {
        pr_cube(300, 110, 64);
        a3d_pr_vis = 0x80;
        a3d_prism();
    }
    bench_end("prism_nofill", 20);
}

/* ---------------------------------------------------------------- demo */
typedef struct { int16_t x, y, z; uint8_t a, b; } obj;
static obj objs[3] = {
    { -110, 0, 360, 0, 0 }, { 0, 20, 420, 40, 80 }, { 120, -10, 380, 90, 20 },
};

static int16_t cam_x, cam_z;
static uint8_t cam_yaw, wire;

static void draw_obj(const a3d_mat *cam, obj *o)
{
    a3d_mat rot;
    int16_t rel[3];
    uint8_t f;
    a3d_rot16(cam, o->x - cam_x, o->y, o->z - cam_z, rel);
    if (rel[2] < 60) return;
    a3d_ox = rel[0]; a3d_oy = rel[1]; a3d_oz = rel[2];
    a3d_mat_ypr(&rot, o->a, o->b, 0);
    a3d_mat_mul(&a3d_om, cam, &rot);
    a3d_oshift = 0;
    a3d_xform(0, 8, cube_v);
    for (f = 0; f < 6; ++f) {
        if (wire) {
            a3d_face_wire(4, cube_f[f], 3);
        } else {
            a3d_shade_set(face_shade[f]);
            a3d_face(4, cube_f[f]);
        }
    }
}

static void demo(void)
{
    a3d_mat cam;
    uint16_t frames = 0, vbls = 0;
    uint8_t in, pressed, i, n;
    int16_t s, c;

    cam_x = 0; cam_z = 0;
    a8_log("a8_3d: ready\n");
    for (;;) {
        in = input_read();
        pressed = input_pressed();
        if (in & IN_LEFT) cam_yaw -= 2;
        if (in & IN_RIGHT) cam_yaw += 2;
        if (in & (IN_UP | IN_DOWN)) {
            s = a3d_sin(cam_yaw) >> 3;
            c = a3d_cos(cam_yaw) >> 3;
            if (in & IN_UP) { cam_x += s; cam_z += c; }
            else { cam_x -= s; cam_z -= c; }
        }
        if (pressed & IN_FIRE) {
            wire ^= 1;
            a8_log("a8_3d: wire=%u\n", wire);
        }
        a3d_cam_ypr(&cam, cam_yaw, 0, 0);
        a3d_clear_dirty(A3D_C0);
        for (i = 0; i < 3; ++i) {
            objs[i].a += 2 + i;
            objs[i].b += 3 - i;
            draw_obj(&cam, &objs[i]);
        }
        n = a3d_flip();
        vbls += n;
        if (++frames == 100) {
            /* fps x 10 = 100 frames * 50 Hz * 10 / vbls */
            uint16_t fps10 = (uint16_t)(50000u / vbls);
            a8_log("a8_3d: fps10=%u\n", fps10);
            a3d_print(0, 0, "FPS");
            a3d_num(4, 0, fps10 / 10, 2);
            frames = 0; vbls = 0;
        }
    }
}

int main(void)
{
    a3d_init(88, 1);
    a3d_view(80, 44, 80);
    sound_init();
    frame_init();
    COLOR4 = A8_COLOR(9, 4);       /* sky (pixel value 0) */
    COLOR0 = A8_COLOR(3, 4);       /* 1 */
    COLOR1 = A8_COLOR(3, 8);       /* 2 */
    COLOR2 = A8_COLOR(3, 12);      /* 3 */
    a3d_print(10, 0, "A3D RENDERER BENCH");
    checks();
    bench();
    a8_log("bench: done\n");
    demo();
    return 0;
}
